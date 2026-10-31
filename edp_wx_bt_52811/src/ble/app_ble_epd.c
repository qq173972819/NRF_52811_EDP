/* SPDX-License-Identifier: Apache-2.0 */
/*
 * edp_html 兼容 GATT 服务：
 *   Service  : 62750001-d828-918d-fb46-b6c11c675aec
 *   RX (Write): 62750002-d828-918d-fb46-b6c11c675aec
 *   TX (Notify+Read 首字节固件版本): 62750003-d828-918d-fb46-b6c11c675aec
 * 命令：0x30 WRITE_IMG / 0x31 WRITE_BLOCK / 0x32 QUERY_STATUS /
 *       0x33 RESET_TRANSFER / 0x05 REFRESH / 0x01 INIT / 0x02 CLEAR /
 *       0x06 SLEEP；CRC16-CCITT（0x8408、初值 0xFFFF）。
 * 非阻塞约束：RX 写回调仅做协议解析与数据拷贝，屏幕 SPI 时序全部经
 * k_work 工作项在系统线程执行，严禁在 BLE 回调内阻塞。
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "app_ble_epd.h"
#include "app_config.h"
#include "epd/epd_154d.h"
#include "led/app_led.h"
#include "power/app_power.h"
#include "protocol/epd_protocol.h"

LOG_MODULE_REGISTER(app_ble_epd, CONFIG_LOG_DEFAULT_LEVEL);

#define FIRMWARE_VERSION 0x20U /* 支持 0x31 CRC 块传图（>= 0x20） */

/* 屏幕被别处占用时的重排间隔（上电自检期间蓝牙可能已连接） */
#define EPD_BUSY_RETRY_MS 50U

/* 128-bit UUID 存储顺序：小端（LSB first），即把标准 UUID 字符串的 16 字节
 * 整体倒序写入。示例 6275000X-d828-918d-fb46-b6c11c675aec 的 BE 字节为
 *   62 75 00 0X d8 28 91 8d fb 46 b6 c1 1c 67 5a ec
 * 倒序后 = ec 5a 67 1c c1 b6 46 fb 8d 91 28 d8 0X 00 75 62
 * 注意 b6 c1 1c 67 段倒序为 1c c1，切勿写成 c1 1c（会导致广播/服务 UUID
 * 变成 b61cc1675aec，网页端 getPrimaryService 找不到服务）。
 */
#define EPD_SVC_UUID_LE                                                          \
	{                                                                          \
		0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb, 0x8d, 0x91, 0x28, \
			0xd8, 0x01, 0x00, 0x75, 0x62                                          \
	}
#define EPD_RX_UUID_LE                                                            \
	{                                                                          \
		0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb, 0x8d, 0x91, 0x28, \
			0xd8, 0x02, 0x00, 0x75, 0x62                                          \
	}
#define EPD_TX_UUID_LE                                                            \
	{                                                                          \
		0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb, 0x8d, 0x91, 0x28, \
			0xd8, 0x03, 0x00, 0x75, 0x62                                          \
	}

static const struct bt_uuid_128 epd_svc_uuid = BT_UUID_INIT_128(
	0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb,
	0x8d, 0x91, 0x28, 0xd8, 0x01, 0x00, 0x75, 0x62);
static const struct bt_uuid_128 epd_rx_uuid = BT_UUID_INIT_128(
	0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb,
	0x8d, 0x91, 0x28, 0xd8, 0x02, 0x00, 0x75, 0x62);
static const struct bt_uuid_128 epd_tx_uuid = BT_UUID_INIT_128(
	0xec, 0x5a, 0x67, 0x1c, 0xc1, 0xb6, 0x46, 0xfb,
	0x8d, 0x91, 0x28, 0xd8, 0x03, 0x00, 0x75, 0x62);

/* 广播数据载荷（文件作用域：BT_DATA 初始化需编译期常量地址） */
static const uint8_t epd_adv_flags = BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR;
static const uint8_t epd_svc_uuid_ad[16] = EPD_SVC_UUID_LE;

static struct bt_conn *epd_conn;
static bool param_defense_requested;
static uint16_t last_timeout;
static struct k_work_delayable epd_param_work;
static struct epd_transfer epd_transfer_state;

/* 断开/连接失败后恢复广播：控制器在连接建立时会自动停止广播，断开后
 * 必须重新 bt_le_adv_start，否则设备再也搜不到（网页端表现为"连接断开
 * 后再点连接找不到设备"）。
 */
static struct k_work_delayable adv_restart_work;
static uint8_t adv_restart_left;
#define ADV_RESTART_DELAY_MS 200U /* 等控制器完成链路拆除 */
#define ADV_RESTART_RETRY_MS 300U /* 重试间隔 */
#define ADV_RESTART_MAX_ATTEMPTS 10U

/* 动态广播名：NRF_EDP_XXXX
 * XXXX = 身份地址 MAC 显示串的最后四位（即最后两个字节），大写 hex。
 * 长度 12 字节可视字符 + 结尾 NUL；放入 scan response，容量充足。
 */
static char epd_name_str[13];
static uint8_t epd_name_bytes[12];

static int epd_build_device_name(void)
{
	bt_addr_le_t addrs[CONFIG_BT_ID_MAX];
	size_t addr_count = ARRAY_SIZE(addrs);
	const uint8_t *a;

	bt_id_get(addrs, &addr_count);
	if (addr_count == 0U) {
		LOG_WRN("identity address unavailable");
		return -ENODEV;
	}

	/* 无效地址（全 0）时拒绝广播名，避免空名广播 */
	a = addrs[0].a.val;
	if (a[0] == 0U && a[1] == 0U && a[2] == 0U &&
	    a[3] == 0U && a[4] == 0U && a[5] == 0U) {
		LOG_WRN("identity address invalid");
		return -ENODEV;
	}

	/* 身份地址字节序为 LSB-first，显示顺序 a[5]:a[4]:...:a[0]（同
	 * bt_addr_le_to_str）。广播名取显示串的最后四位，即 a[1]:a[0]。
	 * 例：MAC 显示为 D5:B2:3A:66:16:42 -> NRF_EDP_1642
	 */
	snprintk(epd_name_str, sizeof(epd_name_str), "NRF_EDP_%02X%02X",
		 a[1], a[0]);
	memcpy(epd_name_bytes, epd_name_str, sizeof(epd_name_bytes));
	LOG_INF("advertising name: %s", epd_name_str);
	return 0;
}

/* 待处理工作项：RX 回调拷贝后交给系统 workqueue，SPI 操作全部在此执行 */
struct epd_pending {
	uint8_t action;
	uint8_t payload[EPD_PROTOCOL_MAX_PAYLOAD];
	size_t payload_length;
	bool ack;		 /* 需要发送 0xA0 块 ACK */
	uint16_t ack_block_id;
	uint8_t ack_status;
	struct bt_conn *conn;	 /* 响应通知目标 */
};

static struct epd_pending epd_pending;
static struct k_spinlock epd_lock;
static struct k_work_delayable epd_work;

static ssize_t epd_rx_write_cb(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags);
static ssize_t epd_tx_read_cb(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset);
static void epd_work_handler(struct k_work *work);
static void epd_connected(struct bt_conn *conn, uint8_t err);
static void epd_disconnected(struct bt_conn *conn, uint8_t reason);
static void epd_conn_le_param_updated(struct bt_conn *conn, uint16_t interval,
				      uint16_t latency, uint16_t timeout);
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static void epd_conn_le_data_len_updated(struct bt_conn *conn,
					 struct bt_conn_le_data_len_info *info);
#endif
static void epd_gatt_att_mtu_updated(struct bt_conn *conn, uint16_t tx,
				     uint16_t rx);
static int start_advertising_internal(uint32_t interval_ms);
static void adv_restart_work_handler(struct k_work *work);

static struct bt_conn_cb epd_conn_callbacks = {
	.connected = epd_connected,
	.disconnected = epd_disconnected,
	.le_param_updated = epd_conn_le_param_updated,
#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
	.le_data_len_updated = epd_conn_le_data_len_updated,
#endif
};

static struct bt_gatt_cb epd_gatt_callbacks = {
	.att_mtu_updated = epd_gatt_att_mtu_updated,
};

/* RX/TX 特征 CCC 变更回调（Zephyr 4.x 托管 CCC，无需自行管理 cfg/value） */
static void epd_rx_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				   uint16_t value)
{
	(void)attr;
	LOG_INF("RX CCC changed value=0x%04x", value);
}

static void epd_tx_ccc_cfg_changed(const struct bt_gatt_attr *attr,
				   uint16_t value)
{
	(void)attr;
	LOG_INF("TX CCC changed value=0x%04x", value);
}

BT_GATT_SERVICE_DEFINE(epd_svc,
	BT_GATT_PRIMARY_SERVICE(&epd_svc_uuid),
	BT_GATT_CHARACTERISTIC(&epd_rx_uuid.uuid,
			       BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
			       BT_GATT_PERM_WRITE, NULL, epd_rx_write_cb, NULL),
	BT_GATT_CCC(epd_rx_ccc_cfg_changed, BT_GATT_PERM_WRITE),
	BT_GATT_CHARACTERISTIC(&epd_tx_uuid.uuid,
			       BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, epd_tx_read_cb, NULL, NULL),
	BT_GATT_CCC(epd_tx_ccc_cfg_changed, BT_GATT_PERM_WRITE),
);

/* 值特征 attr 索引：svc(0) rx decl(1) rx val(2) rx ccc(3)
 * tx decl(4) tx val(5) tx ccc(6) */
#define EPD_RX_VALUE_ATTR (&epd_svc.attrs[2])
#define EPD_TX_VALUE_ATTR (&epd_svc.attrs[5])

static void notify_attr(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			const uint8_t *data, size_t length)
{
	if (conn == NULL || length == 0U || length > UINT16_MAX) {
		return;
	}
	if (!bt_gatt_is_subscribed(conn, attr, BT_GATT_CCC_NOTIFY)) {
		return;
	}

	struct bt_gatt_notify_params params = {
		.attr = attr,
		.data = data,
		.len = (uint16_t)length,
	};
	int err = bt_gatt_notify_cb(conn, &params);

	if (err != 0) {
		LOG_WRN("notify failed len=%zu err=%d", length, err);
	}
}

static void notify_tx(struct bt_conn *conn, const uint8_t *data, size_t length)
{
	/* edp_html 在 RX 上订阅通知；TX 保留兼容旧客户端。 */
	notify_attr(conn, EPD_RX_VALUE_ATTR, data, length);
	notify_attr(conn, EPD_TX_VALUE_ATTR, data, length);
}

static void notify_tx_ack(struct bt_conn *conn, uint16_t block_id,
			  uint8_t status)
{
	uint8_t ack[4];

	ack[0] = 0xA0U;
	ack[1] = (uint8_t)block_id;
	ack[2] = (uint8_t)(block_id >> 8);
	ack[3] = status;

	notify_tx(conn, ack, sizeof(ack));
}

static void queue_pending(uint8_t action, const uint8_t *payload,
			  size_t payload_length, struct bt_conn *conn,
			  bool ack, uint16_t ack_block_id, uint8_t ack_status)
{
	k_spinlock_key_t key = k_spin_lock(&epd_lock);

	epd_pending.action = action;
	if (payload != NULL && payload_length <= sizeof(epd_pending.payload)) {
		memcpy(epd_pending.payload, payload, payload_length);
		epd_pending.payload_length = payload_length;
	} else {
		epd_pending.payload_length = 0U;
	}
	epd_pending.ack = ack;
	epd_pending.ack_block_id = ack_block_id;
	epd_pending.ack_status = ack_status;
	epd_pending.conn = conn;

	k_spin_unlock(&epd_lock, key);
	(void)k_work_schedule(&epd_work, K_NO_WAIT);
}

static ssize_t epd_rx_write_cb(struct bt_conn *conn,
			       const struct bt_gatt_attr *attr,
			       const void *buf, uint16_t len, uint16_t offset,
			       uint8_t flags)
{
	const uint8_t *packet = buf;
	const uint8_t *payload = NULL;
	size_t payload_length = 0U;
	uint8_t action = EPD_ACTION_NONE;
	int result;

	(void)attr;
	(void)flags;

	LOG_INF("RX write offset=%u len=%u", offset, len);

	if (offset != 0U) {
		LOG_WRN("RX write invalid offset=%u len=%u", offset, len);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
	}
	if (packet == NULL || len == 0U) {
		LOG_WRN("RX write invalid len=%u packet=%p", len, packet);
		return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
	}

	result = epd_protocol_handle(&epd_transfer_state, packet, len,
				     &payload, &payload_length, &action);
	if (result < 0) {
		if (action == EPD_ACTION_ABORT) {
			queue_pending(EPD_ACTION_ABORT, NULL, 0U, conn, false, 0U,
				      0U);
		} else {
			LOG_WRN("protocol reject cmd=0x%02x len=%u result=%d",
				packet[0], len, result);
		}
		return len;
	}

	switch (action) {
	case EPD_ACTION_QUERY_STATUS:
		queue_pending(EPD_ACTION_QUERY_STATUS, NULL, 0U, conn, false, 0U,
			      0U);
		break;
	default:
		if (packet[0] == EPD_CMD_WRITE_BLOCK) {
			/* 块模式：每块回 0xA0 ACK（成功 0x01 / 重复 0x03） */
			uint16_t block_id =
				(uint16_t)packet[1] | ((uint16_t)packet[2] << 8);
			uint8_t status = 0x01U;

			if (result == EPD_PROTOCOL_BLOCK_DROPPED) {
				status = 0x02U;
			} else if (payload_length == 0U && payload == NULL) {
				status = 0x03U; /* 重复块 */
			}
			queue_pending(action, payload, payload_length, conn, true,
				      block_id, status);
		} else {
			queue_pending(action, payload, payload_length, conn, false,
				      0U, 0U);
		}
		break;
	}

	return len;
}

static ssize_t epd_tx_read_cb(struct bt_conn *conn,
			      const struct bt_gatt_attr *attr, void *buf,
			      uint16_t len, uint16_t offset)
{
	uint8_t version = FIRMWARE_VERSION;

	LOG_INF("TX read offset=%u len=%u", offset, len);

	return bt_gatt_attr_read(conn, attr, buf, len, offset, &version,
				 sizeof(version));
}

static void epd_work_handler(struct k_work *work)
{
	k_spinlock_key_t key;

	/* 屏幕被主线程占用时（上电自检尚未结束）延后处理，避免两条路径
	 * 同时驱动 SPI；工作项本身保留 pending，稍后自动重入。 */
	if (epd_154d_is_busy()) {
		(void)k_work_schedule(&epd_work, K_MSEC(EPD_BUSY_RETRY_MS));
		return;
	}

	key = k_spin_lock(&epd_lock);
	struct epd_pending pending = epd_pending;
	bool send_ack = pending.ack;
	uint16_t ack_block_id = pending.ack_block_id;
	uint8_t ack_status = pending.ack_status;
	struct bt_conn *conn = pending.conn;

	k_spin_unlock(&epd_lock, key);

	switch (pending.action) {
	case EPD_ACTION_START_IMAGE:
		app_led_set_receiving(true);
		epd_154d_init();
		epd_154d_begin_image();
		if (pending.payload_length > 0U) {
			epd_154d_write_image(pending.payload,
					     pending.payload_length);
		}
		break;

	case EPD_ACTION_WRITE:
		if (pending.payload_length > 0U) {
			epd_154d_write_image(pending.payload,
					     pending.payload_length);
		}
		break;

	case EPD_ACTION_REFRESH:
		epd_154d_refresh();
		epd_154d_abort();	/* 0x02/0x07 休眠，下次会话重新初始化 */
		app_led_set_receiving(false);
		app_led_refresh_flash();
		app_power_transfer_finished();
		break;

	case EPD_ACTION_ABORT:
		epd_154d_abort();
		app_led_set_receiving(false);
		app_power_transfer_failed();
		break;

	case EPD_ACTION_INIT:
		epd_154d_init();
		/* 网页端在 RX 通知中读取屏幕配置和传输参数。 */
		{
			static const uint8_t epd_web_config[] = {
				0x14U, 0x13U, 0x06U, 0x05U, 0x04U, 0x03U, 0x02U,
				0x23U, 0xffU, 0xffU, 0x07U, 0x01U, 0x00U, 0xffU,
			};
			static const uint8_t epd_web_fid[] = "FID=DONGSHAN";
			static const uint8_t epd_web_mtu[] = "mtu=244";

			notify_tx(conn, epd_web_config, sizeof(epd_web_config));
			notify_tx(conn, epd_web_fid, sizeof(epd_web_fid) - 1U);
			notify_tx(conn, epd_web_mtu, sizeof(epd_web_mtu) - 1U);
		}
		break;

	case EPD_ACTION_CLEAR: {
		/* 全白清屏：分批写入 0x55（2bpp 白=01） */
		static const uint8_t white_run[64] = {
			[0 ... 63] = 0x55U,
		};
		size_t written = 0U;

		epd_154d_init();
		epd_154d_begin_image();
		while (written < EPD_IMAGE_LENGTH) {
			const size_t chunk =
				MIN(sizeof(white_run), EPD_IMAGE_LENGTH - written);

			epd_154d_write_image(white_run, chunk);
			written += chunk;
		}
		epd_154d_refresh();
		epd_154d_abort();
		app_led_set_receiving(false);
		app_led_refresh_flash();
		app_power_transfer_finished();
		break;
	}

	case EPD_ACTION_SLEEP:
		epd_154d_abort();
		break;

	case EPD_ACTION_QUERY_STATUS: {
		uint8_t status[EPD_PROTOCOL_MAX_STATUS_LENGTH];
		size_t status_length = epd_protocol_build_status(
			&epd_transfer_state, status, sizeof(status));

		notify_tx(conn, status, status_length);
		break;
	}

	default:
		break;
	}

	if (send_ack) {
		notify_tx_ack(conn, ack_block_id, ack_status);
	}
}

static void schedule_adv_restart(void)
{
	adv_restart_left = ADV_RESTART_MAX_ATTEMPTS;
	(void)k_work_schedule(&adv_restart_work, K_MSEC(ADV_RESTART_DELAY_MS));
}

static void adv_restart_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	/* 已被新连接占用则无需广播 */
	if (epd_conn != NULL) {
		return;
	}

	err = start_advertising_internal(BLE_SLOW_ADV_INTERVAL_MS);
	if (err == 0) {
		adv_restart_left = 0U;
		LOG_INF("advertising restarted");
		return;
	}
	if (err == -EALREADY) {
		adv_restart_left = 0U;
		return;
	}

	if (adv_restart_left > 0U) {
		adv_restart_left--;
	}
	LOG_WRN("advertising restart failed err=%d (retry left=%u)", err,
		adv_restart_left);
	if (adv_restart_left > 0U) {
		(void)k_work_schedule(&adv_restart_work,
				      K_MSEC(ADV_RESTART_RETRY_MS));
	} else {
		LOG_ERR("advertising restart gave up err=%d", err);
	}
}

static void epd_connected(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (err != 0U) {
		LOG_WRN("connection failed addr=%s err=0x%02x", addr, err);
		/* 连接失败后必须恢复广播，否则设备不可再被发现 */
		schedule_adv_restart();
		return;
	}

	/* 连接建立：取消待执行的广播恢复，避免与链路冲突 */
	(void)k_work_cancel_delayable(&adv_restart_work);
	epd_conn = bt_conn_ref(conn);
	param_defense_requested = false;
	last_timeout = 0U;
	app_led_set_connected(true);
	/* 连接/断开事件用 printk 直出（不经 LOG 后端，避免 RTT DROP 丢弃） */
	printk("connected addr=%s\n", addr);
}

static void epd_disconnected(struct bt_conn *conn, uint8_t reason)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

	if (epd_conn == conn) {
		bt_conn_unref(epd_conn);
		epd_conn = NULL;
	}

	/* 断开：中止进行中的会话并让屏幕休眠 */
	if (epd_transfer_state.state != EPD_TRANSFER_IDLE) {
		epd_transfer_reset(&epd_transfer_state);
		queue_pending(EPD_ACTION_ABORT, NULL, 0U, NULL, false, 0U, 0U);
	}

	app_led_set_connected(false);
	app_led_set_receiving(false);
	k_work_cancel_delayable(&epd_param_work);
	/* 断开事件必须直接可见（RTT DROP 模式下 LOG 后端可能丢弃输出） */
	printk("disconnected addr=%s reason=0x%02x\n", addr, reason);

	/* 关键：恢复广播，保证下次还能被网页端搜索到 */
	schedule_adv_restart();
}

static void epd_param_work_handler(struct k_work *work)
{
	struct bt_conn *conn = epd_conn;

	/* 连接已断开则放弃；防御请求每连接最多一次 */
	if (conn == NULL || param_defense_requested) {
		return;
	}

	static const struct bt_le_conn_param defense = {
		.interval_min = 24U,
		.interval_max = 24U,
		.latency = 0U,
		.timeout = 500U,
	};
	int err = bt_conn_le_param_update(conn, &defense);

	param_defense_requested = true;
	LOG_WRN("supervision timeout too short (%u), requested "
		"timeout=500 units ret=%d", last_timeout, err);
}

static void epd_conn_le_param_updated(struct bt_conn *conn, uint16_t interval,
				      uint16_t latency, uint16_t timeout)
{
	LOG_INF("conn params interval=%u latency=%u timeout=%u", interval,
		latency, timeout);

	last_timeout = timeout;

	/* 网页端请求的 supervision timeout 若短于 500ms，链路对任何一次
	 * 处理延迟都极敏感（尤其 GATT 交互期间），作为从机防御性地请求
	 * 一次更宽松的参数（每连接仅一次，被对端拒绝不重试）。
	 * 请求经 system workqueue 延迟 100ms 执行，避免在 BT 回调内直接
	 * 发起 connection update（SoftDevice Controller 对该操作有单飞行
	 * 限制，与对端 LL 参数请求竞争可能导致状态异常）。
	 */
	if (timeout < 50U && !param_defense_requested) {
		k_work_schedule(&epd_param_work, K_MSEC(100));
	}
}

#if defined(CONFIG_BT_USER_DATA_LEN_UPDATE)
static void epd_conn_le_data_len_updated(struct bt_conn *conn,
					 struct bt_conn_le_data_len_info *info)
{
	if (info != NULL) {
		LOG_INF("DLE updated tx_len=%u rx_len=%u", info->tx_len,
			info->rx_len);
	}
}
#endif

static void epd_gatt_att_mtu_updated(struct bt_conn *conn, uint16_t tx,
				     uint16_t rx)
{
	LOG_INF("ATT MTU tx=%u rx=%u", tx, rx);
}

static int ble_stack_init(void)
{
	int err = bt_enable(NULL);

	if (err != 0) {
		LOG_ERR("bt_enable failed: %d", err);
		return err;
	}

	LOG_INF("bluetooth stack enabled");

	err = bt_conn_cb_register(&epd_conn_callbacks);
	if (err != 0) {
		LOG_ERR("conn callbacks register failed: %d", err);
	}

	bt_gatt_cb_register(&epd_gatt_callbacks);

	/*
	 * 静态注册的 GATT 服务在编译期即进入 GATT DB，无需动态注册。
	 * 注意：bt_gatt_find_by_uuid 仅按 attr->uuid 匹配；BT_GATT_PRIMARY_SERVICE
	 * 生成的 attr->uuid 固定为 0x2800（服务 UUID 存放在 user_data），因此
	 * 用服务 UUID 查找会恒失败并误报 "NOT found"。改为验证 RX/TX 特征值，
	 * 能真实反映服务定义数组是否完整。
	 */
	if (bt_gatt_find_by_uuid(epd_svc.attrs, epd_svc.attr_count,
				&epd_rx_uuid.uuid) == NULL ||
	    bt_gatt_find_by_uuid(epd_svc.attrs, epd_svc.attr_count,
				&epd_tx_uuid.uuid) == NULL) {
		LOG_ERR("GATT RX/TX characteristic(s) NOT in service attrs");
	} else {
		LOG_INF("GATT service attrs ok (rx/tx found)");
	}

	return 0;
}

static int start_advertising_internal(uint32_t interval_ms)
{
	uint32_t units = (interval_ms * 1000U) / 625U;
	static struct bt_le_adv_param adv_param =
		BT_LE_ADV_PARAM_INIT(BT_LE_ADV_OPT_CONN, 0U, 0U, NULL);
	static const struct bt_data ad[] = {
		BT_DATA(BT_DATA_FLAGS, &epd_adv_flags, sizeof(epd_adv_flags)),
		BT_DATA(BT_DATA_UUID128_ALL, epd_svc_uuid_ad,
			sizeof(epd_svc_uuid_ad)),
	};
	/* 广播名随身份地址动态生成（NRF_EDP_XXXX, 12B），放入 scan response。 */
	static const struct bt_data sd[] = {
		BT_DATA(BT_DATA_NAME_COMPLETE, epd_name_bytes,
			sizeof(epd_name_bytes)),
	};
	int err;

	err = epd_build_device_name();
	if (err != 0) {
		return err;
	}

	adv_param.interval_min = units;
	adv_param.interval_max = units;

	/* Idempotent start: stop first (ignore -EALREADY when not
	 * advertising), so repeated calls never fail with -EALREADY.
	 */
	(void)bt_le_adv_stop();

	err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
	if (err != 0) {
		LOG_ERR("advertising start failed: %d", err);
		return err;
	}

	LOG_INF("advertising (interval %u ms)", interval_ms);
	return 0;
}

int app_ble_epd_init(void)
{
	int err = ble_stack_init();

	if (err != 0) {
		return err;
	}

	k_work_init_delayable(&epd_work, epd_work_handler);
	k_work_init_delayable(&epd_param_work, epd_param_work_handler);
	k_work_init_delayable(&adv_restart_work, adv_restart_work_handler);
	adv_restart_left = 0U;
	epd_transfer_reset(&epd_transfer_state);
	memset(&epd_pending, 0, sizeof(epd_pending));

	return start_advertising_internal(BLE_SLOW_ADV_INTERVAL_MS);
}

int app_ble_epd_start_advertising(uint32_t interval_ms)
{
	return start_advertising_internal(interval_ms);
}

void app_ble_epd_stop(void)
{
	int err;

	(void)k_work_cancel_delayable(&adv_restart_work);
	adv_restart_left = 0U;
	err = bt_le_adv_stop();

	LOG_INF("advertising stop requested ret=%d", err);
}

bool app_ble_epd_is_busy(void)
{
	return epd_transfer_state.state != EPD_TRANSFER_IDLE;
}

void app_ble_epd_disconnect(void)
{
	if (epd_conn != NULL) {
		bt_conn_disconnect(epd_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
	}
}

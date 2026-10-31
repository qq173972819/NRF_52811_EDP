/* SPDX-License-Identifier: Apache-2.0 */
/*
 * edp_html 兼容分块协议（三方共同遵守）：
 *   命令表：0x00 SET_PINS / 0x01 INIT / 0x02 CLEAR / 0x03 SEND_CMD /
 *           0x04 SEND_DATA / 0x05 REFRESH / 0x06 SLEEP / 0x20 SET_TIME /
 *           0x21 SET_WEEK_START / 0x30 WRITE_IMG / 0x31 WRITE_BLOCK /
 *           0x32 QUERY_STATUS / 0x33 RESET_TRANSFER
 *   图像：200x200 四色 2bpp（黑00/白01/黄10/红11），每字节 4 像素 MSB-first，
 *         总长 10000 字节。
 *   CRC16-CCITT：反射多项式 0x8408、初值 0xFFFF、逐字节右移异或、结果 LE。
 * 纯逻辑模块，不依赖硬件，供 BLE 层调用与 ztest 验证。
 */

#ifndef EPD_PROTOCOL_H_
#define EPD_PROTOCOL_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- 命令常量（与 edp_html 网页端一致） ---- */
#define EPD_CMD_SET_PINS		0x00U
#define EPD_CMD_INIT			0x01U
#define EPD_CMD_CLEAR			0x02U
#define EPD_CMD_SEND_CMD		0x03U
#define EPD_CMD_SEND_DATA		0x04U
#define EPD_CMD_REFRESH			0x05U
#define EPD_CMD_SLEEP			0x06U
#define EPD_CMD_SET_TIME		0x20U
#define EPD_CMD_SET_WEEK_START		0x21U
#define EPD_CMD_WRITE_IMG		0x30U
#define EPD_CMD_WRITE_BLOCK		0x31U
#define EPD_CMD_QUERY_STATUS		0x32U
#define EPD_CMD_RESET_TRANSFER		0x33U

/* ---- cfg 位域（WRITE_IMG / WRITE_BLOCK） ---- */
#define EPD_CFG_LAYER_MASK		0x0FU
#define EPD_CFG_FIRST_MASK		0xF0U
/* 高 4 位：首块标志 */
#define EPD_CFG_FIRST_BLOCK		0x00U
#define EPD_CFG_CONTINUE		0xF0U
/* 低 4 位：图层 */
#define EPD_CFG_LAYER_COLOR		0x00U	/* 彩色层（四色单步 / 三色红色层） */
#define EPD_CFG_LAYER_BW		0x0FU	/* 黑白层（黑白 / 三色 A 面） */

/* ---- 图像常量 ---- */
#define EPD_IMAGE_WIDTH			200U
#define EPD_IMAGE_HEIGHT		200U
#define EPD_IMAGE_LENGTH		10000U
#define EPD_IMAGE_BITS_PER_PIXEL	2U

/* 块模式：单块 payload 上限（ATT_MTU=247 时 N=236，留余量） */
#define EPD_PROTOCOL_MAX_PAYLOAD	240U
/* 块模式最大块数（ATT_MTU=31 时 N=20 -> 500 块；bitmap 64B 支持 512 块） */
#define EPD_PROTOCOL_MAX_BLOCKS		512U
#define EPD_PROTOCOL_BITMAP_BYTES	(EPD_PROTOCOL_MAX_BLOCKS / 8U)

/* 块模式数据块最小长度：cmd(1)+block_id(2)+total(2)+cfg(1)+crc16(2)=8 */
#define EPD_BLOCK_HEADER_LENGTH		6U
#define EPD_BLOCK_CRC_LENGTH		2U

/* 0x31 块模式单块 payload 下限（ATT_MTU>=31 时） */
#define EPD_BLOCK_PAYLOAD_MIN		20U

/* ---- 传输状态机 ---- */
enum epd_transfer_state {
	EPD_TRANSFER_IDLE,
	EPD_TRANSFER_RECEIVING,
	EPD_TRANSFER_COMPLETE,
};

/* 协议处理结果 */
enum epd_protocol_result {
	EPD_PROTOCOL_OK = 0,
	EPD_PROTOCOL_IGNORED = 1,	/* 命令已接受但无需动作（SET_PINS 等） */
	EPD_PROTOCOL_BLOCK_DROPPED = 2,	/* 块 CRC 失败被丢弃，会话继续 */
	EPD_PROTOCOL_BAD_STATE = -1,	/* 状态不允许该命令（如 IDLE 收续块） */
	EPD_PROTOCOL_BAD_LENGTH = -2,	/* 包长/越界错误（越界会中止会话） */
	EPD_PROTOCOL_BAD_CRC = -3,	/* CRC 校验失败 */
};

/* 动作（BLE 层据此编排工作项，严禁在回调内直接执行耗时操作） */
enum epd_protocol_action {
	EPD_ACTION_NONE = 0,
	EPD_ACTION_START_IMAGE,		/* 复位屏幕 + 初始化 + 进入 0x10 RAM 写模式 */
	EPD_ACTION_WRITE,		/* 将 payload 写入屏幕 RAM */
	EPD_ACTION_REFRESH,		/* 执行 0x12 刷新 + 休眠 */
	EPD_ACTION_ABORT,		/* 错误中止会话（不刷新），屏幕休眠 */
	EPD_ACTION_INIT,		/* 0x01 屏幕初始化 */
	EPD_ACTION_CLEAR,		/* 0x02 清屏 */
	EPD_ACTION_SLEEP,		/* 0x06 屏幕休眠 */
	EPD_ACTION_QUERY_STATUS,	/* 0x32 查询：构造 0xA1 状态响应 */
};

struct epd_transfer {
	enum epd_transfer_state state;
	uint32_t bytes_written;		/* 普通模式累计写入字节 */
	uint16_t total_blocks;		/* 块模式总块数（0=普通模式） */
	uint16_t received_blocks;	/* 块模式已收块数 */
	uint16_t session_id;		/* 块模式会话 id（0x33 重置时设置） */
	uint16_t image_length;		/* 图像长度（固定 10000） */
	uint8_t block_bitmap[EPD_PROTOCOL_BITMAP_BYTES]; /* 块模式已收位图 */
};

void epd_transfer_reset(struct epd_transfer *transfer);

/* CRC16-CCITT：反射 0x8408、初值 0xFFFF（与 edp_html ble_transfer.js 一致） */
uint16_t epd_protocol_crc16(const uint8_t *data, size_t length);

/*
 * 协议统一入口。解析一包 RX 数据，更新传输状态。
 * 成功（OK/BLOCK_DROPPED）时：
 *   - payload/payload_length 指向待写屏数据（无数据时置 NULL/0）；
 *   - action 给出 BLE 层应执行的动作。
 * 失败（<0）时：
 *   - action 可能为 EPD_ACTION_ABORT（越界）或 NONE；
 *   - 会话状态按错误类型处理（越界中止，其余保留）。
 */
int epd_protocol_handle(struct epd_transfer *transfer, const uint8_t *packet,
			size_t length, const uint8_t **payload,
			size_t *payload_length, uint8_t *action);

/*
 * 构造 0x32 QUERY_STATUS 的状态响应（0xA1 格式）：
 *   [0xA1][total:2 LE][received:2 LE][session:1][active:1][bitmap:ceil(total/8)]
 * 写入 out（容量须 >= EPD_PROTOCOL_MAX_STATUS_LENGTH），返回响应长度。
 */
#define EPD_PROTOCOL_MAX_STATUS_LENGTH	(1 + 2 + 2 + 1 + 1 + EPD_PROTOCOL_BITMAP_BYTES)
size_t epd_protocol_build_status(struct epd_transfer *transfer, uint8_t *out,
				 size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* EPD_PROTOCOL_H_ */

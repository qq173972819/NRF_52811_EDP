#include "app_beacon.h"

#include <errno.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "APP/app_beacon/app_beacon_parser.h"
#include "APP/app_led/app_led.h"

LOG_MODULE_REGISTER(app_beacon, CONFIG_LOG_DEFAULT_LEVEL);

static const struct i2c_dt_spec esp32s3 =
    I2C_DT_SPEC_GET(DT_NODELABEL(esp32s3));

K_MSGQ_DEFINE(report_queue, APP_BEACON_FRAME_LENGTH, 1, 4);

static void queue_latest_frame(const uint8_t frame[APP_BEACON_FRAME_LENGTH])
{
    uint8_t discarded[APP_BEACON_FRAME_LENGTH];

    if (k_msgq_put(&report_queue, frame, K_NO_WAIT) == 0) {
        return;
    }

    (void)k_msgq_get(&report_queue, discarded, K_NO_WAIT);
    (void)k_msgq_put(&report_queue, frame, K_NO_WAIT);
}

static bool parse_ad_data(struct bt_data *data, void *user_data)
{
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    ARG_UNUSED(user_data);

    if (data->type != BT_DATA_MANUFACTURER_DATA) {
        return true;
    }

    if (!app_beacon_parse_manufacturer(data->data, data->data_len, frame)) {
        return true;
    }

    queue_latest_frame(frame);
    return false;
}

static void device_found(const bt_addr_le_t *addr, int8_t rssi,
                         uint8_t adv_type, struct net_buf_simple *ad)
{
    ARG_UNUSED(addr);
    ARG_UNUSED(rssi);
    ARG_UNUSED(adv_type);

    bt_data_parse(ad, parse_ad_data, NULL);
}

static void report_thread(void *p1, void *p2, void *p3)
{
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    while (true) {
        k_msgq_get(&report_queue, frame, K_FOREVER);

        int ret = i2c_write_dt(&esp32s3, frame, sizeof(frame));
        if (ret < 0) {
            LOG_ERR("I2C report failed: %d", ret);
            continue;
        }

        // LOG_HEXDUMP_INF(frame, sizeof(frame), "Beacon report");
        ret = LED_ON();
        if (ret < 0) {
            LOG_ERR("SYS-LED on failed: %d", ret);
            continue;
        }
        k_msleep(100);
        ret = LED_OFF();
        if (ret < 0) {
            LOG_ERR("SYS-LED off failed: %d", ret);
        }
    }
}

K_THREAD_DEFINE(report_thread_id, 1024, report_thread, NULL, NULL, NULL,
                9, 0, 0);

int app_beacon_init(void)
{
    static const struct bt_le_scan_param scan_param =
        BT_LE_SCAN_PARAM_INIT(BT_LE_SCAN_TYPE_PASSIVE,
                              BT_LE_SCAN_OPT_NONE,
                              BT_GAP_SCAN_FAST_INTERVAL,
                              BT_GAP_SCAN_FAST_WINDOW);

    if (!i2c_is_ready_dt(&esp32s3)) {
        LOG_ERR("ESP32S3 I2C bus is not ready");
        return -ENODEV;
    }

    int ret = bt_enable(NULL);
    if (ret < 0) {
        LOG_ERR("Bluetooth init failed: %d", ret);
        return ret;
    }

    ret = bt_le_scan_start(&scan_param, device_found);
    if (ret < 0) {
        LOG_ERR("Bluetooth scan start failed: %d", ret);
        return ret;
    }

    LOG_INF("Scanning Nordic iBeacon UUID prefix 20250901");
    return 0;
}

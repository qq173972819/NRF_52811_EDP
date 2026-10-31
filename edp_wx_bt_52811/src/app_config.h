/* SPDX-License-Identifier: Apache-2.0 */

#ifndef APP_CONFIG_H_
#define APP_CONFIG_H_

/* Always-resident power: this PCB revision has no wake button, so
 * System OFF is intentionally not used and the device keeps running.
 */
#define POWER_MODE_SLOW_ADVERTISING 0

#ifndef APP_POWER_MODE
#define APP_POWER_MODE POWER_MODE_SLOW_ADVERTISING
#endif

#ifndef APP_BLE_DEVICE_NAME
#define APP_BLE_DEVICE_NAME "NRF52811-EPD"
#endif

#ifndef BLE_SLOW_ADV_INTERVAL_MS
#define BLE_SLOW_ADV_INTERVAL_MS 2000
#endif

#ifndef BLE_ACTIVE_ADV_INTERVAL_MS
#define BLE_ACTIVE_ADV_INTERVAL_MS 500
#endif

#ifndef BLE_IDLE_DISCONNECT_MS
#define BLE_IDLE_DISCONNECT_MS 30000
#endif

#ifndef BATTERY_LOW_THRESHOLD_MV
#define BATTERY_LOW_THRESHOLD_MV 2600
#endif

#ifndef BATTERY_LOW_HYSTERESIS_MV
#define BATTERY_LOW_HYSTERESIS_MV 100
#endif

#ifndef BATTERY_SAMPLE_INTERVAL_MS
#define BATTERY_SAMPLE_INTERVAL_MS 60000
#endif

#ifndef BATTERY_LOW_ALERT_INTERVAL_MS
#define BATTERY_LOW_ALERT_INTERVAL_MS 60000
#endif

/* Slow blink while BLE data is being received (non-blocking work item).
 * 500 ms 翻转 = 约 1 Hz，视觉上为"慢闪"（与刷新完成的 120 ms 快闪区分）。
 */
#ifndef LED_RX_BLINK_INTERVAL_MS
#define LED_RX_BLINK_INTERVAL_MS 500
#endif

/* Power-on LED: on once for 1 s then off. */
#ifndef LED_BOOT_ONCE_ON_MS
#define LED_BOOT_ONCE_ON_MS 1000
#endif

/* Fast-blink pattern timing (refresh done: 3 blinks then off). */
#ifndef LED_FLASH_ON_MS
#define LED_FLASH_ON_MS 120
#endif

#ifndef LED_FLASH_OFF_MS
#define LED_FLASH_OFF_MS 120
#endif

#ifndef LED_FLASH_COUNT         
#define LED_FLASH_COUNT 3
#endif

#ifndef LED_LOW_SHORT_ON_MS       
#define LED_LOW_SHORT_ON_MS 100
#endif

#ifndef LED_LOW_SHORT_OFF_MS
#define LED_LOW_SHORT_OFF_MS 100
#endif

#ifndef LED_LOW_LONG_ON_MS
#define LED_LOW_LONG_ON_MS 500
#endif

#ifndef EPD_BUSY_TIMEOUT_MS
#define EPD_BUSY_TIMEOUT_MS 30000
#endif

#endif /* APP_CONFIG_H_ */

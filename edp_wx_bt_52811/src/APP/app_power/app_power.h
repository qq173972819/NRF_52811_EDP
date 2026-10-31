#ifndef __APP_POWER_H__
#define __APP_POWER_H__

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Initialize low-power features:
 *  - Configure unused GPIO pins to output low (eliminate leakage)
 *  - Start watchdog timer
 *  - Disable unused peripherals
 *  - Clear reset reason register
 */
void app_power_init(void);

/**
 * @brief Feed (kick) the watchdog timer. Call periodically.
 */
void app_power_feed_watchdog(void);

/**
 * @brief Check if this boot is a cold boot (power-on reset).
 *  - Reads RESETREAS register to distinguish:
 *    - Power-on / pin reset → true  (should scan for beacons)
 *    - Deep sleep (System OFF) wakeup → false (skip scanning)
 * @return true if cold boot (power-on reset), false if wakeup from deep sleep
 */
bool app_power_is_cold_boot(void);

/**
 * @brief Enter System OFF deep sleep with RTC timed wakeup.
 *  - Feeds watchdog before sleep
 *  - Configures RTC2 COMPARE0 for wakeup after ms milliseconds
 *  - Enters System OFF mode (~0.3µA)
 *  - RTC triggers reset to wake CPU after specified duration
 * @param ms  Sleep duration in milliseconds
 */
void app_power_enter_deep_sleep(uint32_t ms);

#endif /* __APP_POWER_H__ */

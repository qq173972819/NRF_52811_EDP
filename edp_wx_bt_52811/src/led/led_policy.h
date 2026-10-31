/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LED_LED_POLICY_H_
#define LED_LED_POLICY_H_

#include <stdbool.h>
#include <stdint.h>

enum led_mode {
	LED_MODE_OFF,
	LED_MODE_BOOT_ONCE,
	LED_MODE_CONNECTED,
	LED_MODE_RECEIVING,
	LED_MODE_REFRESH_FLASH,
	LED_MODE_LOW_BATTERY,
};

struct led_request {
	bool boot_once;
	bool receiving;
	bool refresh_flash;
	bool connected;
	bool battery_low;
};

struct led_low_pattern {
	uint8_t phase;
};

struct led_pattern_step {
	bool on;
	uint32_t delay_ms;
	bool round_complete;
};

enum led_mode led_policy_select(const struct led_request *request);
void led_low_pattern_start(struct led_low_pattern *pattern);
struct led_pattern_step led_low_pattern_next(struct led_low_pattern *pattern);
uint32_t led_deadline_remaining_ms(int64_t deadline_ms, int64_t now_ms);

#endif /* LED_LED_POLICY_H_ */

/* SPDX-License-Identifier: Apache-2.0 */

#include "led/led_policy.h"

#include <limits.h>

#include "app_config.h"

enum led_mode led_policy_select(const struct led_request *request)
{
	if (request->receiving) {
		return LED_MODE_RECEIVING;
	}
	if (request->refresh_flash) {
		return LED_MODE_REFRESH_FLASH;
	}
	if (request->boot_once) {
		return LED_MODE_BOOT_ONCE;
	}
	if (request->connected) {
		/* 只要链路还在就常亮：包括在传输/刷新过程中以及刷新完成之后。
		 * 屏幕刷新结束只会让 LED 快闪 3 下，闪完回到常亮；
		 * 只有蓝牙断开才熄灭。 */
		return LED_MODE_CONNECTED;
	}
	if (request->battery_low) {
		return LED_MODE_LOW_BATTERY;
	}
	return LED_MODE_OFF;
}

void led_low_pattern_start(struct led_low_pattern *pattern)
{
	pattern->phase = 0U;
}

struct led_pattern_step led_low_pattern_next(struct led_low_pattern *pattern)
{
	struct led_pattern_step step;

	if (pattern->phase < 6U) {
		step.on = (pattern->phase % 2U) == 0U;
		step.delay_ms = step.on ? LED_LOW_SHORT_ON_MS :
					  LED_LOW_SHORT_OFF_MS;
		step.round_complete = false;
		++pattern->phase;
		return step;
	}

	if (pattern->phase == 6U) {
		step.on = true;
		step.delay_ms = LED_LOW_LONG_ON_MS;
		step.round_complete = false;
		++pattern->phase;
		return step;
	}

	step.on = false;
	step.delay_ms = 0U;
	step.round_complete = true;
	pattern->phase = 0U;
	return step;
}

uint32_t led_deadline_remaining_ms(int64_t deadline_ms, int64_t now_ms)
{
	uint64_t remaining;

	if (deadline_ms <= now_ms) {
		return 0U;
	}

	remaining = (uint64_t)deadline_ms - (uint64_t)now_ms;
	return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

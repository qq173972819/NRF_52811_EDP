/* SPDX-License-Identifier: Apache-2.0 */

#include "led/app_led.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <zephyr/logging/log.h>

#include "app_config.h"
#include "led/led_policy.h"

LOG_MODULE_REGISTER(app_led, CONFIG_LOG_DEFAULT_LEVEL);

static const struct gpio_dt_spec status_led =
	GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

static const char *led_mode_name(enum led_mode mode)
{
	switch (mode) {
	case LED_MODE_BOOT_ONCE:
		return "BOOT_ONCE";
	case LED_MODE_CONNECTED:
		return "CONNECTED";
	case LED_MODE_RECEIVING:
		return "RECEIVING";
	case LED_MODE_REFRESH_FLASH:
		return "REFRESH_FLASH";
	case LED_MODE_LOW_BATTERY:
		return "LOW_BATTERY";
	case LED_MODE_OFF:
	default:
		return "OFF";
	}
}

static void led_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(led_work, led_work_handler);
static struct k_spinlock led_lock;
static struct led_low_pattern low_pattern;
static enum led_mode active_mode;
static bool led_initialized;
static bool connected_active;
static bool receiving_active;
static bool battery_low_active;
static bool refresh_flash_pending;
static bool boot_once_pending;
static bool mode_log_pending;
static enum led_mode mode_log_value;
static bool receiving_on;
static bool flash_phase_on;
static uint8_t flash_count;
static bool low_round_start_pending;
static bool low_alert_deferred;
static bool forcing_off;
static int64_t low_round_deadline_ms;

static int64_t led_deadline_after(int64_t now_ms)
{
	return now_ms > INT64_MAX - BATTERY_LOW_ALERT_INTERVAL_MS ?
		INT64_MAX : now_ms + BATTERY_LOW_ALERT_INTERVAL_MS;
}

static struct led_request led_build_request_locked(void)
{
	struct led_request request = {
		.boot_once = boot_once_pending,
		.receiving = receiving_active,
		.refresh_flash = refresh_flash_pending,
		.connected = connected_active,
		.battery_low = battery_low_active,
	};

	return request;
}

static void led_reset_mode_state(enum led_mode mode)
{
	receiving_on = false;
	flash_phase_on = false;
	flash_count = 0U;
	low_round_start_pending = false;
	low_alert_deferred = false;
	if (mode == LED_MODE_LOW_BATTERY) {
		led_low_pattern_start(&low_pattern);
		low_round_deadline_ms = led_deadline_after(k_uptime_get());
		low_round_start_pending = true;
	}
}

static bool led_select_mode_locked(void)
{
	struct led_request request = led_build_request_locked();
	const enum led_mode selected = led_policy_select(&request);

	if (selected == active_mode) {
		return false;
	}

	active_mode = selected;
	led_reset_mode_state(selected);
	mode_log_pending = true;
	mode_log_value = selected;
	return true;
}

/* 自续排的模式（接收慢闪/快闪/低电）不需要外部再踢一次；
 * 静态模式（熄灭/常亮）不会自续排，任何事件后都必须重跑一次工作项，
 * 否则 GPIO 电平会停留在旧状态（表现为"该熄灭时不熄灭"）。 */
static bool led_mode_needs_echo(enum led_mode mode)
{
	return mode == LED_MODE_OFF || mode == LED_MODE_CONNECTED;
}

static void led_schedule_after_event(bool mode_changed)
{
	if (!mode_changed && !led_mode_needs_echo(active_mode)) {
		return;
	}

	/* 静态模式在这里同步写出电平，不等工作项排队。
	 * 典型场景：慢闪正好停在"灭"的半个周期时传图结束，
	 * 若只重排工作项，LED 会残留熄灭状态，表现为"传完了灯却灭着"。
	 * 这里直接落电平，保证"蓝牙还在连接 = 常亮"。 */
	if (active_mode == LED_MODE_CONNECTED) {
		(void)gpio_pin_set_dt(&status_led, 1);
	} else if (active_mode == LED_MODE_OFF) {
		(void)gpio_pin_set_dt(&status_led, 0);
	}

	(void)k_work_reschedule(&led_work, K_NO_WAIT);
}

/* 模式变化日志（在锁外输出，便于定位"LED 为什么没灭"这类问题） */
static void led_flush_mode_log(void)
{
	if (!mode_log_pending) {
		return;
	}
	mode_log_pending = false;
	LOG_INF("LED mode -> %s", led_mode_name(mode_log_value));
}

static void led_work_handler(struct k_work *work)
{
	k_spinlock_key_t key;
	bool output_on = false;
	uint32_t delay_ms = 0U;
	bool reschedule = false;
	enum led_mode handled_mode;

	(void)work;
	key = k_spin_lock(&led_lock);
	if (!led_initialized || forcing_off) {
		k_spin_unlock(&led_lock, key);
		return;
	}

	handled_mode = active_mode;

	switch (active_mode) {
	case LED_MODE_BOOT_ONCE:
		if (flash_count == 0U) {
			output_on = true;
			delay_ms = LED_BOOT_ONCE_ON_MS;
			reschedule = true;
		} else {
			/* 上电脉冲结束：转到新模式并立即写出该模式的电平
			 * （若此刻已被连上，直接落到常亮，不额外"闪一下"） */
			boot_once_pending = false;
			(void)led_select_mode_locked();
			output_on = (active_mode == LED_MODE_CONNECTED);
			delay_ms = 0U;
			reschedule = true;
		}
		++flash_count;
		break;

	case LED_MODE_REFRESH_FLASH:
		flash_phase_on = !flash_phase_on;
		output_on = flash_phase_on;
		delay_ms = output_on ? LED_FLASH_ON_MS : LED_FLASH_OFF_MS;
		if (output_on) {
			++flash_count;
		}
		if (!output_on && flash_count >= LED_FLASH_COUNT) {
			/* 刷新完成：快闪 3 下后回到常态。蓝牙仍在线就恢复常亮，
			 * 已断开则熄灭。这里直接按新模式写出电平，避免中间出现
			 * 一次多余的"灭一下"再被下一次工作项纠回来。 */
			refresh_flash_pending = false;
			flash_phase_on = false;
			flash_count = 0U;
			(void)led_select_mode_locked();
			output_on = (active_mode == LED_MODE_CONNECTED);
			delay_ms = 0U;
			reschedule = true;
		} else {
			reschedule = true;
		}
		break;

	case LED_MODE_RECEIVING:
		receiving_on = !receiving_on;
		output_on = receiving_on;
		delay_ms = LED_RX_BLINK_INTERVAL_MS;
		reschedule = true;
		break;

	case LED_MODE_CONNECTED:
		output_on = true;
		break;

	case LED_MODE_LOW_BATTERY: {
		const int64_t now_ms = k_uptime_get();
		struct led_pattern_step step;
		uint32_t step_delay_ms;

		if (low_round_start_pending) {
			low_round_deadline_ms = led_deadline_after(now_ms);
			low_round_start_pending = false;
		}
		step = led_low_pattern_next(&low_pattern);
		step_delay_ms = step.delay_ms;
		if (step.round_complete) {
			step_delay_ms = led_deadline_remaining_ms(
				low_round_deadline_ms, now_ms);
			low_round_start_pending = true;
		}

		output_on = step.on;
		delay_ms = step_delay_ms;
		reschedule = true;
		break;
	}

	case LED_MODE_OFF:
	default:
		output_on = false;
		break;
	}
	k_spin_unlock(&led_lock, key);

	(void)gpio_pin_set_dt(&status_led, output_on ? 1 : 0);
	led_flush_mode_log();
	if (reschedule) {
		enum led_mode now_mode;

		/* 本次处理期间若发生过外部事件（例如传图结束把慢闪切成常亮），
		 * 这里算出的延时（慢闪/快闪的一个半周期）已经失效，
		 * 必须改成立即重跑，否则新选中模式的电平会被推迟到下一个
		 * 周期才写入，表现就是"传完图灯还黑着"。 */
		key = k_spin_lock(&led_lock);
		now_mode = active_mode;
		k_spin_unlock(&led_lock, key);

		(void)k_work_reschedule(&led_work,
			now_mode == handled_mode ? K_MSEC(delay_ms) : K_NO_WAIT);
	}
}

int app_led_init(void)
{
	k_spinlock_key_t key;
	int result;

	key = k_spin_lock(&led_lock);
	if (led_initialized) {
		k_spin_unlock(&led_lock, key);
		return 0;
	}
	k_spin_unlock(&led_lock, key);

	if (!device_is_ready(status_led.port)) {
		return -ENODEV;
	}

	result = gpio_pin_configure_dt(&status_led, GPIO_OUTPUT_INACTIVE);
	if (result < 0) {
		return result;
	}

	key = k_spin_lock(&led_lock);
	connected_active = false;
	receiving_active = false;
	battery_low_active = false;
	refresh_flash_pending = false;
	receiving_on = false;
	flash_phase_on = false;
	flash_count = 0U;
	low_round_start_pending = false;
	low_alert_deferred = false;
	forcing_off = false;
	boot_once_pending = true;
	active_mode = LED_MODE_BOOT_ONCE;
	led_initialized = true;
	k_spin_unlock(&led_lock, key);

	LOG_INF("LED mode -> %s", led_mode_name(LED_MODE_BOOT_ONCE));

	(void)k_work_reschedule(&led_work, K_NO_WAIT);
	return 0;
}

void app_led_set_connected(bool connected)
{
	k_spinlock_key_t key = k_spin_lock(&led_lock);
	bool mode_changed = false;

	if (led_initialized && !forcing_off) {
		const bool was_connected = connected_active;

		connected_active = connected;
		if (was_connected && !connected) {
			/* Disconnect: back to idle OFF (or low battery). */
			refresh_flash_pending = false;
			receiving_active = false;
			boot_once_pending = false;
		}
		mode_changed = led_select_mode_locked();
	}
	k_spin_unlock(&led_lock, key);

	led_flush_mode_log();
	led_schedule_after_event(mode_changed);
}

void app_led_set_receiving(bool receiving)
{
	k_spinlock_key_t key = k_spin_lock(&led_lock);
	bool mode_changed = false;

	if (led_initialized && !forcing_off) {
		receiving_active = receiving;
		mode_changed = led_select_mode_locked();
	}
	k_spin_unlock(&led_lock, key);

	led_flush_mode_log();
	led_schedule_after_event(mode_changed);
}

void app_led_refresh_flash(void)
{
	k_spinlock_key_t key = k_spin_lock(&led_lock);
	bool mode_changed = false;

	if (led_initialized && !forcing_off) {
		refresh_flash_pending = true;
		mode_changed = led_select_mode_locked();
	}
	k_spin_unlock(&led_lock, key);

	led_flush_mode_log();
	led_schedule_after_event(mode_changed);
}

void app_led_set_battery_low(bool battery_low)
{
	k_spinlock_key_t key = k_spin_lock(&led_lock);
	bool mode_changed = false;

	if (led_initialized && !forcing_off) {
		battery_low_active = battery_low;
		mode_changed = led_select_mode_locked();
	}
	k_spin_unlock(&led_lock, key);

	led_flush_mode_log();
	led_schedule_after_event(mode_changed);
}

void app_led_force_off(void)
{
	struct k_work_sync sync;
	k_spinlock_key_t key = k_spin_lock(&led_lock);
	bool should_force = false;

	if (led_initialized && !forcing_off) {
		forcing_off = true;
		should_force = true;
		connected_active = false;
		receiving_active = false;
		battery_low_active = false;
		refresh_flash_pending = false;
		boot_once_pending = false;
		receiving_on = false;
		flash_phase_on = false;
		flash_count = 0U;
		low_round_start_pending = false;
		low_alert_deferred = false;
		active_mode = LED_MODE_OFF;
	}
	k_spin_unlock(&led_lock, key);

	if (!should_force) {
		return;
	}

	(void)k_work_cancel_delayable_sync(&led_work, &sync);
	(void)gpio_pin_set_dt(&status_led, 0);

	key = k_spin_lock(&led_lock);
	connected_active = false;
	receiving_active = false;
	battery_low_active = false;
	refresh_flash_pending = false;
	boot_once_pending = false;
	receiving_on = false;
	flash_phase_on = false;
	flash_count = 0U;
	low_round_start_pending = false;
	low_alert_deferred = false;
	active_mode = LED_MODE_OFF;
	forcing_off = false;
	k_spin_unlock(&led_lock, key);
}

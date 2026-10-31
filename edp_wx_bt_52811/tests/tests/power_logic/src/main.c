/* SPDX-License-Identifier: Apache-2.0 */

#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>

#include <zephyr/ztest.h>

#include "app_config.h"
#include "battery/battery_state.h"
#include "battery/app_battery.h"
#include "led/app_led.h"
#include "led/led_policy.h"

#ifdef HOST_TEST
#include "../host/adapter_shim.h"
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

extern struct device host_adc_device;
extern struct device host_gpio_device;
#endif

ZTEST(battery_state, test_exact_threshold_boundaries)
{
	zassert_false(battery_low_next(false, 2600));
	zassert_true(battery_low_next(false, 2599));
	zassert_true(battery_low_next(true, 2699));
	zassert_false(battery_low_next(true, 2700));
}

ZTEST(battery_state, test_macro_relative_boundaries)
{
	zassert_false(battery_low_next(false, BATTERY_LOW_THRESHOLD_MV));
	zassert_true(battery_low_next(false, BATTERY_LOW_THRESHOLD_MV - 1));
	zassert_true(battery_low_next(true, BATTERY_LOW_THRESHOLD_MV +
					 BATTERY_LOW_HYSTERESIS_MV - 1));
	zassert_false(battery_low_next(true, BATTERY_LOW_THRESHOLD_MV +
					  BATTERY_LOW_HYSTERESIS_MV));
}

ZTEST(led_policy, test_receiving_has_priority_over_low_battery)
{
	zassert_equal(led_policy_select(false, false), LED_MODE_OFF);
	zassert_equal(led_policy_select(false, true), LED_MODE_LOW_BATTERY);
	zassert_equal(led_policy_select(true, true), LED_MODE_RECEIVING);
}

ZTEST(led_policy, test_low_pattern_exact_steps)
{
	static const bool expected_on[] = {
		true, false, true, false, true, false, true, false,
	};
	static const uint32_t expected_delay[] = {
		LED_LOW_SHORT_ON_MS,
		LED_LOW_SHORT_OFF_MS,
		LED_LOW_SHORT_ON_MS,
		LED_LOW_SHORT_OFF_MS,
		LED_LOW_SHORT_ON_MS,
		LED_LOW_SHORT_OFF_MS,
		LED_LOW_LONG_ON_MS,
		0U,
	};
	static const bool expected_complete[] = {
		false, false, false, false, false, false, false, true,
	};
	struct led_low_pattern pattern;

	led_low_pattern_start(&pattern);
	for (size_t i = 0; i < sizeof(expected_on) / sizeof(expected_on[0]); ++i) {
		const struct led_pattern_step step = led_low_pattern_next(&pattern);

		zassert_equal(step.on, expected_on[i], "step %u", (unsigned int)i);
		zassert_equal(step.delay_ms, expected_delay[i], "step %u",
			      (unsigned int)i);
		zassert_equal(step.round_complete, expected_complete[i], "step %u",
			      (unsigned int)i);
	}

	{
		const struct led_pattern_step restarted = led_low_pattern_next(&pattern);

		zassert_true(restarted.on);
		zassert_equal(restarted.delay_ms, LED_LOW_SHORT_ON_MS);
	}
}

ZTEST(led_policy, test_short_alert_interval_never_underflows)
{
	struct led_low_pattern pattern;
	struct led_pattern_step step = { 0 };

	led_low_pattern_start(&pattern);
	for (int i = 0; i < 8; ++i) {
		step = led_low_pattern_next(&pattern);
	}

	zassert_false(step.on);
	zassert_true(step.round_complete);
	zassert_equal(led_deadline_remaining_ms(1, 1100), 0U);
	zassert_true(led_low_pattern_next(&pattern).on);
}

ZTEST(led_policy, test_deadline_remaining_clamps_safely)
{
	zassert_equal(led_deadline_remaining_ms(60000, 1275), 58725U);
	zassert_equal(led_deadline_remaining_ms(100, 100), 0U);
	zassert_equal(led_deadline_remaining_ms(99, 100), 0U);
	zassert_equal(led_deadline_remaining_ms(INT64_MAX, 0), UINT32_MAX);
}

#ifdef HOST_TEST
ZTEST(app_led, test_adapter_lifecycle_and_mode_handoffs)
{
	host_power_adapter_reset();
	host_gpio_device.ready = false;
	zassert_equal(app_led_init(), -ENODEV);
	zassert_equal(host_power_adapter.gpio_configure_count, 0);

	host_gpio_device.ready = true;
	host_power_adapter.gpio_configure_result = -EIO;
	zassert_equal(app_led_init(), -EIO);
	host_power_adapter.gpio_configure_result = 0;
	zassert_equal(app_led_init(), 0);
	zassert_equal(host_power_adapter.gpio_configure_flags, GPIO_OUTPUT_INACTIVE);
	zassert_equal(host_power_adapter.gpio_node_id, HOST_DT_ALIAS_LED0);
	zassert_equal(host_power_adapter.gpio_prop_id, HOST_DT_PROP_GPIOS);

	app_led_set_battery_low(true);
	zassert_equal(host_pending_work_with_delay(0U), 1);
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.gpio_values[0], 1);
	zassert_equal(host_pending_work_with_delay(LED_LOW_SHORT_ON_MS), 1);
	zassert_true(host_run_work_with_delay(LED_LOW_SHORT_ON_MS));
	zassert_equal(host_power_adapter.gpio_values[1], 0);

	app_led_set_receiving(true);
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.gpio_values[2], 1);
	zassert_equal(host_pending_work_with_delay(LED_RX_BLINK_INTERVAL_MS), 1);
	zassert_true(host_run_work_with_delay(LED_RX_BLINK_INTERVAL_MS));
	zassert_equal(host_power_adapter.gpio_values[3], 0);

	app_led_set_receiving(false);
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.gpio_values[4], 0);
	zassert_equal(host_pending_work_with_delay(BATTERY_LOW_ALERT_INTERVAL_MS), 1);
	zassert_true(host_run_work_with_delay(BATTERY_LOW_ALERT_INTERVAL_MS));
	zassert_equal(host_power_adapter.gpio_values[5], 1);
	zassert_equal(host_pending_work_with_delay(LED_LOW_SHORT_ON_MS), 1);

	app_led_force_off();
	zassert_equal(host_power_adapter.gpio_values[6], 0);
	zassert_equal(host_pending_work_with_delay(LED_LOW_SHORT_ON_MS), 0);
	zassert_equal(host_pending_work_with_delay(LED_RX_BLINK_INTERVAL_MS), 0);

	app_led_set_battery_low(true);
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.gpio_values[7], 1);
	app_led_force_off();
	zassert_true(host_power_adapter.spinlock_acquire_count > 0);
	zassert_equal(host_power_adapter.spinlock_depth, 0);
	zassert_equal(host_power_adapter.spinlock_max_depth, 1);
	zassert_equal(host_power_adapter.spinlock_violation_count, 0);
}

ZTEST(app_led, test_low_battery_deadline_accounts_for_late_handlers)
{
	app_led_force_off();
	host_power_adapter_reset();
	app_led_set_battery_low(true);
	zassert_true(host_run_work_with_delay(0U));

	for (int i = 0; i < 6; ++i) {
		host_advance_time(LED_LOW_SHORT_ON_MS + 25U);
		zassert_true(host_run_work_with_delay(0U));
	}
	host_advance_time(LED_LOW_LONG_ON_MS + 25U);
	zassert_true(host_run_work_with_delay(0U));

	zassert_equal(host_monotonic_ms(), 1275);
	zassert_equal(host_power_adapter.gpio_values[7], 0);
	zassert_equal(host_pending_work_with_delay(58725U), 1);
	zassert_equal(host_power_adapter.spinlock_depth, 0);
	zassert_equal(host_power_adapter.spinlock_max_depth, 1);
	zassert_equal(host_power_adapter.spinlock_violation_count, 0);
	app_led_force_off();
}

ZTEST(app_led, test_force_off_ignores_setter_during_cancel)
{
	app_led_force_off();
	host_power_adapter_reset();
	app_led_set_battery_low(true);
	zassert_equal(host_pending_work_with_delay(0U), 1);

	host_power_adapter.setter_during_led_cancel = true;
	app_led_force_off();
	zassert_equal(host_pending_work_with_delay(0U), 0);
	host_power_adapter.setter_during_led_cancel = false;

	app_led_set_battery_low(true);
	zassert_equal(host_pending_work_with_delay(0U), 1);
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.gpio_values[1], 1);
	app_led_force_off();
}

ZTEST(app_battery, test_adapter_lifecycle_sampling_and_suspend)
{
	host_power_adapter_reset();
	zassert_equal(app_battery_last_mv(), -ENODATA);
	zassert_false(app_battery_is_low());

	host_adc_device.ready = false;
	zassert_equal(app_battery_init(), -ENODEV);
	host_adc_device.ready = true;
	host_power_adapter.adc_setup_result = -EIO;
	zassert_equal(app_battery_init(), -EIO);
	host_power_adapter.adc_setup_result = 0;
	zassert_equal(app_battery_init(), 0);
	zassert_equal(host_pending_work_with_delay(0U), 1);
	zassert_equal(host_power_adapter.adc_node_id, HOST_DT_PATH_ZEPHYR_USER);

	host_adc_block_next_read();
	zassert_true(host_start_work_thread_with_delay(0U));
	zassert_true(host_wait_adc_read_blocked());
	zassert_true(host_start_battery_suspend_thread());
	zassert_true(host_wait_cancel_sync_waiting());
	host_release_adc_read();
	zassert_true(host_join_concurrency_threads());
	zassert_equal(app_battery_last_mv(), -ENODATA);
	zassert_equal(host_pending_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS), 0);
	zassert_equal(host_power_adapter.work_cancel_while_running_count, 1);
	zassert_equal(host_power_adapter.work_self_cancel_violation_count, 0);
	zassert_equal(app_battery_init(), 0);
	zassert_equal(host_pending_work_with_delay(0U), 1);

	host_power_adapter.adc_raw_value = 1774;
	host_power_adapter.adc_converted_mv = 1299;
	zassert_true(host_run_work_with_delay(0U));
	zassert_equal(host_power_adapter.adc_resolution, 12);
	zassert_equal(host_power_adapter.adc_oversampling, 8);
	zassert_equal(app_battery_last_mv(), 2598);
	zassert_true(app_battery_is_low());
	zassert_equal(host_pending_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS), 1);

	host_power_adapter.adc_read_result = -EIO;
	zassert_true(host_run_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS));
	zassert_equal(app_battery_last_mv(), 2598);
	zassert_true(app_battery_is_low());
	zassert_equal(host_pending_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS), 1);

	host_power_adapter.adc_read_result = 0;
	host_power_adapter.adc_convert_result = -ENOTSUP;
	zassert_true(host_run_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS));
	zassert_equal(app_battery_last_mv(), 2598);
	zassert_true(app_battery_is_low());

	host_power_adapter.adc_convert_result = 0;
	host_power_adapter.adc_converted_mv = 1350;
	zassert_true(host_run_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS));
	zassert_equal(app_battery_last_mv(), 2700);
	zassert_false(app_battery_is_low());

	app_battery_suspend();
	zassert_equal(host_pending_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS), 0);
	zassert_false(host_run_work_with_delay(BATTERY_SAMPLE_INTERVAL_MS));
	zassert_true(host_power_adapter.spinlock_acquire_count > 0);
	zassert_equal(host_power_adapter.spinlock_depth, 0);
	zassert_equal(host_power_adapter.spinlock_max_depth, 1);
	zassert_equal(host_power_adapter.spinlock_violation_count, 0);
	app_led_force_off();
}
#endif

ZTEST_SUITE(battery_state, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(led_policy, NULL, NULL, NULL, NULL, NULL);
#ifdef HOST_TEST
ZTEST_SUITE(app_led, NULL, NULL, NULL, NULL, NULL);
ZTEST_SUITE(app_battery, NULL, NULL, NULL, NULL, NULL);
#endif

/* SPDX-License-Identifier: Apache-2.0 */

#include "battery/app_battery.h"

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/kernel.h>

#include "app_config.h"
#include "battery/battery_state.h"
#include "led/app_led.h"

static const struct adc_dt_spec battery_adc =
	ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static void battery_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(battery_work, battery_work_handler);
static struct k_spinlock battery_lock;
static bool battery_initialized;
static bool battery_suspended;
static bool battery_low;
static int32_t battery_last_mv = -ENODATA;

static bool battery_sampling_enabled(void)
{
	k_spinlock_key_t key = k_spin_lock(&battery_lock);
	const bool enabled = battery_initialized && !battery_suspended;

	k_spin_unlock(&battery_lock, key);
	return enabled;
}

static void battery_schedule_next(void)
{
	if (battery_sampling_enabled()) {
		(void)k_work_reschedule(&battery_work,
					K_MSEC(BATTERY_SAMPLE_INTERVAL_MS));
	}
}

static void battery_work_handler(struct k_work *work)
{
	struct adc_sequence sequence = { 0 };
	int16_t raw_sample;
	int32_t measured_mv;
	bool next_low;
	int result;

	(void)work;
	if (!battery_sampling_enabled()) {
		return;
	}

	sequence.buffer = &raw_sample;
	sequence.buffer_size = sizeof(raw_sample);
	result = adc_sequence_init_dt(&battery_adc, &sequence);
	if (result < 0) {
		battery_schedule_next();
		return;
	}

	result = adc_read(battery_adc.dev, &sequence);
	if (result < 0) {
		battery_schedule_next();
		return;
	}

	measured_mv = raw_sample;
	result = adc_raw_to_millivolts_dt(&battery_adc, &measured_mv);
	if (result < 0) {
		battery_schedule_next();
		return;
	}
	measured_mv *= 2;

	{
		k_spinlock_key_t key = k_spin_lock(&battery_lock);

		if (!battery_initialized || battery_suspended) {
			k_spin_unlock(&battery_lock, key);
			return;
		}
		next_low = battery_low_next(battery_low, measured_mv);
		battery_last_mv = measured_mv;
		battery_low = next_low;
		k_spin_unlock(&battery_lock, key);
	}

	app_led_set_battery_low(next_low);
	battery_schedule_next();
}

int app_battery_init(void)
{
	k_spinlock_key_t key;
	int result;

	key = k_spin_lock(&battery_lock);
	if (battery_initialized) {
		k_spin_unlock(&battery_lock, key);
		return 0;
	}
	k_spin_unlock(&battery_lock, key);

	if (!device_is_ready(battery_adc.dev)) {
		return -ENODEV;
	}

	result = adc_channel_setup_dt(&battery_adc);
	if (result < 0) {
		return result;
	}

	key = k_spin_lock(&battery_lock);
	battery_last_mv = -ENODATA;
	battery_low = false;
	battery_suspended = false;
	battery_initialized = true;
	k_spin_unlock(&battery_lock, key);

	result = k_work_reschedule(&battery_work, K_NO_WAIT);
	if (result < 0) {
		key = k_spin_lock(&battery_lock);
		battery_initialized = false;
		k_spin_unlock(&battery_lock, key);
		return result;
	}

	return 0;
}

bool app_battery_is_low(void)
{
	k_spinlock_key_t key = k_spin_lock(&battery_lock);
	const bool low = battery_low;

	k_spin_unlock(&battery_lock, key);
	return low;
}

int32_t app_battery_last_mv(void)
{
	k_spinlock_key_t key = k_spin_lock(&battery_lock);
	const int32_t last_mv = battery_last_mv;

	k_spin_unlock(&battery_lock, key);
	return last_mv;
}

void app_battery_suspend(void)
{
	struct k_work_sync sync;
	k_spinlock_key_t key = k_spin_lock(&battery_lock);
	const bool initialized = battery_initialized;

	battery_suspended = true;
	battery_initialized = false;
	k_spin_unlock(&battery_lock, key);

	if (initialized) {
		(void)k_work_cancel_delayable_sync(&battery_work, &sync);
	}
}

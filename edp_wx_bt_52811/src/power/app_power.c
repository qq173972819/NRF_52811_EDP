/* SPDX-License-Identifier: Apache-2.0 */

#include "power/app_power.h"

#include <errno.h>
#include <stdint.h>

#include <zephyr/kernel.h>

#include "app_config.h"
#include "ble/app_ble_epd.h"

static void idle_disconnect_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(idle_disconnect_work,
			      idle_disconnect_work_handler);
static struct k_spinlock power_lock;
static bool power_initialized;

static bool power_is_active(void)
{
	k_spinlock_key_t key = k_spin_lock(&power_lock);
	const bool active = power_initialized;

	k_spin_unlock(&power_lock, key);
	return active;
}

static void idle_disconnect_work_handler(struct k_work *work)
{
	(void)work;
	if (power_is_active()) {
		app_ble_epd_disconnect();
	}
}

int app_power_init(void)
{
	k_spinlock_key_t key;
	uint32_t interval_ms = BLE_SLOW_ADV_INTERVAL_MS;
	int result;

	key = k_spin_lock(&power_lock);
	if (power_initialized) {
		k_spin_unlock(&power_lock, key);
		return 0;
	}
	k_spin_unlock(&power_lock, key);

	/* Always-resident: just start slow advertising; never enter
	 * System OFF (this PCB revision has no wake button).
	 */
	result = app_ble_epd_start_advertising(interval_ms);
	if (result < 0) {
		return result;
	}

	key = k_spin_lock(&power_lock);
	power_initialized = true;
	k_spin_unlock(&power_lock, key);
	return 0;
}

static void handle_transfer_end(void)
{
	if (power_is_active()) {
		(void)k_work_reschedule(&idle_disconnect_work,
					K_MSEC(BLE_IDLE_DISCONNECT_MS));
	}
}

void app_power_transfer_finished(void)
{
	handle_transfer_end();
}

void app_power_transfer_failed(void)
{
	handle_transfer_end();
}

bool app_power_shutdown_pending(void)
{
	return false;
}

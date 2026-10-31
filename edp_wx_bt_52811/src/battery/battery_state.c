/* SPDX-License-Identifier: Apache-2.0 */

#include "battery/battery_state.h"

#include "app_config.h"

bool battery_low_next(bool was_low, int32_t battery_mv)
{
	if (was_low) {
		return battery_mv < BATTERY_LOW_THRESHOLD_MV +
				    BATTERY_LOW_HYSTERESIS_MV;
	}

	return battery_mv < BATTERY_LOW_THRESHOLD_MV;
}

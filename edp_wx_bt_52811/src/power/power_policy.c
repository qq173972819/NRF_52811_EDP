/* SPDX-License-Identifier: Apache-2.0 */

#include "power/power_policy.h"

enum power_action power_timeout_action(int mode, bool transfer_busy,
				       bool wake_pin_inactive)
{
	(void)mode;
	(void)transfer_busy;
	(void)wake_pin_inactive;
	return POWER_KEEP_ADVERTISING;
}

/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_POWER_POLICY_H_
#define POWER_POWER_POLICY_H_

#include <stdbool.h>

/* Always-resident power policy: this PCB revision has no wake button,
 * so System OFF is never used and the device keeps advertising.
 */
enum power_action {
	POWER_KEEP_ADVERTISING,
};

enum power_action power_timeout_action(int mode, bool transfer_busy,
				       bool wake_pin_inactive);

#endif /* POWER_POWER_POLICY_H_ */

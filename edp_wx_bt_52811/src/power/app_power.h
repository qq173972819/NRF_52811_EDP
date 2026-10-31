/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_APP_POWER_H_
#define POWER_APP_POWER_H_

#include <stdbool.h>

int app_power_init(void);
void app_power_transfer_finished(void);
void app_power_transfer_failed(void);
bool app_power_shutdown_pending(void);

#endif /* POWER_APP_POWER_H_ */

/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BATTERY_APP_BATTERY_H_
#define BATTERY_APP_BATTERY_H_

#include <stdbool.h>
#include <stdint.h>

/* Initializes ADC and queues the first sample; returns setup/queue errors. */
int app_battery_init(void);
bool app_battery_is_low(void);
/* Returns -ENODATA until the first sample and conversion both succeed. */
int32_t app_battery_last_mv(void);
void app_battery_suspend(void);

#endif /* BATTERY_APP_BATTERY_H_ */

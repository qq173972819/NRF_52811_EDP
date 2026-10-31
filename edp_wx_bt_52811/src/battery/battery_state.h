/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BATTERY_BATTERY_STATE_H_
#define BATTERY_BATTERY_STATE_H_

#include <stdbool.h>
#include <stdint.h>

bool battery_low_next(bool was_low, int32_t battery_mv);

#endif /* BATTERY_BATTERY_STATE_H_ */

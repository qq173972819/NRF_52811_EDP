/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ZEPHYR_DEVICE_H_
#define POWER_LOGIC_HOST_ZEPHYR_DEVICE_H_

#include <stdbool.h>

struct device {
	bool ready;
};

static inline bool device_is_ready(const struct device *device)
{
	return device != NULL && device->ready;
}

#endif /* POWER_LOGIC_HOST_ZEPHYR_DEVICE_H_ */

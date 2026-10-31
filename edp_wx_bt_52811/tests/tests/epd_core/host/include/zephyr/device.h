/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HOST_ZEPHYR_DEVICE_H_
#define HOST_ZEPHYR_DEVICE_H_

#include <stdbool.h>
#include <stddef.h>

struct device {
	bool ready;
};

static inline bool device_is_ready(const struct device *device)
{
	return device != NULL && device->ready;
}

#endif /* HOST_ZEPHYR_DEVICE_H_ */

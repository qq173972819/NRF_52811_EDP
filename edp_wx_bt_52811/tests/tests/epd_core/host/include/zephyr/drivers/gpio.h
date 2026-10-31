/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HOST_ZEPHYR_DRIVERS_GPIO_H_
#define HOST_ZEPHYR_DRIVERS_GPIO_H_

#include <zephyr/device.h>

extern struct device host_gpio_device;

struct gpio_dt_spec {
	const struct device *port;
	int id;
};

#define HOST_GPIO_ID_dc_gpios 1
#define HOST_GPIO_ID_reset_gpios 2
#define HOST_GPIO_ID_busy_gpios 3
#define GPIO_DT_SPEC_GET(node, property)                                         \
	{ &host_gpio_device, HOST_GPIO_ID_##property }
#define GPIO_OUTPUT_INACTIVE 1
#define GPIO_INPUT 2

int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value);
int gpio_pin_get_dt(const struct gpio_dt_spec *spec);
int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags);

#endif /* HOST_ZEPHYR_DRIVERS_GPIO_H_ */

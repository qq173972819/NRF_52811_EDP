/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ZEPHYR_DRIVERS_GPIO_H_
#define POWER_LOGIC_HOST_ZEPHYR_DRIVERS_GPIO_H_

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>

#define GPIO_OUTPUT_INACTIVE 0x01

struct gpio_dt_spec {
	const struct device *port;
	uint32_t pin;
	uint32_t node_id;
	uint32_t prop_id;
};

extern struct device host_gpio_device;

#define GPIO_DT_SPEC_GET(node_id, prop) GPIO_DT_SPEC_GET_RESOLVE(node_id, prop)
#define GPIO_DT_SPEC_GET_RESOLVE(node_id, prop)                                  \
	GPIO_DT_SPEC_GET_KNOWN(node_id, prop)
#define GPIO_DT_SPEC_GET_KNOWN(node_id, prop)                                    \
	GPIO_DT_SPEC_GET_##node_id##_##prop
#define GPIO_DT_SPEC_GET_202U_gpios                                              \
	{ &host_gpio_device, 11U, HOST_DT_ALIAS_LED0, HOST_DT_PROP_GPIOS }

int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags);
int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value);

#endif /* POWER_LOGIC_HOST_ZEPHYR_DRIVERS_GPIO_H_ */

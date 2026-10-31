/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include <errno.h>
#include <string.h>

#include "adapter_shim.h"

struct device host_gpio_device = { true };
struct device host_spi_device = { true };
struct host_adapter_state host_adapter;

static int dc_value;

void host_adapter_reset(void)
{
	memset(&host_adapter, 0, sizeof(host_adapter));
	host_gpio_device.ready = true;
	host_spi_device.ready = true;
	dc_value = 0;
}

void host_adapter_clear_events(void)
{
	host_adapter.configure_count = 0;
	host_adapter.reset_set_count = 0;
	host_adapter.busy_get_count = 0;
	host_adapter.sleep_count = 0;
	host_adapter.sleep_total_ms = 0;
	host_adapter.spi_count = 0;
	host_adapter.fail_spi_at = 0;
}

int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	(void)spec;
	if (spec->id == HOST_GPIO_ID_dc_gpios) {
		dc_value = value;
	} else if (spec->id == HOST_GPIO_ID_reset_gpios &&
		   host_adapter.reset_set_count < 2) {
		host_adapter.reset_values[host_adapter.reset_set_count++] = value;
	}
	return 0;
}

int gpio_pin_get_dt(const struct gpio_dt_spec *spec)
{
	(void)spec;
	++host_adapter.busy_get_count;
	if (host_adapter.busy_error < 0 &&
	    (host_adapter.fail_busy_at == 0 ||
	     host_adapter.fail_busy_at == host_adapter.busy_get_count)) {
		return host_adapter.busy_error;
	}
	if (host_adapter.busy_reads_remaining > 0) {
		--host_adapter.busy_reads_remaining;
		return 1;
	}
	return 0;
}

int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags)
{
	(void)spec;
	(void)flags;
	++host_adapter.configure_count;
	return 0;
}

int spi_write_dt(const struct spi_dt_spec *spec,
		 const struct spi_buf_set *buffers)
{
	struct host_spi_event *event;

	(void)spec;
	if (host_adapter.spi_count >= 32) {
		return -ENOMEM;
	}
	event = &host_adapter.spi_events[host_adapter.spi_count++];
	event->dc = dc_value;
	event->length = buffers->buffers[0].len;
	event->first_byte = ((const uint8_t *)buffers->buffers[0].buf)[0];
	if (host_adapter.fail_spi_at == host_adapter.spi_count) {
		return -EIO;
	}
	return 0;
}

void k_msleep(int32_t delay_ms)
{
	++host_adapter.sleep_count;
	host_adapter.sleep_total_ms += delay_ms;
}

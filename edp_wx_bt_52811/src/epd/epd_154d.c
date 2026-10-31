/* SPDX-License-Identifier: Apache-2.0 */

#include "epd_154d.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>

#include "epd_154d_core.h"

#define EPD_NODE DT_ALIAS(epd0)
#define EPD_BUSY_POLL_MS 5

/* Zephyr 4.3 warns for the required explicit zero-delay form. */
#ifdef SPI_DEPRECATE_DELAY_WARN
#undef SPI_DEPRECATE_DELAY_WARN
#define SPI_DEPRECATE_DELAY_WARN
#endif

static const struct spi_dt_spec epd_spi =
	SPI_DT_SPEC_GET(EPD_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB, 0);
static const struct gpio_dt_spec epd_dc = GPIO_DT_SPEC_GET(EPD_NODE, dc_gpios);
static const struct gpio_dt_spec epd_reset =
	GPIO_DT_SPEC_GET(EPD_NODE, reset_gpios);
static const struct gpio_dt_spec epd_busy = GPIO_DT_SPEC_GET(EPD_NODE, busy_gpios);

static struct epd_154d_core epd_core;
static bool epd_core_available;
static bool epd_initialized;
static bool epd_main_owns; /* 屏幕总线被主线程占用（上电自检）时为 true */

static int bus_reset(bool asserted, void *context)
{
	(void)context;
	return gpio_pin_set_dt(&epd_reset, asserted);
}

static int bus_write_command(uint8_t command, void *context)
{
	struct spi_buf buffer = {
		.buf = &command,
		.len = 1,
	};
	const struct spi_buf_set buffers = {
		.buffers = &buffer,
		.count = 1,
	};
	int result;

	(void)context;
	result = gpio_pin_set_dt(&epd_dc, 0);
	if (result < 0) {
		return result;
	}

	return spi_write_dt(&epd_spi, &buffers);
}

static int bus_write_data(const uint8_t *data, size_t length, void *context)
{
	struct spi_buf buffer = {
		.buf = (void *)data,
		.len = length,
	};
	const struct spi_buf_set buffers = {
		.buffers = &buffer,
		.count = 1,
	};
	int result;

	(void)context;
	result = gpio_pin_set_dt(&epd_dc, 1);
	if (result < 0) {
		return result;
	}

	return spi_write_dt(&epd_spi, &buffers);
}

static int bus_wait_ready(int32_t timeout_ms, void *context)
{
	int32_t elapsed_ms = 0;

	(void)context;
	for (;;) {
		/* Use raw level semantics, matching the pre-regression driver:
		 * GDEM0154F51H BUSY is active-low (raw 0 == busy, raw 1 == idle).
		 * Reading the raw pin avoids any dependency on the dts polarity
		 * flag and cannot mis-classify an idle panel as busy.
		 */
		const int busy = gpio_pin_get_raw(epd_busy.port, epd_busy.pin);

		if (busy < 0) {
			return busy;
		}
		if (busy != 0) {
			return 0;
		}
		if (elapsed_ms >= timeout_ms) {
			return -ETIMEDOUT;
		}

		k_msleep(EPD_BUSY_POLL_MS);
		elapsed_ms += EPD_BUSY_POLL_MS;
	}
}

static void bus_delay_ms(int32_t delay_ms, void *context)
{
	(void)context;
	k_msleep(delay_ms);
}

static const struct epd_154d_bus epd_bus = {
	.reset = bus_reset,
	.write_command = bus_write_command,
	.write_data = bus_write_data,
	.wait_ready = bus_wait_ready,
	.delay_ms = bus_delay_ms,
	.context = NULL,
};

int epd_154d_init(void)
{
	int result;

	if (epd_initialized) {
		return 0;
	}
	if (!spi_is_ready_dt(&epd_spi) || !device_is_ready(epd_dc.port) ||
	    !device_is_ready(epd_reset.port) || !device_is_ready(epd_busy.port)) {
		return -ENODEV;
	}

	result = gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_INACTIVE);
	if (result < 0) {
		return result;
	}
	result = gpio_pin_configure_dt(&epd_reset, GPIO_OUTPUT_INACTIVE);
	if (result < 0) {
		return result;
	}
	result = gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);
	if (result < 0) {
		return result;
	}

	epd_core_available = true;
	result = epd_154d_core_init(&epd_core, &epd_bus);
	if (result < 0) {
		return result;
	}

	epd_initialized = true;
	return 0;
}

int epd_154d_begin_image(void)
{
	if (!epd_initialized) {
		return -ENODEV;
	}

	return epd_154d_core_begin_image(&epd_core);
}

int epd_154d_write_image(const uint8_t *data, size_t length)
{
	if (!epd_initialized) {
		return -ENODEV;
	}

	return epd_154d_core_write_image(&epd_core, data, length);
}

int epd_154d_refresh(void)
{
	if (!epd_initialized) {
		return -ENODEV;
	}

	return epd_154d_core_refresh(&epd_core);
}

int epd_154d_abort(void)
{
	if (!epd_core_available) {
		return -ENODEV;
	}

	epd_initialized = false;
	epd_core_available = false;
	return epd_154d_core_sleep(&epd_core);
}

static int epd_show_test_pattern_inner(void)
{
	/* A full 10 KB frame buffer would overflow the task stack and, as a
	 * static buffer, would not fit in the remaining SRAM.  Instead the
	 * test pattern is generated one row at a time (50 bytes) and streamed
	 * through the normal write path, accumulating to the full 10000 bytes.
	 */
	uint8_t row[EPD_154D_WIDTH / 4U];
	int result;

	/* 2bpp color encoding must match the applet protocol:
	 * 00 black, 01 white, 10 yellow, 11 red.
	 */
	result = epd_154d_begin_image();
	if (result < 0) {
		return result;
	}

	for (uint32_t y = 0U; y < EPD_154D_HEIGHT; y++) {
		memset(row, 0x55, sizeof(row));

		for (uint32_t x = 0U; x < EPD_154D_WIDTH; x++) {
			uint8_t color;

			if (y < (EPD_154D_HEIGHT / 2U)) {
				color = (x < (EPD_154D_WIDTH / 2U)) ? 0x00 : 0x01;
			} else {
				color = (x < (EPD_154D_WIDTH / 2U)) ? 0x02 : 0x03;
			}

			const size_t index = x / 4U;
			const uint8_t shift = 6U - ((x % 4U) * 2U);

			row[index] &= (uint8_t)~(0x03U << shift);
			row[index] |= (uint8_t)(color << shift);
		}

		result = epd_154d_write_image(row, sizeof(row));
		if (result < 0) {
			return result;
		}
	}

	return epd_154d_refresh();
}

int epd_154d_show_test_pattern(void)
{
	int result;

	/* 上电自检期间总线被主线程独占；此时蓝牙可能已经连上并开始写图，
	 * BLE 侧通过 epd_154d_is_busy() 判断后延后自己的 EPD 工作项，
	 * 避免两条路径同时驱动 SPI 造成屏显损坏。 */
	epd_main_owns = true;
	result = epd_show_test_pattern_inner();
	epd_main_owns = false;

	return result;
}

bool epd_154d_is_busy(void)
{
	return epd_main_owns;
}

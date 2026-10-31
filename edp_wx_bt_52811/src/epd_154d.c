#include "epd_154d.h"

#include <errno.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(epd_154d, CONFIG_LOG_DEFAULT_LEVEL);

#define EPD_NODE DT_ALIAS(epd0)

#if !DT_NODE_HAS_STATUS(EPD_NODE, okay)
#error "epd0 alias is missing or disabled"
#endif

static const struct spi_dt_spec epd_spi = SPI_DT_SPEC_GET(
	EPD_NODE, SPI_WORD_SET(8) | SPI_TRANSFER_MSB);
static const struct gpio_dt_spec epd_dc = GPIO_DT_SPEC_GET(EPD_NODE, dc_gpios);
static const struct gpio_dt_spec epd_reset_gpio = GPIO_DT_SPEC_GET(EPD_NODE, reset_gpios);
static const struct gpio_dt_spec epd_busy = GPIO_DT_SPEC_GET(EPD_NODE, busy_gpios);

static uint8_t test_frame[EPD_154D_FRAME_BYTES];

static int epd_write_bytes(const uint8_t *data, size_t length)
{
	while (length > 0) {
		size_t chunk = MIN(length, 256U);
		struct spi_buf buf = {
			.buf = (void *)data,
			.len = chunk,
		};
		struct spi_buf_set set = {
			.buffers = &buf,
			.count = 1,
		};
		int ret = spi_write_dt(&epd_spi, &set);
		if (ret < 0) {
			return ret;
		}

		data += chunk;
		length -= chunk;
	}

	return 0;
}

static int epd_write_command(uint8_t cmd)
{
	int ret = gpio_pin_set_dt(&epd_dc, 0);
	if (ret < 0) {
		return ret;
	}

	return epd_write_bytes(&cmd, 1);
}

static int epd_write_data(const uint8_t *data, size_t length)
{
	int ret = gpio_pin_set_dt(&epd_dc, 1);
	if (ret < 0) {
		return ret;
	}

	return epd_write_bytes(data, length);
}

static int epd_wait_idle(uint32_t timeout_ms)
{
	int64_t deadline = k_uptime_get() + timeout_ms;

	while (gpio_pin_get_raw(epd_busy.port, epd_busy.pin) == 0) {
		if (k_uptime_get() >= deadline) {
			return -ETIMEDOUT;
		}
		k_msleep(20);
	}

	return 0;
}

static int epd_hw_reset(void)
{
	int ret = gpio_pin_set_dt(&epd_reset_gpio, 0);
	if (ret < 0) {
		return ret;
	}

	k_msleep(10);

	ret = gpio_pin_set_dt(&epd_reset_gpio, 1);
	if (ret < 0) {
		return ret;
	}

	k_msleep(10);
	return 0;
}

static int epd_refresh(void)
{
	int ret = epd_write_command(0x12);
	if (ret < 0) {
		return ret;
	}

	static const uint8_t refresh_param = 0x00;
	ret = epd_write_data(&refresh_param, 1);
	if (ret < 0) {
		return ret;
	}

	return epd_wait_idle(5000);
}

static int epd_write_frame(const uint8_t *frame, size_t length)
{
	int ret = epd_write_command(0x10);
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_data(frame, length);
	if (ret < 0) {
		return ret;
	}

	return epd_refresh();
}

int epd_154d_init(void)
{
	int ret;

	if (!device_is_ready(epd_spi.bus)) {
		return -ENODEV;
	}

	if (!gpio_is_ready_dt(&epd_dc) || !gpio_is_ready_dt(&epd_reset_gpio) ||
	    !gpio_is_ready_dt(&epd_busy)) {
		return -ENODEV;
	}

	ret = gpio_pin_configure_dt(&epd_dc, GPIO_OUTPUT_INACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&epd_reset_gpio, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return ret;
	}

	ret = gpio_pin_configure_dt(&epd_busy, GPIO_INPUT);
	if (ret < 0) {
		return ret;
	}

	ret = epd_hw_reset();
	if (ret < 0) {
		return ret;
	}

	ret = epd_wait_idle(2000);
	if (ret < 0) {
		return ret;
	}

	static const uint8_t init_4d[] = { 0x78 };
	static const uint8_t init_psr[] = { 0x0F, 0x29 };
	static const uint8_t init_btst[] = { 0x0D, 0x12, 0x30, 0x20, 0x19, 0x2A, 0x22 };
	static const uint8_t init_cdi[] = { 0x37 };
	static const uint8_t init_tres[] = { 0x00, 0xC8, 0x00, 0xC8 };
	static const uint8_t init_e9[] = { 0x01 };
	static const uint8_t init_30[] = { 0x08 };

	ret = epd_write_command(0x4D);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_4d, sizeof(init_4d));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x00);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_psr, sizeof(init_psr));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x06);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_btst, sizeof(init_btst));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x50);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_cdi, sizeof(init_cdi));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x61);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_tres, sizeof(init_tres));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0xE9);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_e9, sizeof(init_e9));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x30);
	if (ret < 0) {
		return ret;
	}
	ret = epd_write_data(init_30, sizeof(init_30));
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x04);
	if (ret < 0) {
		return ret;
	}

	return epd_wait_idle(5000);
}

int epd_154d_display(const uint8_t *frame, size_t length)
{
	if (frame == NULL || length != EPD_154D_FRAME_BYTES) {
		return -EINVAL;
	}

	return epd_write_frame(frame, length);
}

static void epd_fill_test_pattern(void)
{
	memset(test_frame, 0x55, sizeof(test_frame));

	for (uint32_t y = 0; y < EPD_154D_HEIGHT; y++) {
		for (uint32_t x = 0; x < EPD_154D_WIDTH; x++) {
			uint8_t color;

			if (y < (EPD_154D_HEIGHT / 2)) {
				color = (x < (EPD_154D_WIDTH / 2)) ? 0x00 : 0x01;
			} else {
				color = (x < (EPD_154D_WIDTH / 2)) ? 0x02 : 0x03;
			}

			size_t index = (y * (EPD_154D_WIDTH / 4)) + (x / 4);
			uint8_t shift = 6U - ((x % 4U) * 2U);
			test_frame[index] &= (uint8_t)~(0x03U << shift);
			test_frame[index] |= (uint8_t)(color << shift);
		}
	}
}

int epd_154d_show_test_pattern(void)
{
	epd_fill_test_pattern();
	return epd_154d_display(test_frame, sizeof(test_frame));
}

int epd_154d_sleep(void)
{
	int ret = epd_write_command(0x02);
	if (ret < 0) {
		return ret;
	}

	static const uint8_t sleep_param = 0x00;
	ret = epd_write_data(&sleep_param, 1);
	if (ret < 0) {
		return ret;
	}

	ret = epd_wait_idle(5000);
	if (ret < 0) {
		return ret;
	}

	ret = epd_write_command(0x07);
	if (ret < 0) {
		return ret;
	}

	static const uint8_t deep_sleep = 0xA5;
	return epd_write_data(&deep_sleep, 1);
}

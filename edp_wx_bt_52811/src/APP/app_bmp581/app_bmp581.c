#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <string.h>
#include "bosch/bmp5_defs.h"
#include "bosch/bmp5.h"
#include "app_bmp581.h"

LOG_MODULE_REGISTER(app_bmp581, CONFIG_LOG_DEFAULT_LEVEL);

#define BMP581_CS_PIN 16

static const struct device *spi_dev;
static const struct device *gpio0_dev;
static struct bmp5_dev bmp5_dev;

static const struct spi_config bmp581_spi_cfg = {
	.frequency = 1000000,
	.operation = SPI_WORD_SET(8) | SPI_OP_MODE_MASTER,
	.slave = 0,
	.cs = NULL,
};

/******************************************************************************/
/*! Zephyr SPI platform layer                                                */
/******************************************************************************/

static BMP5_INTF_RET_TYPE bmp581_spi_read(uint8_t reg_addr, uint8_t *reg_data,
					   uint32_t length, void *intf_ptr)
{
	int ret;
	uint8_t tx_buf[8] = {0};
	uint8_t rx_buf[8] = {0};

	tx_buf[0] = reg_addr;

	struct spi_buf tx = {
		.buf = tx_buf,
		.len = length + 1,
	};
	struct spi_buf_set tx_set = {
		.buffers = &tx,
		.count = 1,
	};
	struct spi_buf rx = {
		.buf = rx_buf,
		.len = length + 1,
	};
	struct spi_buf_set rx_set = {
		.buffers = &rx,
		.count = 1,
	};

	/* CS assert = LOW */
	gpio_pin_set(gpio0_dev, BMP581_CS_PIN, 0);
	ret = spi_transceive(spi_dev, &bmp581_spi_cfg, &tx_set, &rx_set);
	/* CS deassert = HIGH */
	gpio_pin_set(gpio0_dev, BMP581_CS_PIN, 1);

	if (ret < 0) {
		LOG_ERR("SPI read failed: %d", ret);
		return BMP5_E_COM_FAIL;
	}

	memcpy(reg_data, &rx_buf[1], length);
	return BMP5_INTF_RET_SUCCESS;
}

static BMP5_INTF_RET_TYPE bmp581_spi_write(uint8_t reg_addr,
					    const uint8_t *reg_data,
					    uint32_t length, void *intf_ptr)
{
	int ret;
	uint8_t tx_buf[8] = {0};

	tx_buf[0] = reg_addr;
	memcpy(&tx_buf[1], reg_data, length);

	struct spi_buf tx = {
		.buf = tx_buf,
		.len = length + 1,
	};
	struct spi_buf_set tx_set = {
		.buffers = &tx,
		.count = 1,
	};

	/* CS assert = LOW */
	gpio_pin_set(gpio0_dev, BMP581_CS_PIN, 0);
	ret = spi_transceive(spi_dev, &bmp581_spi_cfg, &tx_set, NULL);
	/* CS deassert = HIGH */
	gpio_pin_set(gpio0_dev, BMP581_CS_PIN, 1);

	if (ret < 0) {
		LOG_ERR("SPI write failed: %d", ret);
		return BMP5_E_COM_FAIL;
	}

	return BMP5_INTF_RET_SUCCESS;
}

static void bmp581_delay_us(uint32_t period, void *intf_ptr)
{
	k_busy_wait(period);
}

/******************************************************************************/
/*! Application interface                                                     */
/******************************************************************************/

int app_bmp581_init(void)
{
	int ret;
	int8_t rslt;

	spi_dev = DEVICE_DT_GET(DT_NODELABEL(spi2));
	if (!device_is_ready(spi_dev)) {
		LOG_ERR("SPI device not ready");
		return -ENODEV;
	}

	gpio0_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	if (!device_is_ready(gpio0_dev)) {
		LOG_ERR("GPIO0 device not ready");
		return -ENODEV;
	}

	ret = gpio_pin_configure(gpio0_dev, BMP581_CS_PIN, GPIO_OUTPUT);
	if (ret < 0) {
		LOG_ERR("Failed to configure CS pin: %d", ret);
		return ret;
	}
	gpio_pin_set(gpio0_dev, BMP581_CS_PIN, 1);

	bmp5_dev.read = bmp581_spi_read;
	bmp5_dev.write = bmp581_spi_write;
	bmp5_dev.delay_us = bmp581_delay_us;
	bmp5_dev.intf = BMP5_SPI_INTF;
	bmp5_dev.intf_ptr = NULL;

	rslt = bmp5_init(&bmp5_dev);
	if (rslt != BMP5_OK) {
		LOG_ERR("BMP5 init failed: %d", rslt);
		return -EIO;
	}

	LOG_INF("BMP581 initialized, chip_id: 0x%02X", bmp5_dev.chip_id);

	rslt = bmp5_set_power_mode(BMP5_POWERMODE_STANDBY, &bmp5_dev);
	if (rslt != BMP5_OK) {
		LOG_ERR("Set standby failed: %d", rslt);
		return -EIO;
	}

	struct bmp5_osr_odr_press_config osr_cfg = { 0 };
	osr_cfg.odr = BMP5_ODR_10_HZ;
	osr_cfg.press_en = BMP5_ENABLE;
	osr_cfg.osr_t = BMP5_OVERSAMPLING_2X;
	osr_cfg.osr_p = BMP5_OVERSAMPLING_4X;

	rslt = bmp5_set_osr_odr_press_config(&osr_cfg, &bmp5_dev);
	if (rslt != BMP5_OK) {
		LOG_ERR("Set OSR/ODR failed: %d", rslt);
		return -EIO;
	}

	rslt = bmp5_set_power_mode(BMP5_POWERMODE_NORMAL, &bmp5_dev);
	if (rslt != BMP5_OK) {
		LOG_ERR("Set normal mode failed: %d", rslt);
		return -EIO;
	}

	k_msleep(200);
	return 0;
}

int app_bmp581_read_pressure(float *pressure_pa, float *temperature_c)
{
	int8_t rslt;
	struct bmp5_sensor_data data;
	struct bmp5_osr_odr_press_config osr_cfg = { 0 };

	osr_cfg.press_en = BMP5_ENABLE;

	rslt = bmp5_get_sensor_data(&data, &osr_cfg, &bmp5_dev);
	if (rslt != BMP5_OK) {
		LOG_ERR("Get sensor data failed: %d", rslt);
		return -EIO;
	}

	*pressure_pa = data.pressure;
	*temperature_c = data.temperature;
	return 0;
}

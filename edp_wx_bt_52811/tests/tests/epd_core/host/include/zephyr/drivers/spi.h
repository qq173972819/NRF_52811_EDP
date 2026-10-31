/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HOST_ZEPHYR_DRIVERS_SPI_H_
#define HOST_ZEPHYR_DRIVERS_SPI_H_

#include <stdbool.h>
#include <stddef.h>

#include <zephyr/device.h>

extern struct device host_spi_device;

struct spi_dt_spec {
	const struct device *bus;
};

struct spi_buf {
	void *buf;
	size_t len;
};

struct spi_buf_set {
	const struct spi_buf *buffers;
	size_t count;
};

#define SPI_WORD_SET(bits) (bits)
#define SPI_TRANSFER_MSB 0
#define SPI_DT_SPEC_GET(node, operation, delay) { &host_spi_device }

static inline bool spi_is_ready_dt(const struct spi_dt_spec *spec)
{
	return spec != NULL && device_is_ready(spec->bus);
}

int spi_write_dt(const struct spi_dt_spec *spec,
		 const struct spi_buf_set *buffers);

#endif /* HOST_ZEPHYR_DRIVERS_SPI_H_ */

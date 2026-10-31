/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EPD_154D_CORE_H_
#define EPD_154D_CORE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_154D_IMAGE_BYTES 10000U
#define EPD_BUSY_TIMEOUT_MS 30000

struct epd_154d_bus {
	int (*reset)(bool asserted, void *context);
	int (*write_command)(uint8_t command, void *context);
	int (*write_data)(const uint8_t *data, size_t length, void *context);
	int (*wait_ready)(int32_t timeout_ms, void *context);
	void (*delay_ms)(int32_t delay_ms, void *context);
	void *context;
};

struct epd_154d_core {
	struct epd_154d_bus bus;
	size_t bytes_written;
	uint8_t state;
};

int epd_154d_core_init(struct epd_154d_core *core,
			const struct epd_154d_bus *bus);
int epd_154d_core_begin_image(struct epd_154d_core *core);
int epd_154d_core_write_image(struct epd_154d_core *core, const uint8_t *data,
			      size_t length);
int epd_154d_core_refresh(struct epd_154d_core *core);
int epd_154d_core_sleep(struct epd_154d_core *core);

#ifdef __cplusplus
}
#endif

#endif /* EPD_154D_CORE_H_ */

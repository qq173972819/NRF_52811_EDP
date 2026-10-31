/* SPDX-License-Identifier: Apache-2.0 */

#include "epd_154d_core.h"

#include <errno.h>

enum epd_154d_state {
	EPD_154D_STATE_UNINITIALIZED,
	EPD_154D_STATE_POWERED,
	EPD_154D_STATE_READY,
	EPD_154D_STATE_WRITING,
	EPD_154D_STATE_SLEEPING,
};

static int write_command_data(struct epd_154d_core *core, uint8_t command,
			      const uint8_t *data, size_t length)
{
	int result = core->bus.write_command(command, core->bus.context);

	if (result < 0) {
		return result;
	}

	return core->bus.write_data(data, length, core->bus.context);
}

static bool bus_is_valid(const struct epd_154d_bus *bus)
{
	return bus != NULL && bus->reset != NULL && bus->write_command != NULL &&
	       bus->write_data != NULL && bus->wait_ready != NULL &&
	       bus->delay_ms != NULL;
}

int epd_154d_core_init(struct epd_154d_core *core,
			const struct epd_154d_bus *bus)
{
	static const uint8_t data_4d[] = { 0x78 };
	static const uint8_t data_00[] = { 0x0f, 0x29 };
	static const uint8_t data_06[] = {
		0x0d, 0x12, 0x30, 0x20, 0x19, 0x2a, 0x22,
	};
	static const uint8_t data_50[] = { 0x37 };
	static const uint8_t data_61[] = { 0x00, 0xc8, 0x00, 0xc8 };
	static const uint8_t data_e9[] = { 0x01 };
	static const uint8_t data_30[] = { 0x08 };
	int result;

	if (core == NULL || !bus_is_valid(bus)) {
		return -EINVAL;
	}

	core->bus = *bus;
	core->bytes_written = 0;
	core->state = EPD_154D_STATE_UNINITIALIZED;

	result = core->bus.reset(true, core->bus.context);
	if (result < 0) {
		return result;
	}
	core->bus.delay_ms(50, core->bus.context);

	result = core->bus.reset(false, core->bus.context);
	if (result < 0) {
		return result;
	}
	core->bus.delay_ms(50, core->bus.context);

	result = core->bus.wait_ready(EPD_BUSY_TIMEOUT_MS, core->bus.context);
	if (result < 0) {
		return result;
	}

#define WRITE_INIT(command, data)                                               \
	do {                                                                       \
		result = write_command_data(core, command, data, sizeof(data));      \
		if (result < 0) {                                                    \
			return result;                                                 \
		}                                                                    \
	} while (0)

	WRITE_INIT(0x4d, data_4d);
	WRITE_INIT(0x00, data_00);
	WRITE_INIT(0x06, data_06);
	WRITE_INIT(0x50, data_50);
	WRITE_INIT(0x61, data_61);
	WRITE_INIT(0xe9, data_e9);
	WRITE_INIT(0x30, data_30);

#undef WRITE_INIT

	result = core->bus.write_command(0x04, core->bus.context);
	if (result < 0) {
		return result;
	}
	core->state = EPD_154D_STATE_POWERED;
	result = core->bus.wait_ready(EPD_BUSY_TIMEOUT_MS, core->bus.context);
	if (result < 0) {
		return result;
	}

	core->state = EPD_154D_STATE_READY;
	return 0;
}

int epd_154d_core_begin_image(struct epd_154d_core *core)
{
	int result;

	if (core == NULL) {
		return -EINVAL;
	}
	if (core->state != EPD_154D_STATE_READY) {
		return -EPIPE;
	}

	result = core->bus.write_command(0x10, core->bus.context);
	if (result < 0) {
		return result;
	}

	core->bytes_written = 0;
	core->state = EPD_154D_STATE_WRITING;
	return 0;
}

int epd_154d_core_write_image(struct epd_154d_core *core, const uint8_t *data,
			      size_t length)
{
	int result;

	if (core == NULL || data == NULL || length == 0) {
		return -EINVAL;
	}
	if (core->state != EPD_154D_STATE_WRITING) {
		return -EPIPE;
	}
	if (length > EPD_154D_IMAGE_BYTES - core->bytes_written) {
		return -EMSGSIZE;
	}

	result = core->bus.write_data(data, length, core->bus.context);
	if (result < 0) {
		return result;
	}

	core->bytes_written += length;
	return 0;
}

int epd_154d_core_refresh(struct epd_154d_core *core)
{
	static const uint8_t refresh_data = 0x00;
	int result;

	if (core == NULL) {
		return -EINVAL;
	}
	if (core->state != EPD_154D_STATE_WRITING) {
		return -EPIPE;
	}
	if (core->bytes_written != EPD_154D_IMAGE_BYTES) {
		return -EMSGSIZE;
	}

	result = write_command_data(core, 0x12, &refresh_data, 1);
	if (result < 0) {
		return result;
	}
	result = core->bus.wait_ready(EPD_BUSY_TIMEOUT_MS, core->bus.context);
	if (result < 0) {
		return result;
	}

	core->state = EPD_154D_STATE_READY;
	return 0;
}

int epd_154d_core_sleep(struct epd_154d_core *core)
{
	static const uint8_t power_off_data = 0x00;
	static const uint8_t deep_sleep_key = 0xa5;
	int result;

	if (core == NULL) {
		return -EINVAL;
	}
	if (core->state != EPD_154D_STATE_POWERED &&
	    core->state != EPD_154D_STATE_READY &&
	    core->state != EPD_154D_STATE_WRITING) {
		return -EPIPE;
	}

	result = write_command_data(core, 0x02, &power_off_data, 1);
	if (result < 0) {
		return result;
	}
	result = core->bus.wait_ready(EPD_BUSY_TIMEOUT_MS, core->bus.context);
	if (result < 0) {
		return result;
	}
	result = write_command_data(core, 0x07, &deep_sleep_key, 1);
	if (result < 0) {
		return result;
	}

	core->state = EPD_154D_STATE_SLEEPING;
	return 0;
}

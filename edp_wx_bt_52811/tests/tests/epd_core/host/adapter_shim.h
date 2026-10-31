/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EPD_CORE_HOST_ADAPTER_SHIM_H_
#define EPD_CORE_HOST_ADAPTER_SHIM_H_

#include <stddef.h>
#include <stdint.h>

struct host_spi_event {
	int dc;
	size_t length;
	uint8_t first_byte;
};

struct host_adapter_state {
	int configure_count;
	int reset_set_count;
	int reset_values[2];
	int busy_get_count;
	int busy_reads_remaining;
	int busy_error;
	int fail_busy_at;
	int sleep_count;
	int32_t sleep_total_ms;
	int spi_count;
	int fail_spi_at;
	struct host_spi_event spi_events[32];
};

extern struct host_adapter_state host_adapter;

void host_adapter_reset(void);
void host_adapter_clear_events(void);

#endif /* EPD_CORE_HOST_ADAPTER_SHIM_H_ */

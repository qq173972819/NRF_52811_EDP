/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ADAPTER_SHIM_H_
#define POWER_LOGIC_HOST_ADAPTER_SHIM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct host_power_adapter_state {
	int gpio_configure_result;
	int gpio_configure_count;
	int gpio_configure_flags;
	uint32_t gpio_node_id;
	uint32_t gpio_prop_id;
	int gpio_set_count;
	int gpio_values[32];
	int adc_setup_result;
	int adc_setup_count;
	uint32_t adc_node_id;
	int adc_sequence_init_result;
	int adc_sequence_init_count;
	uint8_t adc_resolution;
	uint8_t adc_oversampling;
	int adc_read_result;
	int adc_read_count;
	int16_t adc_raw_value;
	int adc_convert_result;
	int adc_convert_count;
	int32_t adc_converted_mv;
	int work_reschedule_count;
	int work_cancel_count;
	int work_cancel_while_running_count;
	int work_self_cancel_violation_count;
	int work_queued_count;
	int spinlock_acquire_count;
	int spinlock_depth;
	int spinlock_max_depth;
	int spinlock_violation_count;
	bool setter_during_led_cancel;
};

extern struct host_power_adapter_state host_power_adapter;

void host_power_adapter_reset(void);
void host_advance_time(uint32_t elapsed_ms);
bool host_run_work_with_delay(uint32_t delay_ms);
int host_pending_work_with_delay(uint32_t delay_ms);
int64_t host_monotonic_ms(void);
void host_adc_block_next_read(void);
bool host_start_work_thread_with_delay(uint32_t delay_ms);
bool host_wait_adc_read_blocked(void);
bool host_start_battery_suspend_thread(void);
bool host_wait_cancel_sync_waiting(void);
void host_release_adc_read(void);
bool host_join_concurrency_threads(void);

#endif /* POWER_LOGIC_HOST_ADAPTER_SHIM_H_ */

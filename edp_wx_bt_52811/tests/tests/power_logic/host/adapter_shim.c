/* SPDX-License-Identifier: Apache-2.0 */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

#include <limits.h>
#include <string.h>

#include "adapter_shim.h"
#include "battery/app_battery.h"
#include "led/app_led.h"

#define MAX_TRACKED_WORK 8
#define HOST_SPINLOCK_KEY 0xa5U
#define HOST_WAIT_MS 5000U

struct device host_gpio_device = { true };
struct device host_adc_device = { true };
struct host_power_adapter_state host_power_adapter;

static INIT_ONCE sync_once = INIT_ONCE_STATIC_INIT;
static CRITICAL_SECTION work_lock;
static HANDLE adc_entered_event;
static HANDLE adc_release_event;
static HANDLE cancel_waiting_event;
static HANDLE work_thread_handle;
static HANDLE suspend_thread_handle;
static struct k_work_delayable *tracked_work[MAX_TRACKED_WORK];
static size_t tracked_work_count;
static int64_t monotonic_ms;
static uint32_t threaded_work_delay_ms;
static volatile LONG block_adc_read;

static BOOL CALLBACK sync_init(PINIT_ONCE once, PVOID parameter, PVOID *context)
{
	(void)once;
	(void)parameter;
	(void)context;
	InitializeCriticalSection(&work_lock);
	adc_entered_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	adc_release_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	cancel_waiting_event = CreateEvent(NULL, TRUE, FALSE, NULL);
	return adc_entered_event != NULL && adc_release_event != NULL &&
	       cancel_waiting_event != NULL;
}

static void ensure_sync(void)
{
	(void)InitOnceExecuteOnce(&sync_once, sync_init, NULL, NULL);
}

static void track_work_locked(struct k_work_delayable *work)
{
	for (size_t i = 0; i < tracked_work_count; ++i) {
		if (tracked_work[i] == work) {
			return;
		}
	}

	if (tracked_work_count < MAX_TRACKED_WORK) {
		tracked_work[tracked_work_count++] = work;
	}
}

static HANDLE work_completion_event_locked(struct k_work_delayable *work)
{
	if (work->completion_event == NULL) {
		work->completion_event = CreateEvent(NULL, TRUE, TRUE, NULL);
	}
	return (HANDLE)work->completion_event;
}

static uint32_t work_remaining_ms_locked(const struct k_work_delayable *work)
{
	const int64_t remaining = work->due_ms - monotonic_ms;

	if (remaining <= 0) {
		return 0U;
	}
	return remaining > UINT32_MAX ? UINT32_MAX : (uint32_t)remaining;
}

void host_power_adapter_reset(void)
{
	ensure_sync();
	EnterCriticalSection(&work_lock);
	for (size_t i = 0; i < tracked_work_count; ++i) {
		tracked_work[i]->scheduled = false;
		tracked_work[i]->running = false;
		tracked_work[i]->queued = false;
		tracked_work[i]->cancel_requested = false;
		tracked_work[i]->due_ms = 0;
		tracked_work[i]->running_thread_id = 0U;
		if (tracked_work[i]->completion_event != NULL) {
			(void)SetEvent((HANDLE)tracked_work[i]->completion_event);
		}
	}
	memset(&host_power_adapter, 0, sizeof(host_power_adapter));
	monotonic_ms = 0;
	LeaveCriticalSection(&work_lock);
	(void)ResetEvent(adc_entered_event);
	(void)ResetEvent(adc_release_event);
	(void)ResetEvent(cancel_waiting_event);
	(void)InterlockedExchange(&block_adc_read, 0);
	host_gpio_device.ready = true;
	host_adc_device.ready = true;
}

void host_advance_time(uint32_t elapsed_ms)
{
	ensure_sync();
	EnterCriticalSection(&work_lock);
	monotonic_ms = monotonic_ms > INT64_MAX - elapsed_ms ?
			INT64_MAX : monotonic_ms + elapsed_ms;
	LeaveCriticalSection(&work_lock);
}

int64_t host_monotonic_ms(void)
{
	int64_t now_ms;

	ensure_sync();
	EnterCriticalSection(&work_lock);
	now_ms = monotonic_ms;
	LeaveCriticalSection(&work_lock);
	return now_ms;
}

bool host_run_work_with_delay(uint32_t delay_ms)
{
	struct k_work_delayable *selected = NULL;
	HANDLE completion_event = NULL;

	ensure_sync();
	EnterCriticalSection(&work_lock);
	for (size_t i = 0; i < tracked_work_count; ++i) {
		struct k_work_delayable *work = tracked_work[i];

		if (work->scheduled &&
		    work_remaining_ms_locked(work) == delay_ms) {
			selected = work;
			break;
		}
	}
	if (selected != NULL) {
		if (selected->due_ms > monotonic_ms) {
			monotonic_ms = selected->due_ms;
		}
		completion_event = work_completion_event_locked(selected);
		(void)ResetEvent(completion_event);
		selected->scheduled = false;
		selected->queued = false;
		selected->running = true;
		selected->cancel_requested = false;
		selected->running_thread_id = GetCurrentThreadId();
	}
	LeaveCriticalSection(&work_lock);

	if (selected == NULL) {
		return false;
	}
	selected->work.handler(&selected->work);

	EnterCriticalSection(&work_lock);
	selected->running = false;
	selected->running_thread_id = 0U;
	selected->queued = false;
	if (selected->cancel_requested) {
		selected->scheduled = false;
		selected->cancel_requested = false;
	}
	(void)SetEvent(completion_event);
	LeaveCriticalSection(&work_lock);
	return true;
}

int host_pending_work_with_delay(uint32_t delay_ms)
{
	int count = 0;

	ensure_sync();
	EnterCriticalSection(&work_lock);
	for (size_t i = 0; i < tracked_work_count; ++i) {
		if (tracked_work[i]->scheduled &&
		    work_remaining_ms_locked(tracked_work[i]) == delay_ms) {
			++count;
		}
	}
	LeaveCriticalSection(&work_lock);
	return count;
}

static DWORD WINAPI work_thread_main(LPVOID parameter)
{
	(void)parameter;
	return host_run_work_with_delay(threaded_work_delay_ms) ? 0U : 1U;
}

static DWORD WINAPI suspend_thread_main(LPVOID parameter)
{
	(void)parameter;
	app_battery_suspend();
	return 0U;
}

void host_adc_block_next_read(void)
{
	ensure_sync();
	(void)ResetEvent(adc_entered_event);
	(void)ResetEvent(adc_release_event);
	(void)ResetEvent(cancel_waiting_event);
	(void)InterlockedExchange(&block_adc_read, 1);
}

bool host_start_work_thread_with_delay(uint32_t delay_ms)
{
	threaded_work_delay_ms = delay_ms;
	work_thread_handle = CreateThread(NULL, 0U, work_thread_main, NULL, 0U, NULL);
	return work_thread_handle != NULL;
}

bool host_wait_adc_read_blocked(void)
{
	return WaitForSingleObject(adc_entered_event, HOST_WAIT_MS) == WAIT_OBJECT_0;
}

bool host_start_battery_suspend_thread(void)
{
	suspend_thread_handle =
		CreateThread(NULL, 0U, suspend_thread_main, NULL, 0U, NULL);
	return suspend_thread_handle != NULL;
}

bool host_wait_cancel_sync_waiting(void)
{
	return WaitForSingleObject(cancel_waiting_event, HOST_WAIT_MS) ==
	       WAIT_OBJECT_0;
}

void host_release_adc_read(void)
{
	(void)SetEvent(adc_release_event);
}

bool host_join_concurrency_threads(void)
{
	DWORD work_exit = 1U;
	DWORD suspend_exit = 1U;
	bool joined = true;

	if (work_thread_handle == NULL || suspend_thread_handle == NULL) {
		return false;
	}
	joined = WaitForSingleObject(work_thread_handle, HOST_WAIT_MS) ==
		 WAIT_OBJECT_0;
	joined = (WaitForSingleObject(suspend_thread_handle, HOST_WAIT_MS) ==
		  WAIT_OBJECT_0) && joined;
	(void)GetExitCodeThread(work_thread_handle, &work_exit);
	(void)GetExitCodeThread(suspend_thread_handle, &suspend_exit);
	(void)CloseHandle(work_thread_handle);
	(void)CloseHandle(suspend_thread_handle);
	work_thread_handle = NULL;
	suspend_thread_handle = NULL;
	return joined && work_exit == 0U && suspend_exit == 0U;
}

int gpio_pin_configure_dt(const struct gpio_dt_spec *spec, int flags)
{
	++host_power_adapter.gpio_configure_count;
	host_power_adapter.gpio_configure_flags = flags;
	host_power_adapter.gpio_node_id = spec->node_id;
	host_power_adapter.gpio_prop_id = spec->prop_id;
	return host_power_adapter.gpio_configure_result;
}

int gpio_pin_set_dt(const struct gpio_dt_spec *spec, int value)
{
	(void)spec;
	if (host_power_adapter.gpio_set_count <
	    (int)(sizeof(host_power_adapter.gpio_values) /
		  sizeof(host_power_adapter.gpio_values[0]))) {
		host_power_adapter.gpio_values[host_power_adapter.gpio_set_count] =
			value;
	}
	++host_power_adapter.gpio_set_count;
	return 0;
}

int adc_channel_setup_dt(const struct adc_dt_spec *spec)
{
	++host_power_adapter.adc_setup_count;
	host_power_adapter.adc_node_id = spec->node_id;
	return host_power_adapter.adc_setup_result;
}

int adc_sequence_init_dt(const struct adc_dt_spec *spec,
			 struct adc_sequence *sequence)
{
	++host_power_adapter.adc_sequence_init_count;
	sequence->channels = 1U << spec->channel_id;
	sequence->resolution = spec->resolution;
	sequence->oversampling = spec->oversampling;
	host_power_adapter.adc_resolution = sequence->resolution;
	host_power_adapter.adc_oversampling = sequence->oversampling;
	return host_power_adapter.adc_sequence_init_result;
}

int adc_read(const struct device *device, const struct adc_sequence *sequence)
{
	(void)device;
	++host_power_adapter.adc_read_count;
	if (host_power_adapter.adc_read_result == 0 && sequence->buffer != NULL &&
	    sequence->buffer_size >= sizeof(host_power_adapter.adc_raw_value)) {
		*(int16_t *)sequence->buffer = host_power_adapter.adc_raw_value;
	}
	if (InterlockedCompareExchange(&block_adc_read, 0, 0) != 0) {
		(void)SetEvent(adc_entered_event);
		(void)WaitForSingleObject(adc_release_event, INFINITE);
		(void)InterlockedExchange(&block_adc_read, 0);
	}
	return host_power_adapter.adc_read_result;
}

int adc_raw_to_millivolts_dt(const struct adc_dt_spec *spec, int32_t *value)
{
	(void)spec;
	++host_power_adapter.adc_convert_count;
	if (host_power_adapter.adc_convert_result == 0) {
		*value = host_power_adapter.adc_converted_mv;
	}
	return host_power_adapter.adc_convert_result;
}

static SRWLOCK *spinlock_native(struct k_spinlock *lock)
{
	SRWLOCK *native = (SRWLOCK *)lock->native_lock;

	if (native == NULL) {
		SRWLOCK *candidate =
			(SRWLOCK *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
					      sizeof(*candidate));
		PVOID previous;

		if (candidate == NULL) {
			return NULL;
		}
		InitializeSRWLock(candidate);
		previous = InterlockedCompareExchangePointer(
			(PVOID volatile *)&lock->native_lock, candidate, NULL);
		if (previous != NULL) {
			(void)HeapFree(GetProcessHeap(), 0U, candidate);
			native = (SRWLOCK *)previous;
		} else {
			native = candidate;
		}
	}
	return native;
}

k_spinlock_key_t k_spin_lock(struct k_spinlock *lock)
{
	SRWLOCK *native = spinlock_native(lock);
	LONG depth;

	if (native == NULL) {
		(void)InterlockedIncrement(
			(volatile LONG *)&host_power_adapter.spinlock_violation_count);
		return 0U;
	}
	AcquireSRWLockExclusive(native);
	(void)InterlockedIncrement(
		(volatile LONG *)&host_power_adapter.spinlock_acquire_count);
	if (lock->depth != 0) {
		(void)InterlockedIncrement(
			(volatile LONG *)&host_power_adapter.spinlock_violation_count);
	}
	++lock->depth;
	depth = InterlockedIncrement(
		(volatile LONG *)&host_power_adapter.spinlock_depth);
	if (depth > host_power_adapter.spinlock_max_depth) {
		host_power_adapter.spinlock_max_depth = depth;
	}
	return HOST_SPINLOCK_KEY;
}

void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key)
{
	SRWLOCK *native = (SRWLOCK *)lock->native_lock;

	if (native == NULL || lock->depth != 1 || key != HOST_SPINLOCK_KEY ||
	    host_power_adapter.spinlock_depth <= 0) {
		(void)InterlockedIncrement(
			(volatile LONG *)&host_power_adapter.spinlock_violation_count);
		return;
	}
	--lock->depth;
	(void)InterlockedDecrement(
		(volatile LONG *)&host_power_adapter.spinlock_depth);
	ReleaseSRWLockExclusive(native);
}

int k_work_reschedule(struct k_work_delayable *work, k_timeout_t delay)
{
	int result;

	ensure_sync();
	EnterCriticalSection(&work_lock);
	track_work_locked(work);
	(void)work_completion_event_locked(work);
	if (work->running && delay == K_NO_WAIT) {
		work->queued = true;
		++host_power_adapter.work_queued_count;
	}
	if (!work->running) {
		work->cancel_requested = false;
	}
	work->scheduled = true;
	work->due_ms = monotonic_ms > INT64_MAX - delay ?
			INT64_MAX : monotonic_ms + delay;
	++host_power_adapter.work_reschedule_count;
	result = work->running && delay == K_NO_WAIT ? 2 : 1;
	LeaveCriticalSection(&work_lock);
	return result;
}

bool k_work_cancel_delayable_sync(struct k_work_delayable *work,
				  struct k_work_sync *sync)
{
	HANDLE completion_event;
	bool was_busy;
	bool wait_for_completion = false;
	bool invoke_setter;

	(void)sync;
	invoke_setter = host_power_adapter.setter_during_led_cancel;
	if (invoke_setter) {
		app_led_set_battery_low(true);
	}

	ensure_sync();
	EnterCriticalSection(&work_lock);
	track_work_locked(work);
	completion_event = work_completion_event_locked(work);
	was_busy = work->scheduled || work->running || work->queued;
	if (work->running && work->running_thread_id == GetCurrentThreadId()) {
		++host_power_adapter.work_self_cancel_violation_count;
		LeaveCriticalSection(&work_lock);
		return false;
	}
	if (work->running) {
		work->cancel_requested = true;
		wait_for_completion = true;
		++host_power_adapter.work_cancel_while_running_count;
	}
	work->scheduled = false;
	work->queued = false;
	++host_power_adapter.work_cancel_count;
	LeaveCriticalSection(&work_lock);

	if (wait_for_completion) {
		(void)SetEvent(cancel_waiting_event);
		(void)WaitForSingleObject(completion_event, INFINITE);
	}
	return was_busy;
}

int64_t k_uptime_get(void)
{
	return host_monotonic_ms();
}

/* SPDX-License-Identifier: Apache-2.0 */

#ifndef POWER_LOGIC_HOST_ZEPHYR_KERNEL_H_
#define POWER_LOGIC_HOST_ZEPHYR_KERNEL_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t k_timeout_t;

#define K_NO_WAIT 0U
#define K_MSEC(ms) ((k_timeout_t)(ms))

struct k_work {
	void (*handler)(struct k_work *work);
};

struct k_work_delayable {
	struct k_work work;
	bool scheduled;
	bool running;
	bool queued;
	bool cancel_requested;
	int64_t due_ms;
	void *completion_event;
	uint32_t running_thread_id;
};

struct k_work_sync {
	int unused;
};

struct k_spinlock {
	void *native_lock;
	int depth;
};

typedef unsigned int k_spinlock_key_t;

#define K_WORK_DELAYABLE_DEFINE(name, work_handler)                              \
	struct k_work_delayable name = { { work_handler }, false, false, false,    \
					  false, 0, NULL, 0U }

k_spinlock_key_t k_spin_lock(struct k_spinlock *lock);
void k_spin_unlock(struct k_spinlock *lock, k_spinlock_key_t key);
int k_work_reschedule(struct k_work_delayable *work, k_timeout_t delay);
bool k_work_cancel_delayable_sync(struct k_work_delayable *work,
				  struct k_work_sync *sync);
int64_t k_uptime_get(void);

#endif /* POWER_LOGIC_HOST_ZEPHYR_KERNEL_H_ */

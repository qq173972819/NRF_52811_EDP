#ifndef __APP_WATCHDOG_H__
#define __APP_WATCHDOG_H__

#include <stdint.h>
#include <stdbool.h>

int watchdog_init(void);
void app_power_feed_watchdog(void);

#endif /* __APP_WATCHDOG_H__ */

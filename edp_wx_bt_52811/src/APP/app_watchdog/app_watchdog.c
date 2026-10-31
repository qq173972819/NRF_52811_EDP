
#include "app_watchdog.h"
#include <zephyr/logging/log.h>
#include <zephyr/drivers/watchdog.h>
#include "APP/app_include.h"

LOG_MODULE_REGISTER(app_watchdog, CONFIG_LOG_DEFAULT_LEVEL);
/* ========================================================================== */
/*  看门狗                                                                    */
/* ========================================================================== */

#define WDT_FEED_INTERVAL_MS 10000 /* 喂狗间隔 10 秒 */
#define WDT_TIMEOUT_MS 30000       /* 超时 30 秒 */

static const struct device *wdt_dev;
static int wdt_channel_id;

int watchdog_init(void)
{
    wdt_dev = DEVICE_DT_GET(DT_NODELABEL(wdt0));
    if (!device_is_ready(wdt_dev))
    {
        my_logW("Watchdog device not ready");
        return -ENODEV;
    }

    struct wdt_timeout_cfg wdt_cfg = {
        .window.min = 0,
        .window.max = WDT_TIMEOUT_MS,
        .callback = NULL, /* 超时直接复位 */
        .flags = WDT_FLAG_RESET_SOC,
    };

    wdt_channel_id = wdt_install_timeout(wdt_dev, &wdt_cfg);
    if (wdt_channel_id < 0)
    {
        my_logE("Watchdog install failed: %d", wdt_channel_id);
        return wdt_channel_id;
    }

    int err = wdt_setup(wdt_dev, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err)
    {
        my_logE("Watchdog setup failed: %d", err);
        return err;
    }

    my_logI("Watchdog started (timeout %d ms)", WDT_TIMEOUT_MS);
    return 0;
}

void app_power_feed_watchdog(void)
{
    if (wdt_dev && device_is_ready(wdt_dev))
    {
        wdt_feed(wdt_dev, wdt_channel_id);
    }
}
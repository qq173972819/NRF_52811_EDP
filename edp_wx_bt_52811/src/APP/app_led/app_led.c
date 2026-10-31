#include "app_led.h"
#include <errno.h>
#include <zephyr/logging/log.h>
#include "APP/app_include.h"

LOG_MODULE_REGISTER(app_led, CONFIG_LOG_DEFAULT_LEVEL);

#define LED_NODE DT_NODELABEL(sysled)
struct gpio_dt_spec led_struct = GPIO_DT_SPEC_GET(LED_NODE, gpios);

int app_led_init(void)
{
    if (!gpio_is_ready_dt(&led_struct))
    {
        return -ENODEV;
    }

    int ret = gpio_pin_configure_dt(&led_struct, GPIO_OUTPUT_INACTIVE);
    if (ret < 0) {
        return ret;
    }

    /* Brief flash to indicate boot */
    ret = LED_ON();
    if (ret < 0) {
        return ret;
    }
    k_msleep(500);
    return LED_OFF();
}

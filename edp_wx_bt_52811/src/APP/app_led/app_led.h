#ifndef __APP_LED_H__
#define __APP_LED_H__

#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>



/* LED GPIO spec (defined in app_led.c) */
extern struct gpio_dt_spec led_struct;

/* LED macros */
#define LED_ON()    gpio_pin_set_dt(&led_struct, 1)
#define LED_OFF()   gpio_pin_set_dt(&led_struct, 0)
#define LED_TOGGLE() gpio_pin_toggle_dt(&led_struct)

/**
 * @brief Initialize LED GPIO and turn on briefly
 */
int app_led_init(void);

#endif /* __APP_LED_H__ */

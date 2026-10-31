/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LED_APP_LED_H_
#define LED_APP_LED_H_

#include <stdbool.h>

int app_led_init(void);
void app_led_set_connected(bool connected);
void app_led_set_receiving(bool receiving);
void app_led_refresh_flash(void);
void app_led_set_battery_low(bool battery_low);
void app_led_force_off(void);

#endif /* LED_APP_LED_H_ */

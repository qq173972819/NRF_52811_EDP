/* SPDX-License-Identifier: Apache-2.0 */

#ifndef BLE_APP_BLE_EPD_H_
#define BLE_APP_BLE_EPD_H_

#include <stdbool.h>
#include <stdint.h>

int app_ble_epd_init(void);
int app_ble_epd_start_advertising(uint32_t interval_ms);
void app_ble_epd_stop(void);
bool app_ble_epd_is_busy(void);
void app_ble_epd_disconnect(void);

#endif /* BLE_APP_BLE_EPD_H_ */

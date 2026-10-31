#ifndef __APP_NVS_H__
#define __APP_NVS_H__

#include <stdint.h>
#include "APP/app_include.h"

#define DEFAULT_CARD_ID     258000000000ULL
#define DEFAULT_TX_POWER    (-60)
#define DEFAULT_TX_INTERVAL 2500

/**
 * @brief Initialize NVS flash storage
 * @return 0 on success, negative errno on failure
 */
int app_nvs_init(void);

/**
 * @brief Read beacon config from NVS
 * @param cfg  Pointer to store the config
 * @return 
 */
void app_nvs_read_config(stBeacon_Cfg *cfg);

/**
 * @brief Save beacon config to NVS
 * @param cfg  Config to save
 * @return 
 */
void app_nvs_save_config(stBeacon_Cfg *cfg);

#endif /* __APP_NVS_H__ */

#include "app_nvs.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/fs/nvs.h>
#include "APP/app_include.h"

#define NVS_CONFIG_KEY 1

static struct nvs_fs fs;
stBeacon_Cfg m_BeaconCfg; /* Global beacon configuration */

int app_nvs_init(void)
{
    my_log("app_nvs_init\n");
    const struct flash_area *fa;
    int ret = flash_area_open(FIXED_PARTITION_ID(storage_partition), &fa);
    if (ret)
    {
        my_log("Flash area open failed: %d\n", ret);
        return ret;
    }

    fs.flash_device = fa->fa_dev;
    fs.offset = fa->fa_off;
    fs.sector_size = 4096;
    fs.sector_count = fa->fa_size / fs.sector_size;

    ret = nvs_mount(&fs);
    if (ret)
    {
        my_log("NVS mount failed: %d\n", ret);
    }
    else
    {
        my_log("NVS mounted (%d sectors)\n", fs.sector_count);
    }

    flash_area_close(fa);
    app_nvs_read_config(&m_BeaconCfg);
    my_log("Beacon Config - Card ID: %llu, TX Power: %d, TX Interval: %u\n",
           m_BeaconCfg.card_id, m_BeaconCfg.tx_power, m_BeaconCfg.tx_interval);
    return ret;
}

void app_nvs_read_config(stBeacon_Cfg *cfg)
{
    ssize_t ret = nvs_read(&fs, NVS_CONFIG_KEY, cfg, sizeof(*cfg));
    if (ret < 0)
    {
        my_log("NVS read failed: %d, using default config\n", ret);
        cfg->card_id = DEFAULT_CARD_ID;
        cfg->tx_power = DEFAULT_TX_POWER;
        cfg->tx_interval = DEFAULT_TX_INTERVAL;
        app_nvs_save_config(cfg);
    }else
    {
        my_log("NVS read OK\n");
    }
}

void app_nvs_save_config(stBeacon_Cfg *cfg)
{
    ssize_t ret = nvs_write(&fs, NVS_CONFIG_KEY, cfg, sizeof(*cfg));
    if (ret < 0)
    {
        my_log("NVS write failed: %d\n", ret);
    }else
    {
        my_log("NVS write OK\n");
    }
}

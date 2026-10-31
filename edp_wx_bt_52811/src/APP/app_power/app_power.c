#include "app_power.h"

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>
#include <hal/nrf_uarte.h>
#include "APP/app_include.h"

LOG_MODULE_REGISTER(app_power, CONFIG_LOG_DEFAULT_LEVEL);

/* ========================================================================== */
/*  未使用 GPIO 高阻态                                                         */
/* ========================================================================== */

/*
 * nRF52832 引脚分配:
 *   P0.31 - SYSLED
 *   其余未使用 → 高阻
 */

/* 已使用的引脚列表（不配置为输出低电平） */
static const uint8_t used_pins[] = {
    31, /* SYSLED */
    5,  /* P0.05 ADC AIN3 */
    20, /* P0.20 MPU6500 INT */
    21, /* P0.21 MPU6500 SDA */
    22, /* P0.22 MPU6500 SCL */
    23, /* P0.23 MPU6500 CS */
};

static bool is_pin_used(uint8_t pin)
{
    for (int i = 0; i < ARRAY_SIZE(used_pins); i++)
    {
        if (used_pins[i] == pin)
        {
            return true;
        }
    }
    return false;
}

static void configure_unused_gpio(void)
{
    const struct device *gpio0 = DEVICE_DT_GET(DT_NODELABEL(gpio0));
    if (!device_is_ready(gpio0))
    {
        my_logW("GPIO0 not ready");
        return;
    }

    int count = 0;
    int err;
    for (uint8_t pin = 0; pin < 32; pin++)
    {
        if (!is_pin_used(pin))
        {
            /* nRF52832 推荐：未使用引脚配置为输出低电平 */
            err = gpio_pin_configure(gpio0, pin, GPIO_OUTPUT_INACTIVE);
            if (err)
            {
                my_logW("Pin P0.%02d config failed: %d", pin, err);
            }
            else
            {
                count++;
                //my_log("  P0.%02d -> output low\n", pin);
            }
        }
    }
    my_log("GPIO: %d unused pins configured\n", count);
}

/* ========================================================================== */
/*  关闭未使用外设                                                             */
/* ========================================================================== */

static void disable_unused_peripherals(void)
{
    /* 关闭所有 UARTE 实例（已通过 prj.conf 禁用控制台，这里确保硬件关闭） */
#if DT_NODE_HAS_STATUS(DT_NODELABEL(uarte0), okay)
    nrf_uarte_disable(NRF_UARTE0);
#endif
#if DT_NODE_HAS_STATUS(DT_NODELABEL(uarte1), okay)
    nrf_uarte_disable(NRF_UARTE1);
#endif

    my_log("Unused peripherals disabled\n");
}

/* ========================================================================== */
/*  DCDC 模式确认                                                             */
/* ========================================================================== */

static void check_dcdc_mode(void)
{
    if (NRF_POWER->DCDCEN & POWER_DCDCEN_DCDCEN_Msk)
    {
        my_log("DCDC mode: enabled\n");
    }
    else
    {
        my_log("DCDC mode: disabled (using LDO)\n");
    }
}

/* ========================================================================== */
/*  定时低功耗睡眠                                                            */
/* ========================================================================== */

/*
 * nRF52832 低功耗方案:
 *
 * System OFF 模式 (~0.3µA) 无法定时唤醒 —— RTC 在 System OFF 下停止运行，
 * 只能通过 GPIO DETECT / RESET 引脚 / NFC 外部信号唤醒。
 *
 * 使用 System ON Sleep (WFE) 模式:
 *   - CPU 停止执行，进入低功耗状态 (~1-2µA)
 *   - RTC0 (Zephyr 系统定时器) 继续运行
 *   - RTC0 定时唤醒 CPU，k_sleep() 返回
 *   - RAM 内容保留，BLE 栈状态保留
 *
 */

void app_power_enter_deep_sleep(uint32_t ms)
{
#if _EN_WATCHDOG
    /* 喂狗 */
    app_power_feed_watchdog();
#endif

    my_log("Sleep start (%u ms)\n", ms);

    k_sleep(K_MSEC(ms));

    my_log("Sleep end\n");
}

/* ========================================================================== */
/*  公共 API                                                                  */
/* ========================================================================== */

void app_power_init(void)
{
    my_log("app_power_init\n");

    /* 1. 未使用 GPIO 切换至高阻态 */
    configure_unused_gpio();

    /* 2. 关闭未使用外设 */
    disable_unused_peripherals();

    /* 3. 检查 DCDC 模式 */
    check_dcdc_mode();

}

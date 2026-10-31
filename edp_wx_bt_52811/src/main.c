/* SPDX-License-Identifier: Apache-2.0 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <hal/nrf_power.h>

#include "battery/app_battery.h"
#include "ble/app_ble_epd.h"
#include "epd/epd_154d.h"
#include "led/app_led.h"
#include "power/app_power.h"

LOG_MODULE_REGISTER(main, CONFIG_LOG_DEFAULT_LEVEL);

int main(void)
{
	int err;
	uint32_t resetreas = NRF_POWER->RESETREAS;

	/* 记录本次启动原因（POWER/RESET 等位），用于区分异常复位重启
	 * 与日志阻塞/卡死：若 RTT 再次出现 boot 输出则说明设备复位过。
	 */
	NRF_POWER->RESETREAS = resetreas; /* 写 1 清除 */
	LOG_INF("RESETREAS=0x%08x", resetreas);

	err = app_led_init();
	if (err != 0) {
		LOG_ERR("LED init failed: %d", err);
		return err;
	}

	err = app_battery_init();
	if (err != 0) {
		LOG_ERR("Battery init failed: %d", err);
		return err;
	}

	/* 蓝牙上电即启动：不等屏幕自检刷新完成，设备一上电就能被发现/连接 */
	err = app_ble_epd_init();
	if (err != 0) {
		LOG_ERR("BLE init failed: %d", err);
		return err;
	}

	err = epd_154d_init();
	if (err != 0) {
		LOG_ERR("EPD init failed: %d", err);
		return err;
	}

	/* 上电自检图案：也是一次屏幕刷新，完成后 LED 快闪 3 下再熄灭 */
	err = epd_154d_show_test_pattern();
	if (err != 0) {
		LOG_ERR("EPD test pattern failed: %d", err);
		return err;
	}
	app_led_refresh_flash();

	err = app_power_init();
	if (err != 0) {
		LOG_ERR("Power init failed: %d", err);
		return err;
	}

	/* Stay resident: this PCB revision has no wake button, never enter
	 * System OFF. Idle in the kernel tickless sleep while advertising.
	 */
	while (true) {
		k_sleep(K_FOREVER);
	}
	return 0;
}

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(nrf52811_epd)

target_sources(app PRIVATE
    src/main.c
    src/battery/app_battery.c
    src/battery/battery_state.c
    src/ble/app_ble_epd.c
    src/epd/epd_154d.c
    src/epd/epd_154d_core.c
    src/led/app_led.c
    src/led/led_policy.c
    src/power/app_power.c
    src/power/power_policy.c
    src/protocol/epd_protocol.c
)

target_include_directories(app PRIVATE
    src
)

# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(power_logic_test)

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
target_sources(app PRIVATE
  src/main.c
  ../../src/battery/battery_state.c
  ../../src/led/led_policy.c
  ../../src/battery/app_battery.c
  ../../src/led/app_led.c
)

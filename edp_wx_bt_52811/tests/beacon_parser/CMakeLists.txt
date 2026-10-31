cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(beacon_parser_test)

target_include_directories(app PRIVATE ../../src)
target_sources(app PRIVATE
    src/main.c
    ../../src/APP/app_beacon/app_beacon_parser.c
)

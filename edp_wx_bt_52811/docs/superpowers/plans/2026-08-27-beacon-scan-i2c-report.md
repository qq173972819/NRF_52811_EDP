# nRF52832 Beacon Scan and I2C Report Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Scan Nordic-company iBeacon advertisements with UUID prefix `20250901`, transform each match into the ESP32S3 13-byte protocol, write it to I2C address `0x17`, and blink SYS-LED after a successful write.

**Architecture:** Keep byte-level iBeacon validation and frame construction in a portable parser module. A runtime module owns Zephyr Bluetooth scanning, a one-entry report queue, a reporting thread, I2C access, and LED indication so the Bluetooth callback never blocks on the bus.

**Tech Stack:** C, Zephyr/NCS v3.3.0, Zephyr Bluetooth Observer API, Nordic TWIM/I2C, devicetree/pinctrl, ztest-style portable C tests plus firmware build verification.

---

## File structure

- Create `src/APP/app_beacon/app_beacon_parser.h`: constants and parser API.
- Create `src/APP/app_beacon/app_beacon_parser.c`: manufacturer validation, UUID filtering, and frame construction only.
- Create `src/APP/app_beacon/app_beacon.h`: runtime initialization API.
- Create `src/APP/app_beacon/app_beacon.c`: Bluetooth scan callback, report queue/thread, I2C write, and LED blink.
- Create `tests/beacon_parser/src/main.c`: parser behavior tests using fixed byte arrays.
- Create `tests/beacon_parser/CMakeLists.txt`, `prj.conf`, and `testcase.yaml`: Zephyr test application metadata.
- Modify `boards/my_nrf52832/my_nrf52832-pinctrl.dtsi`: add I2C1 pins P0.02/P0.03.
- Modify `boards/my_nrf52832/my_nrf52832.dts`: enable I2C1 master and declare ESP32S3 at `0x17`.
- Modify `prj.conf`: enable Bluetooth Observer support and required stacks.
- Modify `CMakeLists.txt`: compile parser and runtime modules.
- Modify `src/main.c`: initialize LED and beacon runtime, then sleep.

### Task 1: Parser contract and failing behavior tests

**Files:**
- Create: `src/APP/app_beacon/app_beacon_parser.h`
- Create: `tests/beacon_parser/src/main.c`
- Create: `tests/beacon_parser/CMakeLists.txt`
- Create: `tests/beacon_parser/prj.conf`
- Create: `tests/beacon_parser/testcase.yaml`

- [ ] **Step 1: Declare the production parser contract**

Declare `bool app_beacon_parse_manufacturer(const uint8_t *data, size_t len, uint8_t frame[13]);` and constants for company `0x0059`, type `0x02`, payload length `0x15`, and frame length 13.

- [ ] **Step 2: Write failing tests**

Use a valid 25-byte manufacturer payload
`59 00 02 15 20 25 09 01 08 FC 04 7B 3C 11 FF 94 00 00 00 00 00 01 00 02 C5`.
Assert that the output equals
`0B AB 3C 11 FF 94 00 00 08 FC 04 7B BA`.
Add separate negative tests for company `0x004C`, type not `0x02`, payload-length byte not `0x15`, UUID prefix mismatch, and input shorter than 25 bytes.

- [ ] **Step 3: Verify RED**

Configure the test with the NCS toolchain. Expected result before `app_beacon_parser.c` exists: link failure for `app_beacon_parse_manufacturer`, proving the tests address the missing production behavior.

### Task 2: Minimal portable parser

**Files:**
- Create: `src/APP/app_beacon/app_beacon_parser.c`
- Modify: `tests/beacon_parser/CMakeLists.txt`

- [ ] **Step 1: Validate the Nordic iBeacon envelope**

Reject null pointers and buffers shorter than 25 bytes. Require bytes `[0..3]` to equal `59 00 02 15` and UUID bytes `[4..7]` to equal `20 25 09 01`.

- [ ] **Step 2: Construct the exact ESP frame**

Set `frame[0]=0x0B`, `frame[1]=0xAB`, copy manufacturer bytes `[12..17]` into frame `[2..7]`, copy manufacturer bytes `[8..11]` into frame `[8..11]`, and set `frame[12]=0xBA`.

- [ ] **Step 3: Verify GREEN**

Build/run the parser test when a native test runner is available; otherwise compile the test application and record the hardware-run limitation. Expected behavior is all six tests passing.

### Task 3: I2C1 master device tree

**Files:**
- Modify: `boards/my_nrf52832/my_nrf52832-pinctrl.dtsi`
- Modify: `boards/my_nrf52832/my_nrf52832.dts`

- [ ] **Step 1: Add I2C1 pinctrl states**

Define `i2c1_default` with `NRF_PSEL(TWIM_SDA, 0, 2)` and `NRF_PSEL(TWIM_SCL, 0, 3)`, plus a matching low-power `i2c1_sleep` state.

- [ ] **Step 2: Enable the controller and target node**

Configure `&i2c1` as `nordic,nrf-twim`, 100 kHz, using the new pinctrl states. Add label `esp32s3` at child address `0x17` with `compatible = "i2c-device"`.

- [ ] **Step 3: Verify generated device tree**

After the firmware build, inspect `build/scan_beacon_iic/zephyr/zephyr.dts` and confirm I2C1 uses pins 2/3 and the child register is `0x17`.

### Task 4: Bluetooth scan and queued I2C reporting

**Files:**
- Create: `src/APP/app_beacon/app_beacon.h`
- Create: `src/APP/app_beacon/app_beacon.c`

- [ ] **Step 1: Initialize the ESP32S3 I2C spec**

Use `I2C_DT_SPEC_GET(DT_NODELABEL(esp32s3))` and reject initialization when `i2c_is_ready_dt()` is false.

- [ ] **Step 2: Parse manufacturer AD data**

In the scan callback, call `bt_data_parse()`. For each `BT_DATA_MANUFACTURER_DATA` element, call the portable parser and enqueue only successful 13-byte frames.

- [ ] **Step 3: Keep the Bluetooth callback non-blocking**

Use a one-entry `k_msgq`. When full, remove the old item and enqueue the latest match. Do not call I2C or sleep from the scan callback.

- [ ] **Step 4: Send and indicate success**

A dedicated thread blocks on the queue, calls `i2c_write_dt()` for exactly 13 bytes, and logs errors. On success it turns SYS-LED on for 100 ms and then off.

- [ ] **Step 5: Start continuous passive scanning**

Call `bt_enable(NULL)` and `bt_le_scan_start(BT_LE_SCAN_PASSIVE, scan_callback)`. Return initialization errors to `main`.

### Task 5: Application integration and configuration

**Files:**
- Modify: `prj.conf`
- Modify: `CMakeLists.txt`
- Modify: `src/main.c`

- [ ] **Step 1: Enable Bluetooth Observer support**

Set `CONFIG_BT=y`, `CONFIG_BT_OBSERVER=y`, `CONFIG_BT_BROADCASTER=n`, `CONFIG_BT_PERIPHERAL=n`, and keep `CONFIG_I2C=y`. Size stacks only as required by the build.

- [ ] **Step 2: Add new sources**

Add `app_beacon_parser.c` and `app_beacon.c` to `target_sources(app PRIVATE ...)`.

- [ ] **Step 3: Replace the sensor polling main loop**

Initialize the LED, call `app_beacon_init()`, return its error on failure, and otherwise sleep forever with `k_sleep(K_FOREVER)`.

### Task 6: Full verification

**Files:**
- Inspect generated build outputs only.

- [ ] **Step 1: Build the board firmware**

Run the existing NCS CMake build for `my_nrf52832/nrf52832`. Expected result: no compiler, linker, Kconfig, or devicetree errors.

- [ ] **Step 2: Confirm configuration and protocol constants**

Inspect `.config`, generated devicetree, and source-level tests for Bluetooth Observer enablement, I2C1 pins/address, Nordic company ID, UUID prefix, and exact 13-byte frame.

- [ ] **Step 3: Hardware acceptance check**

With the example beacon present, expect SYS-LED to pulse and ESP32S3 to log
`ble recv:: 0b ab 3c 11 ff 94 00 00 08 fc 04 7b ba` followed by
`bspNo:: 3c 11 ff 94 00 00`.

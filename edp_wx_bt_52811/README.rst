.. zephyr:code-sample:: nrf52811_epd
   :name: nRF52811 E-Paper BLE Peripheral
   :relevant-api: bluetooth bluetooth_gatt

   Drive a 200x200 4-color e-paper display over Bluetooth LE.

Overview
********

This sample runs on the nRF52811 e-paper driver board. On power-up it:

* Initializes the LED status indicator (brief on then off).
* Refreshes a built-in 4-color test pattern (black / white / yellow / red),
  then fast-blinks the LED 3 times.
* Stays resident (no System OFF: this PCB revision has no wake button) and
  advertises a BLE peripheral with an e-paper transfer service.

BLE Service
***********

* Service UUID: ``62750001-d828-918d-fb46-b6c11c675aec`` (edp_html compatible)
* RX characteristic ``62750002-d828-918d-fb46-b6c11c675aec`` (write):
  protocol packets (see below)
* TX characteristic ``62750003-d828-918d-fb46-b6c11c675aec`` (notify + read):
  first byte of the read value is the firmware version; notify carries
  per-block ACK (``0xA0``) and status (``0xA1``) responses.

Wire protocol (mirrors the edp_html web page BLE transport):

* ``0x30`` plain image block: ``[0x30, cfg, payload...]``; ``cfg`` high
  nibble flags first block (``0x0`` first / ``0xF0`` continue), low nibble
  selects the layer.
* ``0x31`` CRC-protected block: ``[0x31, block_id(2, LE), total(2, LE), cfg,
  payload..., crc16(2, LE)]``; CRC16-CCITT (polynomial ``0x8408``, init
  ``0xFFFF``) over the payload only.
* ``0x32`` QUERY_STATUS: device replies ``0xA1`` + transfer statistics.
* ``0x33`` RESET_TRANSFER: aborts the in-progress transfer and clears state.
* ``0x05`` REFRESH: only executed when the image is complete; a failed or
  incomplete transfer is never refreshed.
* ``0x01``/``0x02``/``0x06``: init / clear / sleep.

Image data is fixed 200x200, 2bpp (10 000 bytes), color encoding:
black ``00``, white ``01``, yellow ``10``, red ``11``, MSB-first (4 pixels
per byte). The device validates the CRC before writing each block; blocks
that fail validation are dropped (ACK ``0xA0`` status ``0x02``) and the
transfer session continues.

LED states (non-blocking work item, never blocks BLE callbacks or SPI path):

* Power-on: on once then off.
* Boot test pattern done: fast blink x3 then off.
* BLE connected: steady on.
* Receiving data: slow blink.
* Screen refresh done: fast blink x3 then steady on (while connected).
* Disconnected: off.

Building and Running
********************

Build with the ``my_nrf52811`` board definition:

.. code-block:: console

   west build -b my_nrf52811

Flash and pair with the WeChat mini program ``wx_epaper_lite-master`` to send
images over BLE.

#include <zephyr/ztest.h>

#include <string.h>

#include "APP/app_beacon/app_beacon_parser.h"

static const uint8_t valid_manufacturer_data[APP_BEACON_MANUFACTURER_LENGTH] = {
    0x59, 0x00, 0x02, 0x15,
    0x20, 0x25, 0x09, 0x01, 0x08, 0xFC, 0x04, 0x7B,
    0x3C, 0x11, 0xFF, 0x94, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x02, 0xC5,
};

ZTEST(beacon_parser, test_builds_expected_esp32s3_frame)
{
    static const uint8_t expected[APP_BEACON_FRAME_LENGTH] = {
        0x0B, 0xAB, 0x3C, 0x11, 0xFF, 0x94, 0x00,
        0x00, 0x08, 0xFC, 0x04, 0x7B, 0xBA,
    };
    uint8_t frame[APP_BEACON_FRAME_LENGTH] = {0};

    zassert_true(app_beacon_parse_manufacturer(valid_manufacturer_data,
                                                sizeof(valid_manufacturer_data),
                                                frame));
    zassert_mem_equal(frame, expected, sizeof(expected));
}

ZTEST(beacon_parser, test_rejects_non_nordic_company)
{
    uint8_t data[sizeof(valid_manufacturer_data)];
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    memcpy(data, valid_manufacturer_data, sizeof(data));
    data[0] = 0x4C;
    zassert_false(app_beacon_parse_manufacturer(data, sizeof(data), frame));
}

ZTEST(beacon_parser, test_rejects_wrong_type)
{
    uint8_t data[sizeof(valid_manufacturer_data)];
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    memcpy(data, valid_manufacturer_data, sizeof(data));
    data[2] = 0x03;
    zassert_false(app_beacon_parse_manufacturer(data, sizeof(data), frame));
}

ZTEST(beacon_parser, test_rejects_wrong_payload_length)
{
    uint8_t data[sizeof(valid_manufacturer_data)];
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    memcpy(data, valid_manufacturer_data, sizeof(data));
    data[3] = 0x14;
    zassert_false(app_beacon_parse_manufacturer(data, sizeof(data), frame));
}

ZTEST(beacon_parser, test_rejects_wrong_uuid_prefix)
{
    uint8_t data[sizeof(valid_manufacturer_data)];
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    memcpy(data, valid_manufacturer_data, sizeof(data));
    data[7] = 0x02;
    zassert_false(app_beacon_parse_manufacturer(data, sizeof(data), frame));
}

ZTEST(beacon_parser, test_rejects_short_or_null_input)
{
    uint8_t frame[APP_BEACON_FRAME_LENGTH];

    zassert_false(app_beacon_parse_manufacturer(valid_manufacturer_data,
                                                sizeof(valid_manufacturer_data) - 1U,
                                                frame));
    zassert_false(app_beacon_parse_manufacturer(NULL,
                                                sizeof(valid_manufacturer_data),
                                                frame));
    zassert_false(app_beacon_parse_manufacturer(valid_manufacturer_data,
                                                sizeof(valid_manufacturer_data),
                                                NULL));
}

ZTEST_SUITE(beacon_parser, NULL, NULL, NULL, NULL, NULL);

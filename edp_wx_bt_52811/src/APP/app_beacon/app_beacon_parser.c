#include "app_beacon_parser.h"

#include <string.h>

static const uint8_t beacon_header[] = {
    (uint8_t)(APP_BEACON_COMPANY_ID & 0xFFU),
    (uint8_t)(APP_BEACON_COMPANY_ID >> 8),
    APP_BEACON_TYPE,
    APP_BEACON_PAYLOAD_LENGTH,
};

static const uint8_t uuid_prefix[] = {
    0x20, 0x25, 0x09, 0x01,
};

bool app_beacon_parse_manufacturer(const uint8_t *data, size_t len,
                                   uint8_t frame[APP_BEACON_FRAME_LENGTH])
{
    if (data == NULL || frame == NULL || len < APP_BEACON_MANUFACTURER_LENGTH) {
        return false;
    }

    if (memcmp(data, beacon_header, sizeof(beacon_header)) != 0 ||
        memcmp(&data[4], uuid_prefix, sizeof(uuid_prefix)) != 0) {
        return false;
    }

    frame[0] = 0x0B;
    frame[1] = 0xAB;
    memcpy(&frame[2], &data[12], 6);
    memcpy(&frame[8], &data[8], 4);
    frame[12] = 0xBA;

    return true;
}

#ifndef APP_BEACON_PARSER_H
#define APP_BEACON_PARSER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define APP_BEACON_COMPANY_ID 0x0059U
#define APP_BEACON_TYPE 0x02U
#define APP_BEACON_PAYLOAD_LENGTH 0x15U
#define APP_BEACON_MANUFACTURER_LENGTH 25U
#define APP_BEACON_FRAME_LENGTH 13U

bool app_beacon_parse_manufacturer(const uint8_t *data, size_t len,
                                   uint8_t frame[APP_BEACON_FRAME_LENGTH]);

#endif /* APP_BEACON_PARSER_H */

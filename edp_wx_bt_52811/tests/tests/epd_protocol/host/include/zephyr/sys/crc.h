/* SPDX-License-Identifier: Apache-2.0 */

#ifndef HOST_ZEPHYR_SYS_CRC_H_
#define HOST_ZEPHYR_SYS_CRC_H_

#include <stddef.h>
#include <stdint.h>

uint32_t crc32_ieee_update(uint32_t crc, const uint8_t *data, size_t len);

#endif /* HOST_ZEPHYR_SYS_CRC_H_ */

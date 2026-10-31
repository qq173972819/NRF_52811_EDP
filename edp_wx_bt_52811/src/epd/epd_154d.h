/* SPDX-License-Identifier: Apache-2.0 */

#ifndef EPD_154D_H_
#define EPD_154D_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EPD_154D_WIDTH 200
#define EPD_154D_HEIGHT 200
#define EPD_154D_FRAME_BYTES ((EPD_154D_WIDTH * EPD_154D_HEIGHT) / 4)

int epd_154d_init(void);
int epd_154d_show_test_pattern(void);
int epd_154d_begin_image(void);
int epd_154d_write_image(const uint8_t *data, size_t length);
int epd_154d_refresh(void);
int epd_154d_abort(void);

/* 屏幕是否被主线程占用（上电自检期间为 true），BLE 侧据此推迟写屏工作项 */
bool epd_154d_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* EPD_154D_H_ */

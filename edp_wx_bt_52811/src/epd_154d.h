#ifndef EPD_154D_H
#define EPD_154D_H

#include <stddef.h>
#include <stdint.h>

#define EPD_154D_WIDTH 200
#define EPD_154D_HEIGHT 200
#define EPD_154D_FRAME_BYTES ((EPD_154D_WIDTH * EPD_154D_HEIGHT) / 4)

int epd_154d_init(void);
int epd_154d_show_test_pattern(void);
int epd_154d_display(const uint8_t *frame, size_t length);
int epd_154d_sleep(void);

#endif

#ifndef __APP_BMP581_H__
#define __APP_BMP581_H__

#include <zephyr/kernel.h>

int app_bmp581_init(void);
int app_bmp581_read_pressure(float *pressure_pa, float *temperature_c);

#endif

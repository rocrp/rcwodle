#ifndef AHT20_H
#define AHT20_H

#include <stdint.h>

#define AHT20_ADDR       0x38
#define AHT20_I2C_BUS    "i2c3"

int aht20_init(void);
int aht20_read(float *temp_c, float *humi_pct);

#define AHT20_TEMP_OFFSET  -10.0f

#endif

#include "aht20.h"
#include <rtdevice.h>

static struct rt_i2c_bus_device *aht_bus = NULL;

static int aht_find_bus(void)
{
    if (!aht_bus) {
        aht_bus = (struct rt_i2c_bus_device *)rt_device_find(AHT20_I2C_BUS);
        if (!aht_bus) return -1;
    }
    return 0;
}

int aht20_init(void)
{
    if (aht_find_bus() < 0) return -1;

    uint8_t buf[2] = {0x08, 0x00};
    if (rt_i2c_mem_write(aht_bus, AHT20_ADDR, 0xBE, 8, buf, 2) != 2)
        return -1;

    rt_thread_mdelay(10);
    return 0;
}

int aht20_read(float *temp_c, float *humi_pct)
{
    if (aht_find_bus() < 0) return -1;

    /* Trigger measurement via mem_write (sends 0xAC 0x33 0x00) */
    uint8_t wbuf[2] = {0x33, 0x00};
    if (rt_i2c_mem_write(aht_bus, AHT20_ADDR, 0xAC, 8, wbuf, 2) != 2)
        return 1;

    rt_thread_mdelay(80);

    /* Pure read — no register address, AHT20 expects raw read */
    uint8_t rbuf[6];
    struct rt_i2c_msg msg = {
        .addr  = AHT20_ADDR,
        .flags = RT_I2C_RD,
        .len   = 6,
        .buf   = rbuf,
    };
    if (rt_i2c_transfer(aht_bus, &msg, 1) != 1)
        return 2;

    /* Bit[7]=0 means data ready; bit[7]=1 means still busy */
    if (rbuf[0] & 0x80)
        return 3;

    /* Bit[3]=1 means calibrated (OK); bit[3]=0 means NOT calibrated */
    if (!(rbuf[0] & 0x08)) {
        aht20_init();
        return 4;
    }

    uint32_t raw_h = ((uint32_t)rbuf[1] << 12) | ((uint32_t)rbuf[2] << 4) | (rbuf[3] >> 4);
    uint32_t raw_t = ((uint32_t)(rbuf[3] & 0x0F) << 16) | ((uint32_t)rbuf[4] << 8) | rbuf[5];

    *humi_pct = (float)raw_h * 100.0f / 1048576.0f;
    *temp_c   = (float)raw_t * 200.0f / 1048576.0f - 50.0f + AHT20_TEMP_OFFSET;

    return 0;
}

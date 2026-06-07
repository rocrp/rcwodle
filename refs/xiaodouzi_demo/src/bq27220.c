#include "bq27220.h"
#include <rtdevice.h>

static struct rt_i2c_bus_device *bq_bus = NULL;

static int bq_init(void)
{
    if (!bq_bus) {
        bq_bus = (struct rt_i2c_bus_device *)rt_device_find(BQ27220_I2C_BUS);
        if (!bq_bus) return -1;
    }
    return 0;
}

int bq27220_read_reg16(uint8_t reg, uint16_t *val)
{
    if (bq_init() < 0) return -1;
    uint8_t buf[2];
    if (rt_i2c_mem_read(bq_bus, BQ27220_ADDR, reg, 8, buf, 2) != 2)
        return -1;
    /* bq27220 returns LSB first */
    *val = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return 0;
}

int bq27220_read_voltage(uint16_t *mv)      { return bq27220_read_reg16(BQ27220_VOLTAGE, mv); }
int bq27220_read_temperature(uint16_t *k)   { return bq27220_read_reg16(BQ27220_TEMPERATURE, k); }
int bq27220_read_remaining(uint16_t *mAh)   { return bq27220_read_reg16(BQ27220_REMAINING, mAh); }
int bq27220_read_full(uint16_t *mAh)        { return bq27220_read_reg16(BQ27220_FULL, mAh); }
int bq27220_read_soh(uint16_t *pct)         { return bq27220_read_reg16(BQ27220_SOH, pct); }
int bq27220_read_ttf(uint16_t *minutes)     { return bq27220_read_reg16(BQ27220_TTF, minutes); }
int bq27220_read_tte(uint16_t *minutes)     { return bq27220_read_reg16(BQ27220_TTE, minutes); }
int bq27220_read_cycles(uint16_t *count)    { return bq27220_read_reg16(BQ27220_CYCLES, count); }
int bq27220_read_flags(uint16_t *flags)     { return bq27220_read_reg16(BQ27220_FLAGS, flags); }

int bq27220_read_current(int16_t *ma)
{
    uint16_t raw;
    int ret = bq27220_read_reg16(BQ27220_CURRENT, &raw);
    if (ret < 0) return ret;
    *ma = (int16_t)raw;
    return 0;
}

int bq27220_read_soc_percent(uint16_t *pct)
{
    if (bq_init() < 0) return -1;
    uint16_t rm, fcc;
    if (bq27220_read_reg16(BQ27220_REMAINING, &rm) < 0) return -1;
    if (bq27220_read_reg16(BQ27220_FULL, &fcc) < 0) return -1;
    if (fcc == 0) return -2;
    *pct = (uint16_t)(((uint32_t)rm * 100) / fcc);
    return 0;
}

int bq27220_write_reg16(uint8_t reg, uint16_t val)
{
    if (bq_init() < 0) return -1;
    uint8_t buf[2] = {(uint8_t)(val & 0xFF), (uint8_t)(val >> 8)};
    if (rt_i2c_mem_write(bq_bus, BQ27220_ADDR, reg, 8, buf, 2) != 2)
        return -1;
    return 0;
}

int bq27220_write_control(uint16_t subcmd)
{
    return bq27220_write_reg16(BQ27220_CONTROL, subcmd);
}

int bq27220_read_device_type(uint16_t *type)
{
    if (bq_init() < 0) return -1;
    if (bq27220_write_control(0x0001) < 0) return -1;
    uint8_t buf[2];
    if (rt_i2c_mem_read(bq_bus, BQ27220_ADDR, BQ27220_CONTROL, 8, buf, 2) != 2)
        return -1;
    *type = (uint16_t)buf[0] | ((uint16_t)buf[1] << 8);
    return 0;
}

int bq27220_read_design_capacity(uint16_t *mAh)
{
    return bq27220_read_reg16(BQ27220_DESIGN_CAPACITY, mAh);
}

int bq27220_write_design_capacity(uint16_t mAh)
{
    return bq27220_write_reg16(BQ27220_DESIGN_CAPACITY, mAh);
}

int bq27220_unseal(void)
{
    if (bq27220_write_control(BQ27220_CTRL_UNSEAL_KEY1) < 0) return -1;
    rt_thread_mdelay(2);
    if (bq27220_write_control(BQ27220_CTRL_UNSEAL_KEY2) < 0) return -1;
    return 0;
}

int bq27220_seal(void)
{
    return bq27220_write_control(BQ27220_SUBCMD_SEAL);
}

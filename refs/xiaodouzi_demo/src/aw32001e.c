#include "aw32001e.h"
#include <rtdevice.h>

static struct rt_i2c_bus_device *aw_bus = NULL;

static int aw_init(void)
{
    if (!aw_bus) {
        aw_bus = (struct rt_i2c_bus_device *)rt_device_find(AW32001E_I2C_BUS);
        if (!aw_bus) return -1;
    }
    return 0;
}

int aw32001e_read_reg(uint8_t reg, uint8_t *val)
{
    if (aw_init() < 0) return -1;

    if (rt_i2c_mem_read(aw_bus, AW32001E_ADDR, reg, 8, val, 1) != 1)
        return -1;

    return 0;
}

int aw32001e_write_reg(uint8_t reg, uint8_t val)
{
    if (aw_init() < 0) return -1;

    if (rt_i2c_mem_write(aw_bus, AW32001E_ADDR, reg, 8, &val, 1) != 1)
        return -1;

    return 0;
}

int aw32001e_read_chipid(uint8_t *id)
{
    return aw32001e_read_reg(AW32001E_REG0A, id);
}

int aw32001e_read_status(uint8_t *chg_stat, uint8_t *pg_stat, uint8_t *therm_stat)
{
    uint8_t val;
    int ret = aw32001e_read_reg(AW32001E_REG08, &val);
    if (ret < 0) return ret;

    if (chg_stat)   *chg_stat   = (val >> 3) & 3;
    if (pg_stat)    *pg_stat    = (val >> 1) & 1;
    if (therm_stat) *therm_stat = val & 1;

    return 0;
}

int aw32001e_read_fault(uint8_t *fault)
{
    return aw32001e_read_reg(AW32001E_REG09, fault);
}

int aw32001e_set_charge_enable(int enable)
{
    uint8_t val;
    int ret = aw32001e_read_reg(AW32001E_REG01, &val);
    if (ret < 0) return ret;

    if (enable)
        val &= ~(1 << 3);
    else
        val |= (1 << 3);

    return aw32001e_write_reg(AW32001E_REG01, val);
}

int aw32001e_set_charge_current(uint16_t ma)
{
    if (ma < 8) ma = 8;
    if (ma > 512) ma = 512;
    uint8_t val = ((ma - 8) / 8) & 0x3F;
    uint8_t cur;
    int ret = aw32001e_read_reg(AW32001E_REG02, &cur);
    if (ret < 0) return ret;
    cur = (cur & 0xC0) | val;
    return aw32001e_write_reg(AW32001E_REG02, cur);
}

int aw32001e_set_charge_voltage(uint16_t mv)
{
    if (mv < 3600) mv = 3600;
    if (mv > 4545) mv = 4545;
    uint8_t val = ((mv - 3600) / 15) & 0x3F;
    uint8_t cur;
    int ret = aw32001e_read_reg(AW32001E_REG04, &cur);
    if (ret < 0) return ret;
    cur = (cur & 0x03) | (val << 2);
    return aw32001e_write_reg(AW32001E_REG04, cur);
}

int aw32001e_set_vsys(uint16_t mv)
{
    if (mv < 4200) mv = 4200;
    if (mv > 4950) mv = 4950;
    uint8_t val = ((mv - 4200) / 50) & 0x0F;
    uint8_t cur;
    int ret = aw32001e_read_reg(AW32001E_REG07, &cur);
    if (ret < 0) return ret;
    cur = (cur & 0xF0) | val;
    return aw32001e_write_reg(AW32001E_REG07, cur);
}

/* WODLE-PORT: see WodleBattery.h. */
#include "WodleBattery.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "bf0_hal.h"

#define BQ27220_ADDR 0x55
#define REG_VOLTAGE 0x08 /* mV */
#define REG_SOC 0x2C     /* % */

namespace
{
struct rt_i2c_bus_device *s_bus;
bool s_available;

bool readWord(uint8_t reg, uint16_t &value)
{
    if (!s_bus) return false;
    uint8_t buf[2] = {0, 0};
    struct rt_i2c_msg msgs[2];
    msgs[0].addr = BQ27220_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1;
    msgs[1].addr = BQ27220_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buf;
    msgs[1].len = 2;
    if (rt_i2c_transfer(s_bus, msgs, 2) != 2) return false;
    value = (uint16_t)(buf[0] | (buf[1] << 8)); /* little-endian per TI spec */
    return true;
}
} // namespace

namespace WodleBattery
{

void init()
{
    /* wodle wiring: I2C2 on PA31/PA32 (DevKit bsp muxes these elsewhere) */
    HAL_PIN_Set(PAD_PA31, I2C2_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA32, I2C2_SDA, PIN_PULLUP, 1);

    s_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if (!s_bus)
    {
        rt_kprintf("[WodleBattery] i2c2 not found\n");
        return;
    }

    uint16_t v = 0;
    s_available = readWord(REG_VOLTAGE, v) && v > 2000 && v < 5000;
    rt_kprintf("[WodleBattery] gauge %s (voltage=%umV)\n",
               s_available ? "OK" : "not responding", v);
}

bool available() { return s_available; }

int percent()
{
    uint16_t soc = 0;
    if (!s_available || !readWord(REG_SOC, soc)) return -1;
    return soc > 100 ? 100 : (int)soc;
}

int millivolts()
{
    uint16_t mv = 0;
    if (!s_available || !readWord(REG_VOLTAGE, mv)) return -1;
    return mv;
}

} // namespace WodleBattery

#ifndef AW32001E_H
#define AW32001E_H

#include <stdint.h>

#define AW32001E_ADDR         0x49
#define AW32001E_I2C_BUS      "i2c3"

#define AW32001E_REG00        0x00
#define AW32001E_REG01        0x01
#define AW32001E_REG02        0x02
#define AW32001E_REG03        0x03
#define AW32001E_REG04        0x04
#define AW32001E_REG05        0x05
#define AW32001E_REG06        0x06
#define AW32001E_REG07        0x07
#define AW32001E_REG08        0x08
#define AW32001E_REG09        0x09
#define AW32001E_REG0A        0x0A
#define AW32001E_REG0B        0x0B
#define AW32001E_REG0C        0x0C
#define AW32001E_REG22        0x22

#define AW32001E_CHG_STAT_NOT 0
#define AW32001E_CHG_STAT_PRE 1
#define AW32001E_CHG_STAT_FAST 2
#define AW32001E_CHG_STAT_DONE 3

int aw32001e_read_reg(uint8_t reg, uint8_t *val);
int aw32001e_write_reg(uint8_t reg, uint8_t val);
int aw32001e_read_chipid(uint8_t *id);
int aw32001e_read_status(uint8_t *chg_stat, uint8_t *pg_stat, uint8_t *therm_stat);
int aw32001e_read_fault(uint8_t *fault);
int aw32001e_set_charge_enable(int enable);
int aw32001e_set_charge_current(uint16_t ma);
int aw32001e_set_charge_voltage(uint16_t mv);
int aw32001e_set_vsys(uint16_t mv);

#endif

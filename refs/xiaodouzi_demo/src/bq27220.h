#ifndef BQ27220_H
#define BQ27220_H

#include <stdint.h>

#define BQ27220_ADDR         0x55
#define BQ27220_I2C_BUS      "i2c3"

/* Standard Commands per TI TRM SLUUBD4 */
#define BQ27220_CONTROL      0x00
#define BQ27220_ATRATE       0x02
#define BQ27220_ARTTE        0x04
#define BQ27220_TEMPERATURE  0x06
#define BQ27220_VOLTAGE      0x08
#define BQ27220_FLAGS        0x0A
#define BQ27220_CURRENT      0x0C
#define BQ27220_AVGCURRENT   0x0E
#define BQ27220_REMAINING    0x10
#define BQ27220_FULL         0x12
#define BQ27220_TTE          0x16
#define BQ27220_TTF          0x1A
#define BQ27220_SOH          0x1C
#define BQ27220_CYCLES       0x1E

int bq27220_read_reg16(uint8_t reg, uint16_t *val);
int bq27220_read_voltage(uint16_t *mv);
int bq27220_read_current(int16_t *ma);
int bq27220_read_temperature(uint16_t *kelvin_x10);
int bq27220_read_remaining(uint16_t *mAh);
int bq27220_read_full(uint16_t *mAh);
int bq27220_read_flags(uint16_t *flags);
int bq27220_read_soh(uint16_t *pct);
int bq27220_read_ttf(uint16_t *minutes);
int bq27220_read_tte(uint16_t *minutes);
int bq27220_read_cycles(uint16_t *count);
int bq27220_read_soc_percent(uint16_t *pct);
int bq27220_read_device_type(uint16_t *type);
int bq27220_read_design_capacity(uint16_t *mAh);
int bq27220_write_design_capacity(uint16_t mAh);
int bq27220_unseal(void);
int bq27220_seal(void);
int bq27220_write_control(uint16_t subcmd);
int bq27220_write_reg16(uint8_t reg, uint16_t val);

/* Control subcommands */
#define BQ27220_SUBCMD_SET_CFGUPDATE   0x0090
#define BQ27220_SUBCMD_EXIT_CFGUPDATE  0x0092
#define BQ27220_SUBCMD_SOFT_RESET      0x0042
#define BQ27220_SUBCMD_SEAL            0x0030
#define BQ27220_CTRL_UNSEAL_KEY1       0x0414
#define BQ27220_CTRL_UNSEAL_KEY2       0x3672

/* Design Capacity register */
#define BQ27220_DESIGN_CAPACITY 0x3C

#endif

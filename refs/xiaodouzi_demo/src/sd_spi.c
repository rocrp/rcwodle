#include "sd_spi.h"
#include "bf0_hal.h"
#include "rtthread.h"
#include "rtdevice.h"
#include <string.h>

#define SD_MOSI 24
#define SD_MISO 25
#define SD_CLK  28
#define SD_CS   29

static int sd_hc;
static uint32_t sd_sec_cnt;

static void sd_mosi(uint8_t v) { rt_pin_write(SD_MOSI, v ? PIN_HIGH : PIN_LOW); }
static void sd_clk(uint8_t v)  { rt_pin_write(SD_CLK, v ? PIN_HIGH : PIN_LOW); }
static void sd_cs_lo(void)     { rt_pin_write(SD_CS, PIN_LOW); }
static void sd_cs_hi(void)     { rt_pin_write(SD_CS, PIN_HIGH); }
static uint8_t sd_miso(void)   { return rt_pin_read(SD_MISO); }

static uint8_t spi_byte(uint8_t b)
{
    uint8_t r = 0;
    for (int i = 7; i >= 0; i--) {
        sd_mosi((b >> i) & 1);
        sd_clk(PIN_HIGH);
        r = (r << 1) | sd_miso();
        sd_clk(PIN_LOW);
    }
    return r;
}

static void spi_byte_out(uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        sd_mosi((b >> i) & 1);
        sd_clk(PIN_HIGH);
        sd_clk(PIN_LOW);
    }
}

static void spi_clocks(int n)
{
    for (int i = 0; i < n; i++) {
        sd_mosi(PIN_HIGH);
        sd_clk(PIN_HIGH);
        sd_clk(PIN_LOW);
    }
}

static uint8_t sd_cmd_raw(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    spi_byte_out(0x40 | cmd);
    spi_byte_out((uint8_t)(arg >> 24));
    spi_byte_out((uint8_t)(arg >> 16));
    spi_byte_out((uint8_t)(arg >> 8));
    spi_byte_out((uint8_t)(arg));
    spi_byte_out(crc ? crc : (cmd == 0 ? 0x95 : 0x01));

    uint8_t r1 = 0xFF;
    uint32_t deadline = HAL_GetTick() + 2000;
    for (int i = 0; i < 5000; i++) {
        r1 = spi_byte(0xFF);
        if (!(r1 & 0x80)) break;
        if (HAL_GetTick() >= deadline) break;
    }
    return r1;
}

static uint8_t sd_cmd(uint8_t cmd, uint32_t arg, uint8_t crc)
{
    sd_cs_lo();
    uint8_t r1 = sd_cmd_raw(cmd, arg, crc);
    sd_cs_hi();
    return r1;
}

int sd_init(void)
{
    HAL_PIN_Set(PAD_PA24, GPIO_A24, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA25, GPIO_A25, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA28, GPIO_A28, PIN_NOPULL, 1);
    HAL_PIN_Set(PAD_PA29, GPIO_A29, PIN_NOPULL, 1);

    rt_pin_mode(SD_MOSI, PIN_MODE_OUTPUT);
    rt_pin_mode(SD_CLK, PIN_MODE_OUTPUT);
    rt_pin_mode(SD_CS, PIN_MODE_OUTPUT);
    rt_pin_mode(SD_MISO, PIN_MODE_INPUT_PULLUP);

    sd_cs_hi();
    sd_mosi(PIN_HIGH);
    sd_clk(PIN_HIGH);

    spi_clocks(80);

    uint8_t r1 = sd_cmd(0, 0, 0x95);
    if (r1 != 0x01) return -1;

    r1 = sd_cmd(8, 0x000001AA, 0x87);
    if (r1 != 0x01) return -2;
    uint8_t r7[4];
    sd_cs_lo();
    for (int i = 0; i < 4; i++) r7[i] = spi_byte(0xFF);
    sd_cs_hi();
    spi_clocks(2);
    if (r7[3] != 0xAA || r7[2] != 0x01) return -2;

    int ok = 0;
    for (int i = 0; i < 500; i++) {
        sd_cmd(55, 0, 0x01);
        r1 = sd_cmd(41, 0x40000000, 0x01);
        if (r1 == 0x00) { ok = 1; break; }
        if (r1 != 0x01) break;
        rt_thread_mdelay(10);
    }
    if (!ok) return -3;

    r1 = sd_cmd(58, 0, 0x01);
    if (r1 != 0x00) return -4;
    sd_cs_lo();
    for (int i = 0; i < 4; i++) r7[i] = spi_byte(0xFF);
    sd_cs_hi();
    spi_clocks(2);
    sd_hc = (r7[0] & 0x40) ? 1 : 0;

    r1 = sd_cmd(16, 512, 0x01);
    if (r1 != 0x00) return -5;

    sd_sec_cnt = 60000000;
    return 0;
}

int sd_read_sector(uint32_t lba, uint8_t *buf)
{
    uint32_t addr = sd_hc ? lba : lba * 512;

    sd_cs_lo();
    spi_clocks(2);
    uint8_t r1 = sd_cmd_raw(17, addr, 0x01);
    if (r1 != 0x00) { sd_cs_hi(); return -1; }

    uint32_t deadline = HAL_GetTick() + 2000;
    uint8_t rx;
    int found = 0;
    while (HAL_GetTick() < deadline) {
        rx = spi_byte(0xFF);
        if (rx == 0xFE) { found = 1; break; }
    }
    if (!found) { sd_cs_hi(); return -1; }

    for (int i = 0; i < 512; i++)
        buf[i] = spi_byte(0xFF);
    spi_byte(0xFF);
    spi_byte(0xFF);
    sd_cs_hi();
    spi_clocks(2);
    return 0;
}

int sd_write_sector(uint32_t lba, const uint8_t *buf)
{
    uint32_t addr = sd_hc ? lba : lba * 512;

    sd_cs_lo();
    spi_clocks(2);
    uint8_t r1 = sd_cmd_raw(24, addr, 0x01);
    if (r1 != 0x00) { sd_cs_hi(); return -1; }

    spi_byte_out(0xFE);
    for (int i = 0; i < 512; i++) spi_byte_out(buf[i]);
    spi_byte_out(0xFF);
    spi_byte_out(0xFF);

    uint8_t dr;
    uint32_t deadline = HAL_GetTick() + 1000;
    while (HAL_GetTick() < deadline) {
        dr = spi_byte(0xFF);
        if (dr != 0xFF) break;
    }
    if ((dr & 0x1F) != 0x05) { sd_cs_hi(); return -1; }

    deadline = HAL_GetTick() + 2000;
    while (HAL_GetTick() < deadline) {
        dr = spi_byte(0xFF);
        if (dr == 0xFF) break;
    }
    sd_cs_hi();
    spi_clocks(2);
    return 0;
}

int sd_is_hc(void) { return sd_hc; }
uint32_t sd_sector_count(void) { return sd_sec_cnt; }

int sd_config_save(uint8_t val)
{
    if (sd_sec_cnt < 2) return -1;
    uint8_t buf[512];
    memset(buf, 0, sizeof(buf));
    buf[0] = val;
    buf[1] = 0xCF;
    return sd_write_sector(sd_sec_cnt - 1, buf);
}

int sd_config_load(uint8_t *val)
{
    if (sd_sec_cnt < 2) return -1;
    uint8_t buf[512];
    if (sd_read_sector(sd_sec_cnt - 1, buf) < 0) return -1;
    if (buf[1] != 0xCF) return -1;
    *val = buf[0];
    return 0;
}
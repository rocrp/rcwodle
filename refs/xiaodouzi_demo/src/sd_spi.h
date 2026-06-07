#ifndef SD_SPI_H
#define SD_SPI_H

#include <stdint.h>

/* Initialize SPI1 and SD card (CMD0-16). Returns 0 on success. */
int sd_init(void);

/* Read one sector (512 bytes). Returns 0 on success, -1 on error. */
int sd_read_sector(uint32_t lba, uint8_t *buf);

/* Write one sector (512 bytes). Returns 0 on success, -1 on error. */
int sd_write_sector(uint32_t lba, const uint8_t *buf);

/* Returns 1 if card is SDHC/SDXC, 0 if SDSC. */
int sd_is_hc(void);

/* Returns approximate sector count (~32GB default). */
uint32_t sd_sector_count(void);

/* Save/restore 1-byte config to a dedicated sector (last sector on card). */
int sd_config_save(uint8_t val);
int sd_config_load(uint8_t *val);

#endif

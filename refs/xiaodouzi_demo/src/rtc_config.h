#ifndef RTC_CONFIG_H
#define RTC_CONFIG_H

#include <stdint.h>

void rtc_config_init(void);
void rtc_config_save(uint8_t val);
uint8_t rtc_config_load(void);
void rtc_read_time(uint8_t *hour, uint8_t *min, uint8_t *sec);

#endif
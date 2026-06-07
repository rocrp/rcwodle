#include "rtc_config.h"
#include "bf0_hal.h"

#define BKP_IDX    1
#define MAGIC      0xCF000000u

static RTC_HandleTypeDef hrtc;

void rtc_config_init(void)
{
    hrtc.Instance = hwp_rtc;
    hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
    hrtc.Init.DivAInt = 0x80;
    hrtc.Init.DivAFrac = 0;
    hrtc.Init.DivB = 0x100;

    HAL_RTC_Init(&hrtc, RTC_INIT_SKIP);
    hwp_rtc->CR |= RTC_CR_BKP;
}

void rtc_config_save(uint8_t val)
{
    HAL_RTC_set_backup(&hrtc, BKP_IDX, val | MAGIC);
}

uint8_t rtc_config_load(void)
{
    uint32_t d = HAL_RTC_get_backup(&hrtc, BKP_IDX);
    if ((d & 0xFF000000u) == MAGIC)
        return (uint8_t)(d & 0xFFu);
    return 0xFF;
}

void rtc_read_time(uint8_t *hour, uint8_t *min, uint8_t *sec)
{
    RTC_TimeTypeDef tm;
    HAL_RTC_GetTime(&hrtc, &tm, RTC_FORMAT_BIN);
    if (hour) *hour = tm.Hours;
    if (min)  *min  = tm.Minutes;
    if (sec)  *sec  = tm.Seconds;
}
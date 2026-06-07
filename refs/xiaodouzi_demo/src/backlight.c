#include "backlight.h"
#include "bf0_hal.h"

static int brightness = 50;

void backlight_init(void)
{
    /* GPTIM1 clock */
    hwp_hpsys_rcc->ENR1 |= HPSYS_RCC_ENR1_GPTIM1;
    hwp_hpsys_rcc->RSTR1 &= ~HPSYS_RCC_RSTR1_GPTIM1;
    HAL_Delay_us(10);

    /* 1KHz PWM: 48MHz / 48 = 1MHz, 1MHz / 1000 = 1KHz */
    hwp_gptim1->PSC   = 48 - 1;
    hwp_gptim1->ARR   = 1000 - 1;
    hwp_gptim1->CCR4  = 500;
    hwp_gptim1->CCMR2 = (6 << GPT_CCMR2_OC4M_Pos);
    hwp_gptim1->CCER |= GPT_CCER_CC4E;
    hwp_gptim1->CR1  |= GPT_CR1_CEN;

    /* PA1 = GPTIM1_CH4 */
    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4, PIN_NOPULL, 1);
}

void backlight_set(int percent)
{
    if (percent < 0)   percent = 0;
    if (percent > 100) percent = 100;
    brightness = percent;

    uint32_t period = 1000 - 1;
    uint32_t pulse  = (period + 1) * percent / 100;
    if (pulse > period) pulse = period;

    hwp_gptim1->ARR  = period;
    hwp_gptim1->CCR4 = pulse;

    if (percent > 0)
        hwp_gptim1->CCER |= GPT_CCER_CC4E;
    else
        hwp_gptim1->CCER &= ~GPT_CCER_CC4E;
}

int backlight_get(void)
{
    return brightness;
}

/* WODLE-PORT: see WodleFrontlight.h. Mirrors hello_wodle's HIL-proven path. */
#include "WodleFrontlight.h"

#include <rtthread.h>

#include "bf0_hal.h"

#define BL_FREQ_HZ 100000 /* vendor LCD_PWM_BACKLIGHT period: 10us */

namespace
{
GPT_HandleTypeDef s_tim;
uint32_t s_counts = 480;
bool s_ready = false;
} // namespace

namespace WodleFrontlight
{

void init()
{
    HAL_RCC_EnableModule(RCC_MOD_GPTIM1);

    uint32_t pclk = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1);
    s_counts = pclk ? (pclk / BL_FREQ_HZ) : 480;

    s_tim.Instance = GPTIM1;
    s_tim.core = CORE_ID_HCPU;
    s_tim.Init.Prescaler = 0;
    s_tim.Init.CounterMode = GPT_COUNTERMODE_UP;
    s_tim.Init.Period = s_counts - 1;
    if (HAL_GPT_Base_Init(&s_tim) != HAL_OK) return;

    GPT_OC_InitTypeDef oc = {0};
    oc.OCMode = GPT_OCMODE_PWM1;
    oc.Pulse = s_counts / 2;
    oc.OCPolarity = GPT_OCPOLARITY_HIGH;
    oc.OCFastMode = GPT_OCFAST_DISABLE;
    if (HAL_GPT_PWM_ConfigChannel(&s_tim, &oc, GPT_CHANNEL_4) != HAL_OK) return;
    s_ready = true;
}

void set(uint8_t percent)
{
    if (!s_ready) return;
    if (percent == 0)
    {
        HAL_GPT_PWM_Stop(&s_tim, GPT_CHANNEL_4);
        return;
    }
    if (percent > 100) percent = 100;
    __HAL_GPT_SET_COMPARE(&s_tim, GPT_CHANNEL_4, (s_counts * percent) / 100);
    HAL_GPT_PWM_Start(&s_tim, GPT_CHANNEL_4);
}

void pulse(int ms)
{
    set(50);
    rt_thread_mdelay(ms);
    set(0);
}

} // namespace WodleFrontlight

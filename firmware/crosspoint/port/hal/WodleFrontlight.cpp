/* WODLE-PORT: see WodleFrontlight.h. Mirrors hello_wodle's HIL-proven path. */
#include "WodleFrontlight.h"

#include <rtthread.h>

#include "FrontlightLevel.h"
#include "HalStorage.h"
#include "bf0_hal.h"

/* WODLE-PORT (spi_epd_demo): 5kHz, matching the demo's hardware-tuned pair
 * (200us period + 50..100%% duty floor below). Earlier 100kHz worked at the
 * single HIL-proven point (50%% duty) but the brightness curve was never
 * characterized; the demo's config is.
 *
 * HIL knob (refs/xiaodouzi_demo, second hardware-proven pair): 1kHz PWM with
 * 30..70%% duty — at the lower frequency the boost lights from ~30%% duty,
 * i.e. a visibly DIMMER minimum than our 50%% floor allows. If HIL wants a
 * darker night-reading floor, try BL_FREQ_HZ=1000 + floor 30. */
#define BL_FREQ_HZ 5000
#define LEVEL_FILE "/.crosspoint/frontlight"

namespace
{
GPT_HandleTypeDef s_tim;
uint32_t s_counts = 480;
bool s_ready = false;
int s_level = 0;

void persist()
{
    if (!Storage.ready()) return;
    Storage.ensureDirectoryExists("/.crosspoint");
    Storage.writeFile(LEVEL_FILE, String(s_level));
}
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
    s_level = percent > 100 ? 100 : percent;
    if (percent == 0)
    {
        HAL_GPT_PWM_Stop(&s_tim, GPT_CHANNEL_4);
        return;
    }
    /* WODLE-PORT (spi_epd_demo MIN_VISIBLE_DUTY): the VBAT boost only lights
     * at >=~50%% duty — map level 1..100 onto duty 50..100 so every UI step
     * is actually visible (linear mapping left levels under ~45 dark). */
    const uint32_t duty = 50u + ((50u * s_level + 50u) / 100u);
    __HAL_GPT_SET_COMPARE(&s_tim, GPT_CHANNEL_4, (s_counts * duty) / 100u);
    HAL_GPT_PWM_Start(&s_tim, GPT_CHANNEL_4);
}

void pulse(int ms)
{
    int prev = s_level;
    set(50);
    rt_thread_mdelay(ms);
    set((uint8_t)prev);
}

int level() { return s_level; }

void setPersisted(uint8_t pct)
{
    set((uint8_t)FrontlightLevel::clamp(pct));
    persist();
}

void stepUp()
{
    set((uint8_t)FrontlightLevel::up(s_level));
    persist();
}

void stepDown()
{
    set((uint8_t)FrontlightLevel::down(s_level));
    persist();
}

void restorePersisted()
{
    if (!Storage.ready() || !Storage.exists(LEVEL_FILE)) return;
    String v = Storage.readFile(LEVEL_FILE);
    int lvl = FrontlightLevel::clamp((int)v.toInt());
    set((uint8_t)lvl);
    rt_kprintf("[WodleFrontlight] restored level %d%%\n", lvl);
}

} // namespace WodleFrontlight

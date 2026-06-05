/* hello_wodle S2 — capability validation app.
 * Output channel = the e-paper console (UART is signal-compromised, no USB).
 * Frontlight driven via direct HAL GPTIM1 ch4 PWM (rt_pwm framework set()
 * fails through the ROM-linked layer; HAL bypass). Light itself still not
 * observed on hardware — under investigation, values printed on screen. */

#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"
#include "epd.h"
#include "epd_console.h"

#define PIN_PWR_EN  10  /* PA10 system power-enable latch */

/* ---------------------------------------------------------------- frontlight */
#define BL_FREQ_HZ  100000 /* vendor LCD_PWM_BACKLIGHT period: 10us */

static GPT_HandleTypeDef bl_tim;
static uint32_t bl_pclk;
static int bl_rc_init = -1, bl_rc_cfg = -1, bl_rc_start = -1;

static void bl_init(void)
{
    HAL_RCC_EnableModule(RCC_MOD_GPTIM1);

    bl_pclk = HAL_RCC_GetPCLKFreq(CORE_ID_HCPU, 1);
    uint32_t counts = bl_pclk ? (bl_pclk / BL_FREQ_HZ) : 480;

    bl_tim.Instance = GPTIM1;
    bl_tim.core = CORE_ID_HCPU;
    bl_tim.Init.Prescaler = 0;
    bl_tim.Init.CounterMode = GPT_COUNTERMODE_UP;
    bl_tim.Init.Period = counts - 1;
    bl_rc_init = HAL_GPT_Base_Init(&bl_tim);

    GPT_OC_InitTypeDef oc = {0};
    oc.OCMode = GPT_OCMODE_PWM1;
    oc.Pulse = counts / 2;
    oc.OCPolarity = GPT_OCPOLARITY_HIGH;
    oc.OCFastMode = GPT_OCFAST_DISABLE;
    bl_rc_cfg = HAL_GPT_PWM_ConfigChannel(&bl_tim, &oc, GPT_CHANNEL_4);

    bl_rc_start = HAL_GPT_PWM_Start(&bl_tim, GPT_CHANNEL_4);
    rt_thread_mdelay(500);
    HAL_GPT_PWM_Stop(&bl_tim, GPT_CHANNEL_4);
}

static void bl_blink_ms(int ms)
{
    HAL_GPT_PWM_Start(&bl_tim, GPT_CHANNEL_4);
    rt_thread_mdelay(ms);
    HAL_GPT_PWM_Stop(&bl_tim, GPT_CHANNEL_4);
}

/* ----------------------------------------------------------------------- app */
int main(void)
{
    rt_kprintf("\n[hello_wodle] S2 boot: %s %s\n", __DATE__, __TIME__);

    HAL_PIN_Set(PAD_PA10, GPIO_A10, PIN_NOPULL, 1);
    rt_pin_mode(PIN_PWR_EN, PIN_MODE_OUTPUT);
    rt_pin_write(PIN_PWR_EN, PIN_HIGH);

    epd_hw_init();
    bl_init();

    epd_console_init();
    epd_console_printf("hello wodle S2");
    epd_console_printf("build %s %s", __DATE__, __TIME__);
    epd_console_printf("");
    epd_console_printf("EPD console: OK");
    epd_console_printf("pclk: %u Hz", (unsigned)bl_pclk);
    epd_console_printf("GPT init/cfg/start: %d/%d/%d",
                       bl_rc_init, bl_rc_cfg, bl_rc_start);
    epd_console_printf("frontlight: 1s blink loop");
    epd_console_printf("");
    epd_console_printf("next: keys / touch / pages");
    epd_console_flush();
    rt_kprintf("[hello_wodle] console flushed\n");

    while (1)
    {
        bl_blink_ms(1000);
        rt_thread_mdelay(1000);
    }
    return 0;
}

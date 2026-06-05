#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

/* wodle pin/peripheral map, from refs/schematic/README.md + board.conf.
 * The frontlight on PA1 is a PWM-boost backlight (pwmt1 ch4) — a raw GPIO level
 * won't light it, so we drive it through the PWM device. */
#define WODLE_PWR_EN_PIN    10            /* PA10 system power-enable latch */
#define WODLE_BL_PWM_DEV    "pwmt1"
#define WODLE_BL_PWM_CH     4             /* LCD_PWM_BACKLIGHT_CHANEL_NUM */
#define WODLE_BL_PERIOD_NS  1000000       /* 1 kHz carrier */

static volatile rt_uint32_t s_hb;

/* MSH: `alive` — report liveness over the finsh console (UART1) */
static void cmd_alive(int argc, char **argv)
{
    rt_kprintf("alive: %u heartbeats, main loop running\n", s_hb);
}
MSH_CMD_EXPORT(cmd_alive, wodle report liveness);

int main(void)
{
    rt_kprintf("\n[hello_wodle] S1 blinky boot: %s %s\n", __DATE__, __TIME__);

    /* Hold the power latch first (PA10 is a plain GPIO on wodle). */
    rt_pin_mode(WODLE_PWR_EN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(WODLE_PWR_EN_PIN, PIN_HIGH);
    rt_kprintf("[hello_wodle] PWR_EN(PA10) high\n");

    /* Frontlight via the PWM-boost backlight (the naked-eye proof-of-life). */
    struct rt_device_pwm *bl = (struct rt_device_pwm *)rt_device_find(WODLE_BL_PWM_DEV);
    if (bl == RT_NULL)
        rt_kprintf("[hello_wodle] WARN: pwm device '%s' not found\n", WODLE_BL_PWM_DEV);
    else
        rt_kprintf("[hello_wodle] backlight on %s ch%d\n", WODLE_BL_PWM_DEV, WODLE_BL_PWM_CH);

    rt_bool_t on = RT_FALSE;
    while (1)
    {
        on = !on;
        if (bl)
        {
            /* full brightness vs off — alternate for a clear blink */
            rt_pwm_set(bl, WODLE_BL_PWM_CH, WODLE_BL_PERIOD_NS, on ? WODLE_BL_PERIOD_NS : 0);
            rt_pwm_enable(bl, WODLE_BL_PWM_CH);
        }
        if (on)
        {
            s_hb++;
            rt_kprintf("[hello_wodle] hb %u (backlight ON)\n", s_hb);
        }
        rt_thread_mdelay(500);
    }
    return 0;
}

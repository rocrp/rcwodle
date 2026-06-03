#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

/* wodle pin map (PAxx == pad index), from refs/schematic/README.md */
#define WODLE_PWR_EN_PIN   10   /* PA10 system power-enable latch (assert high to stay on) */
#define WODLE_BL_PWM_PIN    1   /* PA1  frontlight (driven as GPIO here; re-muxes off the SDK PWM) */

static volatile rt_uint32_t s_heartbeats;

/* MSH: `alive` — confirm the shell is live and report the heartbeat count (for a UART tap) */
static void cmd_alive(int argc, char **argv)
{
    rt_kprintf("alive: %u heartbeats, main loop running\n", s_heartbeats);
}
MSH_CMD_EXPORT(cmd_alive, wodle report liveness);

int main(void)
{
    rt_kprintf("\n[hello_wodle] S1 boot: %s %s\n", __DATE__, __TIME__);

    /* 1) Hold the power latch FIRST — a soft-power device may self-off otherwise.
     *    PA10 is GPIO on wodle (the SDK LCD subsystem does not touch it). */
    rt_pin_mode(WODLE_PWR_EN_PIN, PIN_MODE_OUTPUT);
    rt_pin_write(WODLE_PWR_EN_PIN, PIN_HIGH);
    rt_kprintf("[hello_wodle] PWR_EN(PA10) asserted high\n");

    /* 2) Frontlight as the naked-eye proof-of-life. rt_pin_mode re-muxes PA1 from the
     *    SDK's auto-init backlight PWM to plain GPIO, so this toggle wins. */
    rt_pin_mode(WODLE_BL_PWM_PIN, PIN_MODE_OUTPUT);

    rt_bool_t on = RT_FALSE;
    while (1)
    {
        on = !on;
        rt_pin_write(WODLE_BL_PWM_PIN, on ? PIN_HIGH : PIN_LOW);
        if (on)
        {
            s_heartbeats++;
            /* Second, independent proof-of-life channel for a UART observer (PA18/19). */
            rt_kprintf("[hello_wodle] hb %u\n", s_heartbeats);
        }
        rt_thread_mdelay(500);
    }
    return 0;
}

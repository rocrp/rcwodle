#include "wodle_frontlight.h"

#include "bf0_hal.h"
#include "rtdevice.h"

#define FRONTLIGHT_CHANNEL 4
#define FRONTLIGHT_PERIOD_NS 200000
#define FRONTLIGHT_MIN_VISIBLE_DUTY 50
#define FRONTLIGHT_MAX_DUTY 100

static int s_level;
static int s_duty;

static int clamp_level(int level)
{
    if (level < 0)
    {
        return 0;
    }

    if (level > 100)
    {
        return 100;
    }

    return level;
}

static int level_to_duty(int level)
{
    level = clamp_level(level);
    if (level == 0)
    {
        return 0;
    }

    return FRONTLIGHT_MIN_VISIBLE_DUTY +
           (((FRONTLIGHT_MAX_DUTY - FRONTLIGHT_MIN_VISIBLE_DUTY) * level + 50) / 100);
}

rt_err_t wodle_frontlight_init(void)
{
    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4, PIN_NOPULL, 1);
    s_level = 0;
    s_duty = 0;
    return wodle_frontlight_set_level(0);
}

rt_err_t wodle_frontlight_set_level(int level)
{
    struct rt_device_pwm *pwm;
    rt_uint32_t pulse;
    rt_err_t err;

    HAL_PIN_Set(PAD_PA01, GPTIM1_CH4, PIN_NOPULL, 1);
    pwm = (struct rt_device_pwm *)rt_device_find("pwmt1");
    if (!pwm)
    {
        return -RT_ERROR;
    }

    s_level = clamp_level(level);
    s_duty = level_to_duty(s_level);
    pulse = (rt_uint32_t)((FRONTLIGHT_PERIOD_NS * s_duty) / 100);

    err = rt_pwm_set(pwm, FRONTLIGHT_CHANNEL, FRONTLIGHT_PERIOD_NS, pulse);
    if (err != RT_EOK)
    {
        return err;
    }

    if (s_duty == 0)
    {
        return rt_pwm_disable(pwm, FRONTLIGHT_CHANNEL);
    }

    return rt_pwm_enable(pwm, FRONTLIGHT_CHANNEL);
}

int wodle_frontlight_get_level(void)
{
    return s_level;
}

int wodle_frontlight_get_duty(void)
{
    return s_duty;
}

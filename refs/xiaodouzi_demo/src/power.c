#include "power.h"
#include "backlight.h"
#include "rtthread.h"
#include "bf0_hal.h"
#include "bf0_pm.h"
#include "bsp_board.h"

void power_sleep(void)
{
    rt_thread_mdelay(1000);
    backlight_set(0);

    /* Configure PA34 (PWRKEY) as wakeup pin */
    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, 34));
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_HIGH);
    hwp_pmuc->WKUP_CNT = 0x000F000F;

    /* Pull unused pins low */
    for (uint32_t i = PAD_PA24; i <= PAD_PA44; i++)
        HAL_PIN_Set(i, (pin_function)(i - PAD_PA24 + GPIO_A24), PIN_PULLDOWN, 1);

    /* Disable LDOs */
    hwp_pmuc->PERI_LDO &= ~(PMUC_PERI_LDO_EN_LDO18 |
                             PMUC_PERI_LDO_EN_VDD33_LDO2 |
                             PMUC_PERI_LDO_EN_VDD33_LDO3);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO2_3V3, false, false);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, false, false);

    __disable_irq();
    HAL_PMU_EnterHibernate();
}

int power_wakeup_reason(void)
{
    /* 0 = cold boot, 1 = hibernate wake */
    if (SystemPowerOnModeGet() == PM_HIBERNATE_BOOT ||
        SystemPowerOnModeGet() == PM_SHUTDOWN_BOOT)
        return 1;
    return 0;
}

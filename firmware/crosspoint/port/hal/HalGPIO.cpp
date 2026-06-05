/* WODLE-PORT: 3-key input backend (PA34 power, PA43 KEY2, PA44 KEY3).
 * Debounced polling in update(); edge events latched per frame, matching the
 * upstream InputManager semantics (wasPressed = edge since previous update). */

#include "HalGPIO.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "bf0_hal.h"
#include "WodleFrontlight.h"

HalGPIO gpio;

#define PIN_PWR 34
#define PIN_KEY2 43
#define PIN_KEY3 44

#define NUM_BTNS 7
#define DEBOUNCE_MS 20
#define CHORD_WINDOW_MS 50
#define PWR_SHORT_MAX_MS 800

namespace
{
struct RawKey
{
    int pin;
    bool stable = false; /* debounced pressed state (true = pressed) */
    bool lastRaw = false;
    unsigned long lastChangeMs = 0;
    unsigned long pressedAtMs = 0;
};

RawKey s_pwr{PIN_PWR}, s_key2{PIN_KEY2}, s_key3{PIN_KEY3};

bool s_isPressed[NUM_BTNS];
bool s_wasPressed[NUM_BTNS];
bool s_wasReleased[NUM_BTNS];
unsigned long s_heldStartMs = 0;

bool readRaw(const RawKey &k)
{
    /* active-low assumption (pull-up + switch to GND) — HIL checkpoint */
    return rt_pin_read(k.pin) == PIN_LOW;
}

/* returns true on debounced state change */
bool debounce(RawKey &k, unsigned long now)
{
    bool raw = readRaw(k);
    if (raw != k.lastRaw)
    {
        k.lastRaw = raw;
        k.lastChangeMs = now;
        return false;
    }
    if (raw != k.stable && now - k.lastChangeMs >= DEBOUNCE_MS)
    {
        k.stable = raw;
        if (raw) k.pressedAtMs = now;
        return true;
    }
    return false;
}
} // namespace

void HalGPIO::begin()
{
    HAL_PIN_Set(PAD_PA34, GPIO_A34, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA43, GPIO_A43, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA44, GPIO_A44, PIN_PULLUP, 1);
    rt_pin_mode(PIN_PWR, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_KEY2, PIN_MODE_INPUT_PULLUP);
    rt_pin_mode(PIN_KEY3, PIN_MODE_INPUT_PULLUP);
}

void HalGPIO::update()
{
    unsigned long now = millis();
    for (int i = 0; i < NUM_BTNS; i++)
    {
        s_wasPressed[i] = false;
        s_wasReleased[i] = false;
    }

    bool k2Changed = debounce(s_key2, now);
    bool k3Changed = debounce(s_key3, now);
    bool pwrChanged = debounce(s_pwr, now);

    /* chord: both nav keys held -> BACK */
    bool chord = s_key2.stable && s_key3.stable;
    bool wasChord = s_isPressed[BTN_BACK];

    if (chord && !wasChord)
        s_wasPressed[BTN_BACK] = true;
    if (!chord && wasChord)
        s_wasReleased[BTN_BACK] = true;
    s_isPressed[BTN_BACK] = chord;

    auto mapKey = [&](RawKey &k, bool changed, uint8_t btn) {
        bool pressed = k.stable && !chord;
        if (changed && k.stable && !chord) s_wasPressed[btn] = true;
        if (changed && !k.stable && s_isPressed[btn]) s_wasReleased[btn] = true;
        s_isPressed[btn] = pressed;
    };
    mapKey(s_key2, k2Changed, BTN_DOWN);
    mapKey(s_key3, k3Changed, BTN_UP);

    /* power: short release -> CONFIRM edge; long hold reported live on
     * BTN_POWER so the main loop's hold-to-sleep logic works */
    s_isPressed[BTN_POWER] = s_pwr.stable;
    if (pwrChanged && !s_pwr.stable)
    {
        unsigned long held = now - s_pwr.pressedAtMs;
        if (held < PWR_SHORT_MAX_MS)
            s_wasPressed[BTN_CONFIRM] = true;
        s_wasReleased[BTN_POWER] = true;
    }
    if (pwrChanged && s_pwr.stable)
        s_wasPressed[BTN_POWER] = true;

    bool anyHeld = s_key2.stable || s_key3.stable || s_pwr.stable;
    if (anyHeld && s_heldStartMs == 0) s_heldStartMs = now;
    if (!anyHeld) s_heldStartMs = 0;
}

bool HalGPIO::isPressed(uint8_t b) const { return b < NUM_BTNS && s_isPressed[b]; }
bool HalGPIO::wasPressed(uint8_t b) const { return b < NUM_BTNS && s_wasPressed[b]; }
bool HalGPIO::wasReleased(uint8_t b) const { return b < NUM_BTNS && s_wasReleased[b]; }

bool HalGPIO::wasAnyPressed() const
{
    for (bool v : s_wasPressed)
        if (v) return true;
    return false;
}

bool HalGPIO::wasAnyReleased() const
{
    for (bool v : s_wasReleased)
        if (v) return true;
    return false;
}

unsigned long HalGPIO::getHeldTime() const
{
    return s_heldStartMs ? millis() - s_heldStartMs : 0;
}

unsigned long HalGPIO::getPowerButtonHeldTime() const
{
    return s_pwr.stable ? millis() - s_pwr.pressedAtMs : 0;
}

void HalGPIO::startDeepSleep()
{
    /* SF32LB52x hibernate per SDK example/pm/classical (52x branch): PA34
     * (power key) -> wake_pin0. NEG_EDGE wake is polarity-robust: a full
     * press-release cycle produces both edges regardless of active level.
     * Wake = chip reset -> normal boot. */
    rt_kprintf("[HalGPIO] entering hibernate (wake: PA34 edge)\n");
    WodleFrontlight::set(0);
    rt_thread_mdelay(20); /* let the log out */

    HAL_PMU_SelectWakeupPin(0, HAL_HPAON_QueryWakeupPin(hwp_gpio1, PIN_PWR));
    HAL_PMU_EnablePinWakeup(0, AON_PIN_MODE_NEG_EDGE);
    hwp_pmuc->WKUP_CNT = 0x50005; /* debounce counts for wake pins 0/1 */
    rt_hw_interrupt_disable();
    /* SDK example is C; the enum needs explicit casts under C++ */
    HAL_PMU_ConfigPeriLdo((PMU_PeriLdoTypeDef)PMUC_PERI_LDO_EN_VDD33_LDO3_Pos, false, false);
    HAL_PMU_ConfigPeriLdo((PMU_PeriLdoTypeDef)PMUC_PERI_LDO_EN_VDD33_LDO2_Pos, false, false);
    HAL_PMU_ConfigPeriLdo(PMU_PERI_LDO_1V8, false, false);
    HAL_PMU_EnterHibernate();
    while (1)
        ;
}

void HalGPIO::verifyPowerButtonWakeup(uint16_t, bool)
{
    /* wake verification is a deep-sleep concern; no-op until hibernate lands */
}

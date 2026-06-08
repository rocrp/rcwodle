/* WODLE-PORT: HalGPIO for the wodle's 3 physical keys + power key.
 * API mirrors upstream lib/hal/HalGPIO.h (7 logical buttons). Mapping:
 *
 *   KEY2 (PA43, active-HIGH) -> BTN_DOWN  (page forward / move down)
 *   KEY3 (PA44, active-HIGH) -> BTN_UP    (page back / move up)
 *   KEY2+KEY3 chord          -> BTN_BACK
 *   PWR  (PA34) short press  -> BTN_CONFIRM
 *   PWR  (PA34) long press   -> BTN_POWER (sleep; held-time exposed)
 *
 * ALL THREE keys are active-high with pulldowns. KEY2/KEY3 per the
 * vendor-quality spi_epd_demo; PWR per four agreeing sources (was assumed
 * active-low): refs/xiaodouzi_demo reads PA34 with GPIO_PULLDOWN and treats
 * raw==1 as pressed, arms hibernate wake on AON_PIN_MODE_HIGH, the wodle
 * schematic's stock pad config for PA34 is PD, and the sibling keys are
 * proven active-high. begin() also latches PWR_EN (PA10) high: without it
 * the device powers off on battery once the boot button press is released. */
#pragma once

#include <Arduino.h>

#include "WodleBattery.h"

class HalGPIO
{
public:
    enum class DeviceType : uint8_t
    {
        X4,
        X3
    };

    HalGPIO() = default;

    /* The wodle panel geometry matches the X3 (792x528), and the X3 paths in
     * GfxRenderer/activities are the correct ones (portrait 528 logical). */
    inline bool deviceIsX3() const { return true; }
    inline bool deviceIsX4() const { return false; }

    void begin();
    void update();
    bool isPressed(uint8_t buttonIndex) const;
    bool wasPressed(uint8_t buttonIndex) const;
    bool wasAnyPressed() const;
    bool wasReleased(uint8_t buttonIndex) const;
    bool wasAnyReleased() const;
    unsigned long getHeldTime() const;

    /* WODLE-PORT: raw tap coordinate delivery, additive to the zone->button
     * synthesis (a tap STILL produces its button edge). Edge semantics matching
     * wasPressed/wasReleased: returns true at most once per physical tap
     * (clear-on-read) and writes the tap's logical-portrait (x,y) into the out
     * params; returns false (leaving x/y untouched) when no tap is pending.
     * Coordinates are logical portrait (x in [0,528), y in [0,792)); mapping for
     * non-portrait orientations (the reader can rotate) is a later concern for
     * the caller. Foundation for direct tap-to-select. */
    bool consumeTap(int& x, int& y);
    unsigned long getPowerButtonHeldTime() const;

    void startDeepSleep();
    void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

    /* USB presence from the AW32001 charger's PG_STAT (1s-cached poll);
     * drives the battery-icon charging bolt + plug/unplug repaints. */
    bool isUsbConnected() const { return WodleBattery::usbPowered(); }
    bool wasUsbStateChanged() const { return WodleBattery::usbStateChanged(); }

    enum class WakeupReason
    {
        PowerButton,
        AfterFlash,
        AfterUSBPower,
        Other
    };
    /* PMU wakeup-status register: PIN0 (PA34, the pin startDeepSleep arms)
     * set -> woke from hibernate via the power button; cold boot/reset ->
     * Other. AfterUSBPower mapping is DELIBERATELY deferred even though USB
     * detect now works (AW32001 PG_STAT): a post-sftool-flash boot also has
     * VBUS present and would be misclassified -> insta-sleep after every
     * flash, killing the recovery loop. Needs a HIL-verified reset-cause
     * signature to tell "USB plug woke us" from "reset with USB attached". */
    WakeupReason getWakeupReason() const;

    static constexpr uint8_t BTN_BACK = 0;
    static constexpr uint8_t BTN_CONFIRM = 1;
    static constexpr uint8_t BTN_LEFT = 2;
    static constexpr uint8_t BTN_RIGHT = 3;
    static constexpr uint8_t BTN_UP = 4;
    static constexpr uint8_t BTN_DOWN = 5;
    static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;

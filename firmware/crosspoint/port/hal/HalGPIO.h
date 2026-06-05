/* WODLE-PORT: HalGPIO for the wodle's 3 physical keys + power key.
 * API mirrors upstream lib/hal/HalGPIO.h (7 logical buttons). Mapping:
 *
 *   KEY2 (PA43, active-low)  -> BTN_DOWN  (page forward / move down)
 *   KEY3 (PA44, active-low)  -> BTN_UP    (page back / move up)
 *   KEY2+KEY3 chord          -> BTN_BACK
 *   PWR  (PA34) short press  -> BTN_CONFIRM
 *   PWR  (PA34) long press   -> BTN_POWER (sleep; held-time exposed)
 *
 * LEFT/RIGHT stay unmapped until CST836U touch lands. Polarities are a HIL
 * checkpoint (assumed active-low with pull-ups). */
#pragma once

#include <Arduino.h>

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
    unsigned long getPowerButtonHeldTime() const;

    void startDeepSleep();
    void verifyPowerButtonWakeup(uint16_t requiredDurationMs, bool shortPressAllowed);

    bool isUsbConnected() const { return false; } /* TODO(HIL): VBUS via PMIC */
    bool wasUsbStateChanged() const { return false; }

    enum class WakeupReason
    {
        PowerButton,
        AfterFlash,
        AfterUSBPower,
        Other
    };
    WakeupReason getWakeupReason() const { return WakeupReason::Other; }

    static constexpr uint8_t BTN_BACK = 0;
    static constexpr uint8_t BTN_CONFIRM = 1;
    static constexpr uint8_t BTN_LEFT = 2;
    static constexpr uint8_t BTN_RIGHT = 3;
    static constexpr uint8_t BTN_UP = 4;
    static constexpr uint8_t BTN_DOWN = 5;
    static constexpr uint8_t BTN_POWER = 6;
};

extern HalGPIO gpio;

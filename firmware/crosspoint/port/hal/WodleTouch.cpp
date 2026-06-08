/* WODLE-PORT: see WodleTouch.h. */
#include "WodleTouch.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "HalGPIO.h"
#include "TapClassifier.h"
#include "WodleFrontlight.h"
#include "bf0_hal.h"

#define TOUCH_ADDR 0x15
#define REG_GESTURE 0x01 /* gesture, fingers, xH, xL, yH, yL */

#define PIN_TP_INT 42

/* orientation flags — HIL checkpoint */
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 0

/* keep the pure header's button ids honest */
static_assert(TapClassifier::BTN_BACK == HalGPIO::BTN_BACK);
static_assert(TapClassifier::BTN_CONFIRM == HalGPIO::BTN_CONFIRM);
static_assert(TapClassifier::BTN_UP == HalGPIO::BTN_UP);
static_assert(TapClassifier::BTN_DOWN == HalGPIO::BTN_DOWN);

#define SCREEN_W TapClassifier::SCREEN_W
#define SCREEN_H TapClassifier::SCREEN_H

namespace
{
struct rt_i2c_bus_device *s_bus;
bool s_available;

bool s_touching;
unsigned long s_downAtMs;
int s_downX, s_downY;
int s_lastX, s_lastY;
int s_holdBtn = -1; /* zone button of a declared stationary hold */

bool readTouch(bool &touching, int &x, int &y)
{
    if (!s_bus) return false;
    uint8_t reg = REG_GESTURE;
    uint8_t buf[6] = {0};
    struct rt_i2c_msg msgs[2];
    msgs[0].addr = TOUCH_ADDR;
    msgs[0].flags = RT_I2C_WR;
    msgs[0].buf = &reg;
    msgs[0].len = 1;
    msgs[1].addr = TOUCH_ADDR;
    msgs[1].flags = RT_I2C_RD;
    msgs[1].buf = buf;
    msgs[1].len = sizeof(buf);
    if (rt_i2c_transfer(s_bus, msgs, 2) != 2) return false;

    uint8_t fingers = buf[1] & 0x0F;
    touching = fingers > 0;
    int rawX = ((buf[2] & 0x0F) << 8) | buf[3];
    int rawY = ((buf[4] & 0x0F) << 8) | buf[5];
#if TOUCH_SWAP_XY
    int t = rawX;
    rawX = rawY;
    rawY = t;
#endif
#if TOUCH_MIRROR_X
    rawX = SCREEN_W - 1 - rawX;
#endif
#if TOUCH_MIRROR_Y
    rawY = SCREEN_H - 1 - rawY;
#endif
    x = rawX;
    y = rawY;
    return true;
}

} // namespace

namespace WodleTouch
{

void init()
{
    /* wodle wiring: I2C1 on PA7/PA8 (lcd_base bsp muxes these to LCDC DIO2/3,
     * which the 2-lane EPD doesn't use) */
    HAL_PIN_Set(PAD_PA07, I2C1_SCL, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA08, I2C1_SDA, PIN_PULLUP, 1);
    HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_PULLUP, 1);
    rt_pin_mode(PIN_TP_INT, PIN_MODE_INPUT_PULLUP);

    s_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c1");
    if (!s_bus)
    {
        rt_kprintf("[WodleTouch] i2c1 not found\n");
        return;
    }

    /* Probe a few times — the controller may not answer the very first read
     * right after power-up. The result is only a boot signal: poll() reads the
     * I2C status register directly every cycle (like the AHT20/battery do), so
     * touch works even if this probe misses. */
    bool touching;
    int x, y;
    bool probed = false;
    for (int i = 0; i < 5 && !probed; i++)
    {
        probed = readTouch(touching, x, y);
        if (!probed) rt_thread_mdelay(5);
    }
    s_available = true; /* bus is present; poll regardless of the probe result */
    rt_kprintf("[WodleTouch] CST836U %s\n", probed ? "OK" : "not responding (polling anyway)");
}

bool available() { return s_available; }

Frame poll()
{
    Frame frame;
    if (!s_bus) return frame;

    /* The CST836U only PULSES INT on a falling edge — it does not hold it low
     * for the whole touch — so a synchronous main-loop poll almost never
     * catches INT low (this is why touch appeared dead). Read the I2C status
     * register every cycle instead; the `fingers` byte is authoritative.
     * Cost: one ~6-byte I2C read per loop, negligible. */
    bool touching;
    int x, y;
    if (!readTouch(touching, x, y)) return frame;

    unsigned long now = rt_tick_get_millisecond();
    if (touching)
    {
        if (!s_touching)
        {
            s_touching = true;
            s_downAtMs = now;
            s_downX = x;
            s_downY = y;
            s_holdBtn = -1;
        }
        s_lastX = x;
        s_lastY = y;

        /* Stationary past the tap window -> the zone button goes held.
         * Latched until lift: movement after declaration doesn't cancel. */
        if (s_holdBtn < 0 && TapClassifier::isHold(now - s_downAtMs, s_lastX - s_downX, s_lastY - s_downY))
        {
            s_holdBtn = TapClassifier::zoneButton(s_downX, s_downY);
        }
        if (s_holdBtn >= 0)
        {
            frame.holdButton = s_holdBtn;
            frame.holdStartMs = s_downAtMs;
            /* WODLE-PORT: expose the hold's touch-down coords (logical portrait)
             * alongside the synthesized button — additive, button path unchanged */
            frame.holdX = s_downX;
            frame.holdY = s_downY;
        }
        return frame;
    }

    if (!s_touching) return frame;
    s_touching = false;

    if (s_holdBtn >= 0)
    {
        /* A declared hold consumes the touch: release the held button and
         * suppress tap/swipe classification. */
        frame.holdReleased = true;
        s_holdBtn = -1;
        return frame;
    }

    /* finger lifted: classify the full gesture */
    using TapClassifier::Gesture;
    Gesture g = TapClassifier::classify(now - s_downAtMs, s_lastX - s_downX, s_lastY - s_downY);
    switch (g)
    {
    case Gesture::Tap:
        frame.tapButton = TapClassifier::zoneButton(s_downX, s_downY);
        /* WODLE-PORT: also expose the raw tap coords (logical portrait) so an
         * activity can do direct tap-to-select; the zone->button synthesis above
         * is untouched, so button navigation keeps working as before. */
        frame.tapX = s_downX;
        frame.tapY = s_downY;
        break;
    case Gesture::SwipeLeft:
    case Gesture::SwipeRight:
        frame.tapButton = TapClassifier::swipeButton(g); /* page turns */
        break;
    case Gesture::SwipeUp:
        WodleFrontlight::stepUp();
        break;
    case Gesture::SwipeDown:
        WodleFrontlight::stepDown();
        break;
    default:
        break;
    }
    return frame;
}

} // namespace WodleTouch

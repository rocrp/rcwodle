/* WODLE-PORT: see WodleTouch.h. */
#include "WodleTouch.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "HalGPIO.h"
#include "bf0_hal.h"

#define TOUCH_ADDR 0x15
#define REG_GESTURE 0x01 /* gesture, fingers, xH, xL, yH, yL */

#define PIN_TP_INT 42

/* orientation flags — HIL checkpoint */
#define TOUCH_SWAP_XY 0
#define TOUCH_MIRROR_X 0
#define TOUCH_MIRROR_Y 0

#define SCREEN_W 528
#define SCREEN_H 792
#define TOP_STRIP_PX 96
#define TAP_MAX_MS 400
#define TAP_MAX_MOVE 40

namespace
{
struct rt_i2c_bus_device *s_bus;
bool s_available;

bool s_touching;
unsigned long s_downAtMs;
int s_downX, s_downY;
int s_lastX, s_lastY;

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

int zoneButton(int x, int y)
{
    if (y < TOP_STRIP_PX) return HalGPIO::BTN_BACK;
    if (x < SCREEN_W / 3) return HalGPIO::BTN_UP;
    if (x > 2 * SCREEN_W / 3) return HalGPIO::BTN_DOWN;
    return HalGPIO::BTN_CONFIRM;
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

    bool touching;
    int x, y;
    s_available = readTouch(touching, x, y);
    rt_kprintf("[WodleTouch] CST836U %s\n", s_available ? "OK" : "not responding");
}

bool available() { return s_available; }

int pollTapButton()
{
    if (!s_available) return -1;

    /* INT idles high, pulses/holds low while a finger is down. Skip the I2C
     * read when idle and no tap is in flight. */
    if (!s_touching && rt_pin_read(PIN_TP_INT) == PIN_HIGH) return -1;

    bool touching;
    int x, y;
    if (!readTouch(touching, x, y)) return -1;

    unsigned long now = rt_tick_get_millisecond();
    if (touching)
    {
        if (!s_touching)
        {
            s_touching = true;
            s_downAtMs = now;
            s_downX = x;
            s_downY = y;
        }
        s_lastX = x;
        s_lastY = y;
        return -1;
    }

    if (!s_touching) return -1;
    s_touching = false;

    /* finger lifted: tap = short + little movement */
    unsigned long held = now - s_downAtMs;
    int dx = s_lastX - s_downX, dy = s_lastY - s_downY;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    if (held > TAP_MAX_MS || dx > TAP_MAX_MOVE || dy > TAP_MAX_MOVE) return -1;

    return zoneButton(s_downX, s_downY);
}

} // namespace WodleTouch

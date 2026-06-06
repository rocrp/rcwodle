/* WODLE-PORT: see WodleAht20.h. */
#include "WodleAht20.h"

#include <rtdevice.h>
#include <rtthread.h>

#include "Aht20Codec.h"

#define AHT20_ADDR 0x38
#define MEASURE_DELAY_MS 85   /* datasheet: >=80ms conversion */
#define REFRESH_MS 30000      /* cache lifetime between re-measurements */

namespace
{
struct rt_i2c_bus_device *s_bus;
bool s_available;

/* Trigger/collect state machine — read() never blocks. */
bool s_pending;
unsigned long s_triggeredAt;
bool s_cacheValid;
Aht20Codec::Reading s_cache;
unsigned long s_cachedAt;

bool writeBytes(const uint8_t *data, int len)
{
    struct rt_i2c_msg msg;
    msg.addr = AHT20_ADDR;
    msg.flags = RT_I2C_WR;
    msg.buf = (rt_uint8_t *)data;
    msg.len = len;
    return rt_i2c_transfer(s_bus, &msg, 1) == 1;
}

bool readBytes(uint8_t *data, int len)
{
    struct rt_i2c_msg msg;
    msg.addr = AHT20_ADDR;
    msg.flags = RT_I2C_RD;
    msg.buf = data;
    msg.len = len;
    return rt_i2c_transfer(s_bus, &msg, 1) == 1;
}

bool trigger()
{
    static const uint8_t cmd[3] = {0xAC, 0x33, 0x00};
    if (!writeBytes(cmd, sizeof(cmd))) return false;
    s_pending = true;
    s_triggeredAt = rt_tick_get_millisecond();
    return true;
}
} // namespace

namespace WodleAht20
{

void init()
{
    /* I2C2 pinmux is owned by WodleBattery::init(); this just attaches. */
    s_bus = (struct rt_i2c_bus_device *)rt_device_find("i2c2");
    if (!s_bus)
    {
        rt_kprintf("[WodleAht20] i2c2 not found\n");
        return;
    }

    /* Probe: a plain 1-byte read returns the status register. The boot path
     * is well past the 40ms power-on window by the time we run (the
     * frontlight proof-of-life pulse alone is 300ms). */
    uint8_t status = 0;
    if (!readBytes(&status, 1))
    {
        rt_kprintf("[WodleAht20] not responding\n");
        return;
    }

    if (!(status & Aht20Codec::STATUS_CALIBRATED))
    {
        /* Factory-fresh part: load calibration. 10ms settle per datasheet —
         * the only blocking wait, and only on the first boot of a new unit. */
        static const uint8_t initCmd[3] = {0xBE, 0x08, 0x00};
        writeBytes(initCmd, sizeof(initCmd));
        rt_thread_mdelay(10);
    }

    s_available = true;
    trigger(); /* first reading lands ~80ms later, before the first paint */
    rt_kprintf("[WodleAht20] OK (status=0x%02x)\n", status);
}

bool available() { return s_available; }

bool read(float &temperatureC, float &humidityPct)
{
    if (!s_available) return false;

    const unsigned long now = rt_tick_get_millisecond();

    if (s_pending && now - s_triggeredAt >= MEASURE_DELAY_MS)
    {
        s_pending = false;
        uint8_t frame[Aht20Codec::FRAME_LEN];
        Aht20Codec::Reading r;
        if (readBytes(frame, sizeof(frame)) && Aht20Codec::decode(frame, r))
        {
            s_cache = r;
            s_cacheValid = true;
            s_cachedAt = now;
        }
        /* On a busy/CRC-failed frame the stale-cache check below simply
         * re-triggers; render cadence (page turns) bounds the retry rate. */
    }

    if (!s_pending && (!s_cacheValid || now - s_cachedAt >= REFRESH_MS)) trigger();

    if (!s_cacheValid) return false;
    temperatureC = s_cache.temperatureC;
    humidityPct = s_cache.humidityPct;
    return true;
}

} // namespace WodleAht20

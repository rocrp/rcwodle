/* WODLE-PORT: AHT20 temperature/humidity frame decode — pure functions with
 * no RT-Thread/I2C dependencies so the host test suite can cover them.
 * Protocol per the Aosong AHT20 datasheet:
 *   - 7-byte measurement frame: status, H[19:12], H[11:4], H[3:0]|T[19:16],
 *     T[15:8], T[7:0], CRC8
 *   - CRC8 poly 0x31 (x^8+x^5+x^4+1), init 0xFF, over bytes 0..5
 *   - RH%  = raw20 / 2^20 * 100
 *   - T°C  = raw20 / 2^20 * 200 - 50
 */
#pragma once

#include <stdint.h>

namespace Aht20Codec
{

constexpr int FRAME_LEN = 7;
constexpr uint8_t STATUS_BUSY = 0x80;       /* measurement still running */
constexpr uint8_t STATUS_CALIBRATED = 0x08; /* CAL bit; init cmd needed when clear */

struct Reading
{
    float temperatureC;
    float humidityPct;
};

inline uint8_t crc8(const uint8_t *data, int len)
{
    uint8_t crc = 0xFF;
    for (int i = 0; i < len; i++)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
        {
            crc = (crc & 0x80) ? (uint8_t)((crc << 1) ^ 0x31) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/* Decode a 7-byte frame. Rejects busy frames and CRC mismatches. */
inline bool decode(const uint8_t frame[FRAME_LEN], Reading &out)
{
    if (frame[0] & STATUS_BUSY) return false;
    if (crc8(frame, FRAME_LEN - 1) != frame[FRAME_LEN - 1]) return false;

    const uint32_t rawHum = ((uint32_t)frame[1] << 12) | ((uint32_t)frame[2] << 4) | (frame[3] >> 4);
    const uint32_t rawTemp = ((uint32_t)(frame[3] & 0x0F) << 16) | ((uint32_t)frame[4] << 8) | frame[5];

    out.humidityPct = (float)rawHum / 1048576.0f * 100.0f;
    out.temperatureC = (float)rawTemp / 1048576.0f * 200.0f - 50.0f;
    return true;
}

inline float toFahrenheit(float celsius) { return celsius * 9.0f / 5.0f + 32.0f; }

} // namespace Aht20Codec

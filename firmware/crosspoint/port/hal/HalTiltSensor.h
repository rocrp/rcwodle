/* WODLE-PORT: no IMU on the wodle — permanent no-op (upstream X4 behaves the
 * same way; tilt is an X3-only feature). */
#pragma once

#include <Arduino.h>

class HalTiltSensor
{
public:
    void begin() {}
    bool wake() { return false; }
    void deepSleep() {}
    bool isAvailable() const { return false; }
    void update() {}
    bool wasTiltedForward() { return false; }
    bool wasTiltedBack() { return false; }
    bool hadActivity() { return false; }
    void clearPendingEvents() {}
    void setOrientation(int) {}
};

extern HalTiltSensor halTiltSensor;

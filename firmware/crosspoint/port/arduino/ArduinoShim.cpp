/* WODLE-PORT: globals for the Arduino shim. */
#include "Arduino.h"

HWCDC Serial;
EspClass ESP;

extern "C" void rt_hw_cpu_reset(void);

void EspClass::restart()
{
    rt_kprintf("[shim] ESP.restart -> rt_hw_cpu_reset\n");
    rt_thread_mdelay(50);
    rt_hw_cpu_reset();
}

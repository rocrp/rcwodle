#include "rtthread.h"
#include "rtdevice.h"
#include "bf0_hal.h"

int main(void)
{
    rt_kprintf("\n[hello_wodle] boot OK (scaffold). build %s %s\n", __DATE__, __TIME__);
    while (1)
    {
        rt_thread_mdelay(1000);
    }
    return 0;
}

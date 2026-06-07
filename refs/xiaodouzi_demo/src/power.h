#ifndef POWER_H
#define POWER_H

#define POWER_ON_COLD  0
#define POWER_ON_KEY   1

void power_sleep(void);
int  power_wakeup_reason(void);

#endif

#ifndef WODLE_FRONTLIGHT_H
#define WODLE_FRONTLIGHT_H

#include "rtthread.h"

rt_err_t wodle_frontlight_init(void);
rt_err_t wodle_frontlight_set_level(int level);
int wodle_frontlight_get_level(void);
int wodle_frontlight_get_duty(void);

#endif

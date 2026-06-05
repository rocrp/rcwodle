/* WODLE-PORT: minimal FreeRTOS façade over RT-Thread.
 * Only the surface CrossPoint uses: mutexes (incl. recursive), task create/
 * delay/delete, task notification as a binary wake-up. */
#pragma once

#include <rtthread.h>

typedef rt_tick_t TickType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define pdPASS pdTRUE
#define pdFAIL pdFALSE

#define portMAX_DELAY ((TickType_t)RT_WAITING_FOREVER)
#define portTICK_PERIOD_MS (1000u / RT_TICK_PER_SECOND)
#define pdMS_TO_TICKS(ms) ((TickType_t)rt_tick_from_millisecond((rt_int32_t)(ms)))

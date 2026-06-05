/* WODLE-PORT: FreeRTOS task façade → rt_thread.
 * Task notifications are emulated with one rt_semaphore per shim-created
 * task (small fixed registry — only the render task uses this upstream).
 * Priority mapping: FreeRTOS higher-number=higher-prio, RT-Thread inverse;
 * shim tasks land just below the main thread. */
#pragma once

#include "FreeRTOS.h"

typedef rt_thread_t TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

#define tskIDLE_PRIORITY 0
typedef enum
{
    eNoAction = 0,
    eSetBits,
    eIncrement,
    eSetValueWithOverwrite,
    eSetValueWithoutOverwrite
} eNotifyAction;

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stackWords,
                       void *param, UBaseType_t prio, TaskHandle_t *handle);
void vTaskDelete(TaskHandle_t task);
BaseType_t xTaskNotify(TaskHandle_t task, uint32_t value, eNotifyAction action);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clearOnExit, TickType_t ticks);
BaseType_t xTaskNotifyWait(uint32_t bitsClearEntry, uint32_t bitsClearExit,
                           uint32_t *value, TickType_t ticks);

static inline void vTaskDelay(TickType_t ticks) { rt_thread_delay((rt_int32_t)ticks); }
static inline TaskHandle_t xTaskGetCurrentTaskHandle(void) { return rt_thread_self(); }

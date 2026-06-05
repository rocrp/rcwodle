/* WODLE-PORT: FreeRTOS semaphore façade → rt_mutex.
 * RT-Thread mutexes are owner-recursive, so the Recursive variants map to the
 * same primitive. Binary semaphores are intentionally absent (unused upstream
 * after pruning) — add an rt_sem-backed variant if the compiler asks. */
#pragma once

#include "FreeRTOS.h"

typedef rt_mutex_t SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return rt_mutex_create("cpmtx", RT_IPC_FLAG_PRIO);
}

static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void)
{
    return rt_mutex_create("cprmx", RT_IPC_FLAG_PRIO);
}

static inline void vSemaphoreDelete(SemaphoreHandle_t m)
{
    if (m) rt_mutex_delete(m);
}

static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t ticks)
{
    return rt_mutex_take(m, (rt_int32_t)ticks) == RT_EOK ? pdTRUE : pdFALSE;
}

static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
    return rt_mutex_release(m) == RT_EOK ? pdTRUE : pdFALSE;
}

#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive

static inline void *xSemaphoreGetMutexHolder(SemaphoreHandle_t m)
{
    return m ? (void *)m->owner : RT_NULL;
}

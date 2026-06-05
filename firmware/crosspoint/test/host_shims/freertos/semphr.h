/* host shim: recursive std::mutex behind the FreeRTOS names */
#pragma once

#include <mutex>

#include "FreeRTOS.h"

typedef std::recursive_mutex *SemaphoreHandle_t;

static inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutex()
{
    return new std::recursive_mutex();
}
static inline SemaphoreHandle_t xSemaphoreCreateMutex() { return xSemaphoreCreateRecursiveMutex(); }
static inline void vSemaphoreDelete(SemaphoreHandle_t m) { delete m; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t)
{
    m->lock();
    return pdTRUE;
}
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
    m->unlock();
    return pdTRUE;
}
#define xSemaphoreTakeRecursive xSemaphoreTake
#define xSemaphoreGiveRecursive xSemaphoreGive

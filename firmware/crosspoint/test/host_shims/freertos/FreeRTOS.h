/* host shim: just enough FreeRTOS surface for HalStorage.h on macOS/linux */
#pragma once
typedef unsigned long TickType_t;
typedef long BaseType_t;
#define pdTRUE ((BaseType_t)1)
#define pdFALSE ((BaseType_t)0)
#define portMAX_DELAY ((TickType_t)~0ul)

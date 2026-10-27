// Minimal FreeRTOS API on pthreads: only what the firmware outside drivers/
// uses. Priorities, stack depths and core affinity are ignored; one tick is
// CONFIG_FREERTOS_HZ from the firmware's sdkconfig (1 ms on the board).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

#include "sdkconfig.h"

#ifndef CONFIG_FREERTOS_HZ
#define CONFIG_FREERTOS_HZ 1000
#endif

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdFALSE 0
#define pdTRUE  1
#define pdFAIL  pdFALSE
#define pdPASS  pdTRUE

#define configTICK_RATE_HZ CONFIG_FREERTOS_HZ
#define portMAX_DELAY      ((TickType_t)0xFFFFFFFFu)
#define portTICK_PERIOD_MS ((TickType_t)1000 / configTICK_RATE_HZ)
#define pdMS_TO_TICKS(ms)  ((TickType_t)(((uint64_t)(ms) * configTICK_RATE_HZ) / 1000u))
#define pdTICKS_TO_MS(t)   ((uint32_t)(((uint64_t)(t) * 1000u) / configTICK_RATE_HZ))

// On the board a failed configASSERT aborts (and the chip reboots).
void sim_assert_failed(const char *expr, const char *file, int line, const char *func)
    __attribute__((noreturn));
#define configASSERT(x)                                               \
    do {                                                              \
        if (!(x)) {                                                   \
            sim_assert_failed(#x, __FILE__, __LINE__, __func__);      \
        }                                                             \
    } while (0)

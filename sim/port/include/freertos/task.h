#pragma once

#include "FreeRTOS.h"

typedef void (*TaskFunction_t)(void *);
typedef struct sim_task *TaskHandle_t;

// Each task is a detached pthread with a fixed host-sized stack.
BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                       void *arg, UBaseType_t priority, TaskHandle_t *created);
void vTaskDelete(TaskHandle_t task); // only NULL (the calling task) is supported
void vTaskDelay(TickType_t ticks);
TickType_t xTaskGetTickCount(void);

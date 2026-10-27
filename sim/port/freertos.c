// FreeRTOS tasks, queues and semaphores on pthreads (see freertos/FreeRTOS.h).

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Host threads need far more stack than FreeRTOS tasks (8-byte pointers,
// libc, sanitizers), so the requested depth is ignored.
#define SIM_TASK_STACK_BYTES (1024 * 1024)

// macOS condition variables only time out against CLOCK_REALTIME, so waits
// use an absolute wall-clock deadline. NULL means wait forever.
static const struct timespec *deadline_after(TickType_t ticks, struct timespec *ts)
{
    if (ticks == portMAX_DELAY) {
        return NULL;
    }
    struct timeval now;
    gettimeofday(&now, NULL);
    uint64_t ns = (uint64_t)now.tv_usec * 1000u + (uint64_t)pdTICKS_TO_MS(ticks) * 1000000u;
    ts->tv_sec = now.tv_sec + (time_t)(ns / 1000000000u);
    ts->tv_nsec = (long)(ns % 1000000000u);
    return ts;
}

// Returns false once the deadline has passed.
static bool wait_until(pthread_cond_t *cv, pthread_mutex_t *mu, const struct timespec *deadline)
{
    if (!deadline) {
        pthread_cond_wait(cv, mu);
        return true;
    }
    return pthread_cond_timedwait(cv, mu, deadline) != ETIMEDOUT;
}

// ---------------------------------------------------------------- tasks

struct sim_task {
    TaskFunction_t fn;
    void *arg;
    char name[16];
};

static void *task_entry(void *p)
{
    struct sim_task *t = p;
#ifdef __APPLE__
    pthread_setname_np(t->name);
#else
    pthread_setname_np(pthread_self(), t->name);
#endif
    t->fn(t->arg);
    // Same rule as ESP-IDF: a task function must never return.
    fprintf(stderr, "E sim: task %s returned instead of calling vTaskDelete(NULL)\n", t->name);
    abort();
}

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack_depth,
                       void *arg, UBaseType_t priority, TaskHandle_t *created)
{
    (void)stack_depth;
    (void)priority;
    struct sim_task *t = calloc(1, sizeof(*t));
    if (!t) {
        return pdFAIL;
    }
    t->fn = fn;
    t->arg = arg;
    snprintf(t->name, sizeof(t->name), "%s", name ? name : "task");

    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setstacksize(&attr, SIM_TASK_STACK_BYTES);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_t thread;
    int rc = pthread_create(&thread, &attr, task_entry, t);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        free(t);
        return pdFAIL;
    }
    if (created) {
        *created = t;
    }
    return pdPASS;
}

void vTaskDelete(TaskHandle_t task)
{
    configASSERT(task == NULL);
    pthread_exit(NULL);
}

void vTaskDelay(TickType_t ticks)
{
    if (ticks == 0) {
        sched_yield();
        return;
    }
    uint64_t ms = pdTICKS_TO_MS(ticks);
    struct timespec ts = { .tv_sec = (time_t)(ms / 1000), .tv_nsec = (long)(ms % 1000) * 1000000L };
    while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
    }
}

TickType_t xTaskGetTickCount(void)
{
    return (TickType_t)(((uint64_t)esp_timer_get_time() * configTICK_RATE_HZ) / 1000000u);
}

// ---------------------------------------------------------------- queues

struct sim_queue {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    uint8_t *items;
    size_t item_size, length, head, count;
};

QueueHandle_t xQueueCreate(UBaseType_t length, UBaseType_t item_size)
{
    struct sim_queue *q = calloc(1, sizeof(*q));
    if (!q) {
        return NULL;
    }
    q->items = calloc(length, item_size);
    if (!q->items) {
        free(q);
        return NULL;
    }
    q->item_size = item_size;
    q->length = length;
    pthread_mutex_init(&q->mu, NULL);
    pthread_cond_init(&q->changed, NULL);
    return q;
}

void vQueueDelete(QueueHandle_t q)
{
    if (!q) {
        return;
    }
    pthread_mutex_destroy(&q->mu);
    pthread_cond_destroy(&q->changed);
    free(q->items);
    free(q);
}

BaseType_t xQueueSend(QueueHandle_t q, const void *item, TickType_t ticks)
{
    struct timespec ts;
    const struct timespec *deadline = deadline_after(ticks, &ts);
    pthread_mutex_lock(&q->mu);
    while (q->count == q->length) {
        if (ticks == 0 || !wait_until(&q->changed, &q->mu, deadline)) {
            if (q->count < q->length) {
                break;
            }
            pthread_mutex_unlock(&q->mu);
            return pdFALSE; // errQUEUE_FULL
        }
    }
    size_t tail = (q->head + q->count) % q->length;
    memcpy(q->items + tail * q->item_size, item, q->item_size);
    q->count++;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->mu);
    return pdTRUE;
}

BaseType_t xQueueReceive(QueueHandle_t q, void *item, TickType_t ticks)
{
    struct timespec ts;
    const struct timespec *deadline = deadline_after(ticks, &ts);
    pthread_mutex_lock(&q->mu);
    while (q->count == 0) {
        if (ticks == 0 || !wait_until(&q->changed, &q->mu, deadline)) {
            if (q->count > 0) {
                break;
            }
            pthread_mutex_unlock(&q->mu);
            return pdFALSE; // errQUEUE_EMPTY
        }
    }
    memcpy(item, q->items + q->head * q->item_size, q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count--;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->mu);
    return pdTRUE;
}

BaseType_t xQueueReset(QueueHandle_t q)
{
    pthread_mutex_lock(&q->mu);
    q->head = 0;
    q->count = 0;
    pthread_cond_broadcast(&q->changed);
    pthread_mutex_unlock(&q->mu);
    return pdPASS;
}

UBaseType_t uxQueueMessagesWaiting(QueueHandle_t q)
{
    pthread_mutex_lock(&q->mu);
    UBaseType_t n = (UBaseType_t)q->count;
    pthread_mutex_unlock(&q->mu);
    return n;
}

// ---------------------------------------------------------------- semaphores

struct sim_sem {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    UBaseType_t count, max;
};

SemaphoreHandle_t xSemaphoreCreateCounting(UBaseType_t max_count, UBaseType_t initial_count)
{
    struct sim_sem *s = calloc(1, sizeof(*s));
    if (!s) {
        return NULL;
    }
    s->count = initial_count;
    s->max = max_count;
    pthread_mutex_init(&s->mu, NULL);
    pthread_cond_init(&s->changed, NULL);
    return s;
}

SemaphoreHandle_t xSemaphoreCreateMutex(void)
{
    return xSemaphoreCreateCounting(1, 1);
}

SemaphoreHandle_t xSemaphoreCreateBinary(void)
{
    return xSemaphoreCreateCounting(1, 0);
}

BaseType_t xSemaphoreTake(SemaphoreHandle_t s, TickType_t ticks)
{
    struct timespec ts;
    const struct timespec *deadline = deadline_after(ticks, &ts);
    pthread_mutex_lock(&s->mu);
    while (s->count == 0) {
        if (ticks == 0 || !wait_until(&s->changed, &s->mu, deadline)) {
            if (s->count > 0) {
                break;
            }
            pthread_mutex_unlock(&s->mu);
            return pdFALSE;
        }
    }
    s->count--;
    pthread_mutex_unlock(&s->mu);
    return pdTRUE;
}

BaseType_t xSemaphoreGive(SemaphoreHandle_t s)
{
    pthread_mutex_lock(&s->mu);
    if (s->count >= s->max) {
        pthread_mutex_unlock(&s->mu);
        return pdFALSE;
    }
    s->count++;
    pthread_cond_signal(&s->changed);
    pthread_mutex_unlock(&s->mu);
    return pdTRUE;
}

void vSemaphoreDelete(SemaphoreHandle_t s)
{
    if (!s) {
        return;
    }
    pthread_mutex_destroy(&s->mu);
    pthread_cond_destroy(&s->changed);
    free(s);
}

// esp_log, esp_timer and esp_err on the host.

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "sim.h"

#ifndef CONFIG_LOG_DEFAULT_LEVEL
#define CONFIG_LOG_DEFAULT_LEVEL 3
#endif

// ---------------------------------------------------------------- timer

// A clock that stops while the Mac sleeps: the board never sleeps, so a
// closed lid mustn't look like minutes of silence to the firmware's link
// and inactivity timeouts (macOS's CLOCK_MONOTONIC keeps counting).
#ifdef __APPLE__
#define SIM_CLOCK CLOCK_UPTIME_RAW
#else
#define SIM_CLOCK CLOCK_MONOTONIC
#endif

static int64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(SIM_CLOCK, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int64_t s_boot_us;

// Runs before main(): "boot" is process start, so a simulated reset (which
// re-execs the sim) starts the clock at 0 again, like the board.
__attribute__((constructor)) static void timer_boot(void)
{
    s_boot_us = now_us();
}

int64_t esp_timer_get_time(void)
{
    return now_us() - s_boot_us;
}

// ---------------------------------------------------------------- log

// The board runs with CONFIG_ESP_CONSOLE_NONE: until the log bridge installs
// its hook, log output goes nowhere.
static int discard_vprintf(const char *fmt, va_list args)
{
    (void)fmt;
    (void)args;
    return 0;
}

static vprintf_like_t s_vprintf = discard_vprintf;
static bool s_echo;

void sim_log_echo(bool on)
{
    s_echo = on;
}

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func)
{
    vprintf_like_t old = s_vprintf;
    s_vprintf = func;
    return old;
}

uint32_t esp_log_timestamp(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
{
    (void)tag;
    if (level > CONFIG_LOG_DEFAULT_LEVEL) {
        return;
    }
    va_list args;
    if (s_echo) {
        va_start(args, format);
        sim_vprint_fw_log(format, args);
        va_end(args);
    }
    va_start(args, format);
    s_vprintf(format, args);
    va_end(args);
}

// ---------------------------------------------------------------- err

const char *esp_err_to_name(esp_err_t code)
{
    switch (code) {
    case ESP_OK:                   return "ESP_OK";
    case ESP_FAIL:                 return "ESP_FAIL";
    case ESP_ERR_NO_MEM:           return "ESP_ERR_NO_MEM";
    case ESP_ERR_INVALID_ARG:      return "ESP_ERR_INVALID_ARG";
    case ESP_ERR_INVALID_STATE:    return "ESP_ERR_INVALID_STATE";
    case ESP_ERR_INVALID_SIZE:     return "ESP_ERR_INVALID_SIZE";
    case ESP_ERR_NOT_FOUND:        return "ESP_ERR_NOT_FOUND";
    case ESP_ERR_NOT_SUPPORTED:    return "ESP_ERR_NOT_SUPPORTED";
    case ESP_ERR_TIMEOUT:          return "ESP_ERR_TIMEOUT";
    case ESP_ERR_INVALID_RESPONSE: return "ESP_ERR_INVALID_RESPONSE";
    case ESP_ERR_INVALID_CRC:      return "ESP_ERR_INVALID_CRC";
    case ESP_ERR_INVALID_VERSION:  return "ESP_ERR_INVALID_VERSION";
    case ESP_ERR_INVALID_MAC:      return "ESP_ERR_INVALID_MAC";
    case ESP_ERR_NOT_FINISHED:     return "ESP_ERR_NOT_FINISHED";
    case ESP_ERR_NOT_ALLOWED:      return "ESP_ERR_NOT_ALLOWED";
    default:                       return "UNKNOWN ERROR";
    }
}

// The board would print this, panic and reboot. The sim stops instead, so
// the stack trace (or the debugger) shows where it happened.
void _esp_error_check_failed(esp_err_t rc, const char *file, int line, const char *function,
                             const char *expression)
{
    fprintf(stderr, "ESP_ERROR_CHECK failed: esp_err_t 0x%x (%s)\nfile: \"%s\" line %d\n"
            "func: %s\nexpression: %s\n", rc, esp_err_to_name(rc), file, line, function,
            expression);
    abort();
}

void sim_assert_failed(const char *expr, const char *file, int line, const char *func)
{
    fprintf(stderr, "assert failed: %s %s:%d (%s)\n", func, file, line, expr);
    abort();
}

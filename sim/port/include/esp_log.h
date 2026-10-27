// ESP-IDF logging (v1 text format: "I (1234) tag: message\n") on the host.
// Lines go to the vprintf hook, which the firmware's log bridge points at
// the mux, exactly as on the board. Levels follow the firmware's sdkconfig.
#pragma once

#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>

#include "sdkconfig.h"

typedef enum {
    ESP_LOG_NONE,
    ESP_LOG_ERROR,
    ESP_LOG_WARN,
    ESP_LOG_INFO,
    ESP_LOG_DEBUG,
    ESP_LOG_VERBOSE,
} esp_log_level_t;

typedef int (*vprintf_like_t)(const char *, va_list);

vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);
uint32_t esp_log_timestamp(void);
void esp_log_write(esp_log_level_t level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));

#ifndef CONFIG_LOG_MAXIMUM_LEVEL
#define CONFIG_LOG_MAXIMUM_LEVEL 3
#endif

#ifdef CONFIG_LOG_COLORS
#define SIM_LOG_COLOR_E "\033[0;31m"
#define SIM_LOG_COLOR_W "\033[0;33m"
#define SIM_LOG_COLOR_I "\033[0;32m"
#define SIM_LOG_COLOR_D ""
#define SIM_LOG_COLOR_V ""
#define SIM_LOG_RESET   "\033[0m"
#else
#define SIM_LOG_COLOR_E ""
#define SIM_LOG_COLOR_W ""
#define SIM_LOG_COLOR_I ""
#define SIM_LOG_COLOR_D ""
#define SIM_LOG_COLOR_V ""
#define SIM_LOG_RESET   ""
#endif

#define SIM_LOG_FORMAT(letter, format) \
    SIM_LOG_COLOR_##letter #letter " (%" PRIu32 ") %s: " format SIM_LOG_RESET "\n"

#define SIM_LOG_AT(level, letter, tag, format, ...)                                 \
    do {                                                                            \
        if (CONFIG_LOG_MAXIMUM_LEVEL >= (level)) {                                  \
            esp_log_write((level), (tag), SIM_LOG_FORMAT(letter, format),           \
                          esp_log_timestamp(), (tag), ##__VA_ARGS__);               \
        }                                                                           \
    } while (0)

#define ESP_LOGE(tag, format, ...) SIM_LOG_AT(ESP_LOG_ERROR, E, tag, format, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) SIM_LOG_AT(ESP_LOG_WARN, W, tag, format, ##__VA_ARGS__)
#define ESP_LOGI(tag, format, ...) SIM_LOG_AT(ESP_LOG_INFO, I, tag, format, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) SIM_LOG_AT(ESP_LOG_DEBUG, D, tag, format, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) SIM_LOG_AT(ESP_LOG_VERBOSE, V, tag, format, ##__VA_ARGS__)

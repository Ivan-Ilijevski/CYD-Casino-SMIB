#pragma once

// Same includes as ESP-IDF's esp_timer.h (firmware code relies on them).
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

// Microseconds since the (simulated) boot; restarts at 0 after a reset.
int64_t esp_timer_get_time(void);

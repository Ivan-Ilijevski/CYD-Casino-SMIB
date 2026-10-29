#pragma once

#include <stdint.h>

// The firmware's heartbeat logs free heap, which matters on the board (Wi-Fi
// is its biggest allocation and the E32R40T has no PSRAM). The sim has the
// host's heap, so this reports LVGL's pool instead — the one number here that
// tracks the board at all. See "mem" in the sim's control FIFO.
uint32_t esp_get_free_heap_size(void);

// On the board this reboots the chip. The admin panel's Restart button calls
// it; in the sim it re-execs, the same path as the `R` key.
void esp_restart(void) __attribute__((noreturn));

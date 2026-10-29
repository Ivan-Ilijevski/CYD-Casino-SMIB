// Cabinet door switch — the physical gate on the admin panel.
//
// Wiring (PIN_NUM_DOOR in the board header, internal pull-up on):
//
//     PIN_NUM_DOOR --[ door microswitch ]-- GND
//
//     door SHUT  -> switch closed -> LOW
//     door OPEN  -> released      -> HIGH
//     wire cut   -> HIGH = "open"
//
// A cut or missing wire reads as "door open" on purpose. The passcode is the
// real security boundary, so failing this way keeps the terminal administrable
// and shows a loud permanent banner; failing to "shut" would make the admin
// panel unreachable without a reflash.
#pragma once

#include <stdbool.h>
#include "esp_err.h"

esp_err_t door_init(void);

// Debounced. Polled from app_tick() rather than interrupt-driven: the app task
// is the only thread allowed to touch LVGL, and a door is not in a hurry.
// Boards whose header sets PIN_NUM_DOOR to -1 always read "shut".
bool door_is_open(void);

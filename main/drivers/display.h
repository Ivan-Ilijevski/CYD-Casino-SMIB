// ST7796 LCD + XPT2046 touch + LVGL bring-up for the 4.0" CYD board.
#pragma once

#include "lvgl.h"

// Turns the backlight on (GPIO27 high).
void display_backlight_on(void);

// Initializes the LCD panel, touch controller and LVGL (display driver,
// draw buffer, input device, 1ms tick). Must be called once from app_main
// before any LVGL objects are created.
void display_init(void);

// SMIB UI. Every function here must be called from the app/LVGL task only.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "cms/cms_types.h"

void ui_init(void);

// Screens
void ui_show_idle(void);
// p == NULL keeps the details already on screen (returning from another screen).
void ui_show_main(const player_info_t *p);
void ui_update_balance(uint64_t balance_cents, int32_t points);

// Order status: a countdown ring with the minutes left in the middle, plus a
// compact badge on the main screen so the player can keep playing and still
// see the drink coming.
void ui_show_order_status(void);
// Redraws the ring and the badge. Called about once a second while an order is
// live; the app owns the clock, this only renders what it is handed.
void ui_order_status_set(const cms_order_state_t *o);
// No order any more: hides the badge and leaves the status screen if it is up.
void ui_order_status_clear(void);

// Milliseconds since the last touch input (LVGL inactivity clock).
uint32_t ui_touch_idle_ms(void);

// Overlays (live on the top layer, visible over any screen)
void ui_set_offline(bool offline);
void ui_show_spinner(const char *text);
void ui_hide_spinner(void);
void ui_toast(const char *msg, bool ok); // auto-dismisses after 3 s

// Transfer modal (amount picker + progress)
void ui_transfer_open(uint64_t balance_cents);
void ui_transfer_progress(const char *step_text);
void ui_transfer_close(void);

// Order modal (drink list, one of each item max)
void ui_order_open(const cms_menu_t *menu);
void ui_order_close(void);
// Reads the confirmed selection (qty is always 1); returns item count.
size_t ui_order_get_selection(cms_order_item_t *out, size_t max, bool *pay_points);

// Formats cents as e.g. "1.250 МКД" (or "12,50 МКД" when there are cents).
void ui_fmt_money(char *out, size_t out_size, uint64_t cents, const char *cur);

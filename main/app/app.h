// Top-level SMIB state machine. Runs entirely on the app/LVGL task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    ST_IDLE,            // unauthenticated: logo + card prompt
    ST_AUTHENTICATING,  // card tapped, waiting for CMS auth_res
    ST_AUTHED,          // main screen: balance / points / order / transfer
} app_state_t;

void app_init(void);

// Drains pending events and runs periodic work (CMS keepalive/timeouts).
// Call from the main loop before lv_timer_handler().
void app_tick(void);

// UI button handlers (already on the app task, no queue needed).
void app_on_ui_transfer_confirm(uint64_t amount_cents);
void app_on_ui_order_confirm(void); // order details read from the ui_order module
void app_on_ui_order_open(void);

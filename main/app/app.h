// Top-level SMIB state machine. Runs entirely on the app/LVGL task.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "drivers/settings.h"

typedef enum {
    ST_IDLE,            // unauthenticated: logo + card prompt
    ST_HOLDING,         // card present, waiting out the 3 s hold window
    ST_AUTHENTICATING,  // hold complete, waiting for CMS auth_res
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
void app_on_ui_order_status_open(void); // the main-screen ring badge was tapped
void app_on_ui_cashout(void);       // machine credits -> card, then logout

// True when the terminal may change where its money messages go: no funds
// transfer in flight. Dropping the CMS socket mid-commit is exactly what
// "Funds-transfer safety" in the README forbids.
bool app_can_change_network(void);

// Admin panel save: persists the settings, then re-points the radio and the
// CMS socket at them. Returns false (having toasted why) when a money chain is
// running or the write failed, so the panel can keep the fields on screen.
bool app_on_ui_admin_save(const smib_settings_t *s);

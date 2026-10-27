// Event hand-off into the app/LVGL task. Every other task (mux_rx, sas_task,
// rc522 events, audio) communicates with the state machine ONLY through
// app_post_event(); the app task is the sole LVGL caller.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "cms/cms_types.h"

// --- CMS events (POD copies of parsed JSON; no cJSON crosses tasks) ---

typedef enum {
    CMS_EVT_LINK_UP,
    CMS_EVT_LINK_DOWN,
    CMS_EVT_AUTH_RES,
    CMS_EVT_DEBIT_RES,
    CMS_EVT_COMMIT_RES,
    CMS_EVT_ROLLBACK_RES,
    CMS_EVT_CREDIT_RES,     // cashout deposit (machine credits -> card)
    CMS_EVT_MENU_RES,       // menu itself is cached in the cms module
    CMS_EVT_ORDER_RES,
    CMS_EVT_ORDER_PUSH,     // unsolicited: one order's status/ETA changed
    CMS_EVT_ORDERS_RES,     // resync header; `orders_n` pushes follow it
    CMS_EVT_BALANCE_PUSH,
    CMS_EVT_LOGOUT_PUSH,
} cms_evt_type_t;

typedef struct {
    cms_evt_type_t type;
    bool ok;                // *_RES only; timeouts arrive as ok=false, err "timeout"
    char err_code[24];
    char err_msg[96];       // Macedonian, shown directly in a toast
    char sid[24];
    char player_name[64];
    char cur[8];
    uint64_t balance;       // integer cents (дени)
    int32_t points;
    bool balance_valid;
    bool points_valid;
    char txn[24];           // debit/commit/rollback correlation
    // ORDER_RES / ORDER_PUSH. `order_valid` is false when the message carried no
    // usable order (an order_res failure, or a status string we don't know).
    cms_order_state_t order;
    bool order_valid;
    uint8_t orders_n;       // ORDERS_RES: how many order_push frames follow
} cms_event_t;

// --- SAS events ---

typedef enum {
    SAS_EVT_LINK_UP,
    SAS_EVT_LINK_DOWN,
    SAS_EVT_EXCEPTION,
    SAS_EVT_CREDITS,        // machine credit meter changed
    SAS_EVT_AFT_RESULT,
} sas_evt_type_t;

typedef struct {
    sas_evt_type_t type;
    uint8_t exception;
    bool ok;                // AFT_RESULT
    uint8_t aft_status;     // raw SAS transfer status byte
    uint64_t credits_cents;
    char txn[24];
} sas_event_t;

// --- Top-level app events ---

typedef enum {
    APP_EVT_CARD_PRESENT,
    APP_EVT_CARD_REMOVED,
    APP_EVT_CMS,
    APP_EVT_SAS,
} app_evt_type_t;

typedef struct {
    app_evt_type_t type;
    union {
        char uid[32];       // CARD_PRESENT: UID string from the RC522
        cms_event_t cms;
        sas_event_t sas;
    } u;
} app_event_t;

// Safe from any task (0 wait; drops + logs when the queue is full).
bool app_post_event(const app_event_t *e);

// POD types shared between the CMS client, app state machine and UI.
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char sid[24];
    char name[64];
    uint64_t balance;   // integer cents (дени)
    int32_t points;
    char cur[8];        // e.g. "MKD"
} player_info_t;

#define CMS_MENU_MAX_ITEMS 16

typedef struct {
    int32_t id;             // "drink" id in the CMS protocol
    char name[48];
    uint64_t price;         // cents
    int32_t points_price;   // 0 = not purchasable with points
    bool avail;
    int32_t avg_s;          // cached avg prep seconds; 0 = no history yet
} cms_menu_item_t;

typedef struct {
    int count;
    cms_menu_item_t items[CMS_MENU_MAX_ITEMS];
} cms_menu_t;

typedef struct {
    int32_t drink;
    int32_t qty;
} cms_order_item_t;

// --- Order status (the Wolt-style countdown ring) ---

// Mirrors the CMS `orders.status` CHECK constraint. The wire carries these as
// strings; cms.c maps them here and drops a message whose status it cannot name,
// rather than guessing at an unknown one.
typedef enum {
    CMS_ORDER_RECEIVED,     // placed, the bar has not accepted it yet
    CMS_ORDER_ACCEPTED,     // the bar committed to a time
    CMS_ORDER_FULFILLED,    // poured, waiting at the bar
    CMS_ORDER_CANCELLED,    // refunded by staff
} cms_order_status_t;

// One order as the terminal knows it.
//
// The identity is `num` (CMS orders.number, INTEGER UNIQUE) rather than the
// order UUID: the firmware never sends an order id back, so carrying 36 bytes of
// UUID through a 16-deep event queue would buy nothing.
//
// eta_s/total_s are RELATIVE seconds because this board has no wall clock -- no
// RTC and no SNTP, only boot-relative esp_timer_get_time(). The CMS computes
// them at send time; total_s is measured from order creation so the ring can be
// drawn from one message, with no memory of earlier ones (resync-safe).
typedef struct {
    uint32_t num;
    cms_order_status_t status;
    int32_t eta_s;          // seconds remaining, >= 0
    int32_t total_s;        // full span the ring represents, > 0
    bool promised;          // true = the bar committed; false = our estimate
} cms_order_state_t;

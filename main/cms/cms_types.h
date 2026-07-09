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
} cms_menu_item_t;

typedef struct {
    int count;
    cms_menu_item_t items[CMS_MENU_MAX_ITEMS];
} cms_menu_t;

typedef struct {
    int32_t drink;
    int32_t qty;
} cms_order_item_t;

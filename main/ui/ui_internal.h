// Shared bits between the ui_*.c files. Not for use outside main/ui/.
#pragma once

#include "lvgl.h"
#include "cms/cms_types.h"

extern const lv_font_t Sf_Compact_14;

#define UI_COL_BG      lv_color_hex(0x14181D)
#define UI_COL_PANEL   lv_color_hex(0x1E242B)
#define UI_COL_ACCENT  lv_color_hex(0xC9A227)
#define UI_COL_ON_ACCENT lv_color_hex(0x14181D)
#define UI_COL_TEXT    lv_color_hex(0xF2F2F2)
#define UI_COL_MUTED   lv_color_hex(0x9AA3AD)
#define UI_COL_OK      lv_color_hex(0x2E7D32)
#define UI_COL_ERR     lv_color_hex(0xC62828)

lv_obj_t *ui_idle_create(void);
lv_obj_t *ui_main_create(void);
void ui_main_set_player(const player_info_t *p);
void ui_main_set_balance(uint64_t cents, int32_t points);
uint64_t ui_main_get_balance(void);
const char *ui_main_get_currency(void);

// Styled modal shell on the top layer: dimmed backdrop + centered panel.
// Returns the panel; store the backdrop via out_backdrop to delete later.
lv_obj_t *ui_modal_create(int w, int h, lv_obj_t **out_backdrop);

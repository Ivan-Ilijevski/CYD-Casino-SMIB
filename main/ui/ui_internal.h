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
// The order ring badge on the main screen. NULL hides it.
void ui_main_set_order_badge(const cms_order_state_t *o);

lv_obj_t *ui_order_status_create(void);
void ui_order_status_render(const cms_order_state_t *o);

// Shared by the full ring and the main-screen badge so the two can never
// disagree about how far round the arc should be.
#define UI_ARC_SPAN 1000
int32_t ui_order_arc_value(const cms_order_state_t *o);
// Whole minutes to show for an ETA, rounded up so "0" means only "now".
int32_t ui_order_minutes(int32_t eta_s);
// Upper end of the range shown for an estimate of `lo` minutes.
int32_t ui_order_minutes_upper(int32_t lo);
// The figure in the middle of a ring: an exact number of minutes once the bar
// has promised one, a "3-5" range while it is still our own estimate. False
// when there is no number worth showing (due, poured or cancelled).
bool ui_order_eta_text(const cms_order_state_t *o, char *out, size_t n);
// Ring colour for a status/promised combination.
lv_color_t ui_order_color(const cms_order_state_t *o);

// Styled modal shell on the top layer: dimmed backdrop + centered panel.
// Returns the panel; store the backdrop via out_backdrop to delete later.
lv_obj_t *ui_modal_create(int w, int h, lv_obj_t **out_backdrop);

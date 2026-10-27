// Authenticated main screen: player name, balance, points, and the
// Order / Transfer / Cashout actions.
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "ui.h"
#include "ui_internal.h"

static lv_obj_t *s_name_label;
static lv_obj_t *s_points_label;
static lv_obj_t *s_balance_label;   // big ASCII digits (Montserrat 28)
static lv_obj_t *s_currency_label;  // Cyrillic currency (Sf_Compact_14)

static uint64_t s_balance_cents;
static char s_currency[8] = "МКД";

static void order_btn_cb(lv_event_t *e)
{
    (void)e;
    app_on_ui_order_open();
}

static void transfer_btn_cb(lv_event_t *e)
{
    (void)e;
    ui_transfer_open(s_balance_cents);
}

static void cashout_btn_cb(lv_event_t *e)
{
    (void)e;
    app_on_ui_cashout();
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, bool primary,
                             lv_event_cb_t cb, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    if (primary) {
        lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, 0);
    } else {
        lv_obj_set_style_bg_color(btn, UI_COL_PANEL, 0);
        lv_obj_set_style_border_color(btn, UI_COL_ACCENT, 0);
        lv_obj_set_style_border_width(btn, 2, 0);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(lbl, primary ? UI_COL_ON_ACCENT : UI_COL_TEXT, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

lv_obj_t *ui_main_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COL_BG, LV_PART_MAIN);

    // Top bar: cashout button left, player name + points right.
    lv_obj_t *cashout = make_button(scr, "ИСПЛАТИ", false, cashout_btn_cb, 140, 48);
    lv_obj_align(cashout, LV_ALIGN_TOP_LEFT, 8, 8);

    s_name_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_name_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_name_label, UI_COL_TEXT, 0);
    lv_obj_set_style_text_align(s_name_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_name_label, "");
    lv_obj_align(s_name_label, LV_ALIGN_TOP_RIGHT, -8, 10);

    s_points_label = lv_label_create(scr);
    lv_obj_set_style_text_font(s_points_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_points_label, UI_COL_ACCENT, 0);
    lv_obj_set_style_text_align(s_points_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_label_set_text(s_points_label, "");
    lv_obj_align(s_points_label, LV_ALIGN_TOP_RIGHT, -8, 34);

    // Balance block, centered
    lv_obj_t *balance_caption = lv_label_create(scr);
    lv_obj_set_style_text_font(balance_caption, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(balance_caption, UI_COL_MUTED, 0);
    lv_label_set_text(balance_caption, "Салдо на картичка");
    lv_obj_align(balance_caption, LV_ALIGN_CENTER, 0, -70);

    lv_obj_t *row = lv_obj_create(scr);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_align(row, LV_ALIGN_CENTER, 0, -30);

    s_balance_label = lv_label_create(row);
    lv_obj_set_style_text_font(s_balance_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(s_balance_label, UI_COL_TEXT, 0);
    lv_label_set_text(s_balance_label, "0");

    s_currency_label = lv_label_create(row);
    lv_obj_set_style_text_font(s_currency_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_currency_label, UI_COL_MUTED, 0);
    lv_label_set_text(s_currency_label, s_currency);
    lv_obj_set_style_pad_bottom(s_currency_label, 4, 0);

    // Club logo bottom-right (created first: the action buttons must draw
    // above its transparent corner), action buttons bottom-left.
    LV_IMG_DECLARE(royal_club_zelenezburg_logo_transparent);
    lv_obj_t *logo = lv_img_create(scr);
    lv_img_set_src(logo, &royal_club_zelenezburg_logo_transparent);
    // ~69% render pinned to the source's bottom-right corner so the scaled
    // pixels hug the screen corner despite the full-size layout box.
    lv_image_set_pivot(logo, royal_club_zelenezburg_logo_transparent.header.w,
                       royal_club_zelenezburg_logo_transparent.header.h);
    lv_image_set_scale(logo, 176);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    lv_obj_t *btn_row = lv_obj_create(scr);
    lv_obj_remove_style_all(btn_row);
    lv_obj_set_size(btn_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(btn_row, 12, 0);
    lv_obj_align(btn_row, LV_ALIGN_BOTTOM_LEFT, 8, -10);

    make_button(btn_row, "НАРАЧАЈ", true, order_btn_cb, 165, 64);
    make_button(btn_row, "ПРЕФРЛИ", false, transfer_btn_cb, 165, 64);

    return scr;
}

void ui_main_set_balance(uint64_t cents, int32_t points)
{
    s_balance_cents = cents;

    char amount[32];
    ui_fmt_money(amount, sizeof(amount), cents, ""); // digits only, ASCII
    lv_label_set_text(s_balance_label, amount);

    char pts[48];
    snprintf(pts, sizeof(pts), "%ld поени", (long)points);
    lv_label_set_text(s_points_label, pts);
}

void ui_main_set_player(const player_info_t *p)
{
    lv_label_set_text(s_name_label, p->name);
    if (p->cur[0]) {
        snprintf(s_currency, sizeof(s_currency), "%s", p->cur);
        // Show the local currency name for MKD, otherwise the raw code.
        lv_label_set_text(s_currency_label,
                          strcmp(p->cur, "MKD") == 0 ? "МКД" : s_currency);
    }
    ui_main_set_balance(p->balance, p->points);
}

uint64_t ui_main_get_balance(void)
{
    return s_balance_cents;
}

const char *ui_main_get_currency(void)
{
    return strcmp(s_currency, "MKD") == 0 ? "МКД" : s_currency;
}

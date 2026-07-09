#include <stdio.h>
#include <string.h>
#include "esp_log.h"

#include "ui.h"
#include "ui_internal.h"

static lv_obj_t *s_scr_idle;
static lv_obj_t *s_scr_main;

// Top-layer overlays
static lv_obj_t *s_offline_badge;
static lv_obj_t *s_spinner_backdrop;
static lv_obj_t *s_spinner_label;
static lv_obj_t *s_toast;
static lv_obj_t *s_toast_label;
static lv_timer_t *s_toast_timer;

void ui_fmt_money(char *out, size_t out_size, uint64_t cents, const char *cur)
{
    uint64_t whole = cents / 100;
    unsigned frac = (unsigned)(cents % 100);

    // Thousands-separated whole part ("1.250")
    char digits[24];
    snprintf(digits, sizeof(digits), "%llu", (unsigned long long)whole);
    char sep[32];
    size_t dlen = strlen(digits);
    size_t si = 0;
    for (size_t i = 0; i < dlen && si < sizeof(sep) - 1; i++) {
        if (i > 0 && (dlen - i) % 3 == 0) {
            sep[si++] = '.';
        }
        sep[si++] = digits[i];
    }
    sep[si] = '\0';

    if (frac == 0) {
        snprintf(out, out_size, "%s%s%s", sep, cur && cur[0] ? " " : "", cur ? cur : "");
    } else {
        snprintf(out, out_size, "%s,%02u%s%s", sep, frac, cur && cur[0] ? " " : "", cur ? cur : "");
    }
}

lv_obj_t *ui_modal_create(int w, int h, lv_obj_t **out_backdrop)
{
    lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_50, 0);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE); // swallow touches behind

    lv_obj_t *panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, w, h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(panel, UI_COL_ACCENT, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_text_color(panel, UI_COL_TEXT, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);

    *out_backdrop = backdrop;
    return panel;
}

static void build_offline_badge(void)
{
    s_offline_badge = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_offline_badge);
    lv_obj_set_style_bg_color(s_offline_badge, UI_COL_ERR, 0);
    lv_obj_set_style_bg_opa(s_offline_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_offline_badge, 12, 0);
    lv_obj_set_style_pad_hor(s_offline_badge, 10, 0);
    lv_obj_set_style_pad_ver(s_offline_badge, 4, 0);
    lv_obj_set_size(s_offline_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_offline_badge, LV_ALIGN_TOP_RIGHT, -6, 6);

    lv_obj_t *lbl = lv_label_create(s_offline_badge);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_label_set_text(lbl, "БЕЗ ВРСКА");

    lv_obj_add_flag(s_offline_badge, LV_OBJ_FLAG_HIDDEN);
}

static void build_spinner(void)
{
    s_spinner_backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_spinner_backdrop);
    lv_obj_set_size(s_spinner_backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_spinner_backdrop, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_spinner_backdrop, LV_OPA_60, 0);
    lv_obj_add_flag(s_spinner_backdrop, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *spinner = lv_spinner_create(s_spinner_backdrop);
    lv_obj_set_size(spinner, 64, 64);
    lv_obj_align(spinner, LV_ALIGN_CENTER, 0, -20);
    lv_obj_set_style_arc_color(spinner, UI_COL_ACCENT, LV_PART_INDICATOR);

    s_spinner_label = lv_label_create(s_spinner_backdrop);
    lv_obj_set_style_text_font(s_spinner_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_spinner_label, UI_COL_TEXT, 0);
    lv_label_set_text(s_spinner_label, "");
    lv_obj_align(s_spinner_label, LV_ALIGN_CENTER, 0, 32);

    lv_obj_add_flag(s_spinner_backdrop, LV_OBJ_FLAG_HIDDEN);
}

static void toast_timer_cb(lv_timer_t *t)
{
    (void)t;
    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    s_toast_timer = NULL;
}

static void build_toast(void)
{
    s_toast = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_toast);
    lv_obj_set_style_bg_opa(s_toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_toast, 8, 0);
    lv_obj_set_style_pad_hor(s_toast, 14, 0);
    lv_obj_set_style_pad_ver(s_toast, 8, 0);
    lv_obj_set_size(s_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_align(s_toast, LV_ALIGN_BOTTOM_MID, 0, -14);

    s_toast_label = lv_label_create(s_toast);
    lv_obj_set_style_text_font(s_toast_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_toast_label, lv_color_white(), 0);
    lv_label_set_long_mode(s_toast_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(s_toast_label, 420, 0);

    lv_obj_add_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
}

void ui_init(void)
{
    lv_display_t *d = lv_display_get_default();
    lv_theme_t *theme = lv_theme_default_init(d,
                                              UI_COL_ACCENT,
                                              lv_palette_main(LV_PALETTE_GREY),
                                              true /* dark */,
                                              &Sf_Compact_14);
    lv_display_set_theme(d, theme);

    s_scr_idle = ui_idle_create();
    s_scr_main = ui_main_create();

    build_offline_badge();
    build_spinner();
    build_toast();
}

void ui_show_idle(void)
{
    lv_screen_load(s_scr_idle);
}

void ui_show_main(const player_info_t *p)
{
    ui_main_set_player(p);
    lv_screen_load(s_scr_main);
}

void ui_update_balance(uint64_t balance_cents, int32_t points)
{
    ui_main_set_balance(balance_cents, points);
}

void ui_set_offline(bool offline)
{
    if (offline) {
        lv_obj_remove_flag(s_offline_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_offline_badge, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_show_spinner(const char *text)
{
    lv_label_set_text(s_spinner_label, text ? text : "");
    lv_obj_remove_flag(s_spinner_backdrop, LV_OBJ_FLAG_HIDDEN);
}

void ui_hide_spinner(void)
{
    lv_obj_add_flag(s_spinner_backdrop, LV_OBJ_FLAG_HIDDEN);
}

void ui_toast(const char *msg, bool ok)
{
    lv_obj_set_style_bg_color(s_toast, ok ? UI_COL_OK : UI_COL_ERR, 0);
    lv_label_set_text(s_toast_label, msg);
    lv_obj_remove_flag(s_toast, LV_OBJ_FLAG_HIDDEN);
    if (s_toast_timer) {
        lv_timer_reset(s_toast_timer);
    } else {
        s_toast_timer = lv_timer_create(toast_timer_cb, 3000, NULL);
        lv_timer_set_repeat_count(s_toast_timer, 1);
    }
}

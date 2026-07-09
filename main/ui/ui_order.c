// Drink order modal: menu list with quantity steppers, cash/points toggle.
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "ui.h"
#include "ui_internal.h"

static lv_obj_t *s_backdrop;
static lv_obj_t *s_total_label;
static lv_obj_t *s_pay_points_cb;
static lv_obj_t *s_qty_labels[CMS_MENU_MAX_ITEMS];

static cms_menu_t s_menu;
static int32_t s_qty[CMS_MENU_MAX_ITEMS];

static bool pay_with_points(void)
{
    return s_pay_points_cb && lv_obj_has_state(s_pay_points_cb, LV_STATE_CHECKED);
}

static void update_total(void)
{
    uint64_t total_cents = 0;
    int64_t total_points = 0;
    for (int i = 0; i < s_menu.count; i++) {
        total_cents += (uint64_t)s_qty[i] * s_menu.items[i].price;
        total_points += (int64_t)s_qty[i] * s_menu.items[i].points_price;
    }
    if (pay_with_points()) {
        lv_label_set_text_fmt(s_total_label, "Вкупно: %lld поени", (long long)total_points);
    } else {
        char amount[32];
        ui_fmt_money(amount, sizeof(amount), total_cents, ui_main_get_currency());
        lv_label_set_text_fmt(s_total_label, "Вкупно: %s", amount);
    }
}

static void qty_cb(lv_event_t *e)
{
    intptr_t packed = (intptr_t)lv_event_get_user_data(e);
    int idx = (int)(packed >> 1);
    int delta = (packed & 1) ? 1 : -1;

    if (idx < 0 || idx >= s_menu.count) {
        return;
    }
    int32_t q = s_qty[idx] + delta;
    if (q < 0) {
        q = 0;
    }
    if (q > 9) {
        q = 9;
    }
    s_qty[idx] = q;
    lv_label_set_text_fmt(s_qty_labels[idx], "%ld", (long)q);
    update_total();
}

static void pay_toggle_cb(lv_event_t *e)
{
    (void)e;
    update_total();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_order_close();
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    app_on_ui_order_confirm();
}

static lv_obj_t *stepper_button(lv_obj_t *parent, const char *txt, int idx, int delta)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, 36, 36);
    lv_obj_set_style_bg_color(btn, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(btn, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    intptr_t packed = ((intptr_t)idx << 1) | (delta > 0 ? 1 : 0);
    lv_obj_add_event_cb(btn, qty_cb, LV_EVENT_CLICKED, (void *)packed);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, txt);
    lv_obj_set_style_text_color(lbl, UI_COL_TEXT, 0);
    lv_obj_center(lbl);
    return btn;
}

void ui_order_open(const cms_menu_t *menu)
{
    if (s_backdrop) {
        return;
    }
    s_menu = *menu;
    memset(s_qty, 0, sizeof(s_qty));

    lv_obj_t *panel = ui_modal_create(450, 300, &s_backdrop);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &Sf_Compact_14, 0);
    lv_label_set_text(title, "Нарачка на пијалоци");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    lv_obj_t *list = lv_obj_create(panel);
    lv_obj_remove_style_all(list);
    lv_obj_set_size(list, LV_PCT(100), 150);
    lv_obj_align(list, LV_ALIGN_TOP_MID, 0, 26);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, 6, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);

    for (int i = 0; i < s_menu.count; i++) {
        const cms_menu_item_t *it = &s_menu.items[i];

        lv_obj_t *row = lv_obj_create(list);
        lv_obj_remove_style_all(row);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_column(row, 8, 0);

        lv_obj_t *name = lv_label_create(row);
        lv_obj_set_style_text_font(name, &Sf_Compact_14, 0);
        lv_obj_set_style_text_color(name, it->avail ? UI_COL_TEXT : UI_COL_MUTED, 0);
        char price[32];
        ui_fmt_money(price, sizeof(price), it->price, "");
        lv_label_set_text_fmt(name, "%s  (%s / %ldп)", it->name, price, (long)it->points_price);
        lv_obj_set_flex_grow(name, 1);

        if (!it->avail) {
            s_qty_labels[i] = NULL;
            lv_obj_t *na = lv_label_create(row);
            lv_obj_set_style_text_font(na, &Sf_Compact_14, 0);
            lv_obj_set_style_text_color(na, UI_COL_MUTED, 0);
            lv_label_set_text(na, "нема");
            continue;
        }

        stepper_button(row, "-", i, -1);
        s_qty_labels[i] = lv_label_create(row);
        lv_obj_set_style_text_color(s_qty_labels[i], UI_COL_TEXT, 0);
        lv_obj_set_style_min_width(s_qty_labels[i], 18, 0);
        lv_label_set_text(s_qty_labels[i], "0");
        stepper_button(row, "+", i, +1);
    }

    s_pay_points_cb = lv_checkbox_create(panel);
    lv_obj_set_style_text_font(s_pay_points_cb, &Sf_Compact_14, 0);
    lv_checkbox_set_text(s_pay_points_cb, "Плати со поени");
    lv_obj_align(s_pay_points_cb, LV_ALIGN_BOTTOM_LEFT, 0, -46);
    lv_obj_add_event_cb(s_pay_points_cb, pay_toggle_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_total_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_total_label, &Sf_Compact_14, 0);
    lv_obj_align(s_total_label, LV_ALIGN_BOTTOM_RIGHT, 0, -50);

    lv_obj_t *actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *cancel = lv_button_create(actions);
    lv_obj_set_size(cancel, 130, 44);
    lv_obj_set_style_bg_color(cancel, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(cancel, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_obj_set_style_text_font(cl, &Sf_Compact_14, 0);
    lv_label_set_text(cl, "Откажи");
    lv_obj_center(cl);

    lv_obj_t *confirm = lv_button_create(actions);
    lv_obj_set_size(confirm, 130, 44);
    lv_obj_set_style_bg_color(confirm, UI_COL_ACCENT, 0);
    lv_obj_add_event_cb(confirm, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ol = lv_label_create(confirm);
    lv_obj_set_style_text_font(ol, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(ol, UI_COL_ON_ACCENT, 0);
    lv_label_set_text(ol, "Нарачај");
    lv_obj_center(ol);

    update_total();
}

size_t ui_order_get_selection(cms_order_item_t *out, size_t max, bool *pay_points)
{
    size_t n = 0;
    for (int i = 0; i < s_menu.count && n < max; i++) {
        if (s_qty[i] > 0) {
            out[n].drink = s_menu.items[i].id;
            out[n].qty = s_qty[i];
            n++;
        }
    }
    if (pay_points) {
        *pay_points = pay_with_points();
    }
    return n;
}

void ui_order_close(void)
{
    if (s_backdrop) {
        lv_obj_delete(s_backdrop);
        s_backdrop = NULL;
        s_total_label = NULL;
        s_pay_points_cb = NULL;
        memset(s_qty_labels, 0, sizeof(s_qty_labels));
    }
}

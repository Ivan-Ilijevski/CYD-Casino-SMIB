// Drink order modal: big toggle buttons (one of each drink, 2 per row),
// running total on top, КОРИСТИ ПОЕНИ / ОТКАЖИ / НАРАЧАЈ along the bottom.
#include <stdio.h>
#include <string.h>

#include "app/app.h"
#include "ui.h"
#include "ui_internal.h"

static lv_obj_t *s_backdrop;
static lv_obj_t *s_total_label;
static lv_obj_t *s_pay_points_btn;
static lv_obj_t *s_item_btns[CMS_MENU_MAX_ITEMS]; // NULL = unavailable item

static cms_menu_t s_menu;

static bool pay_with_points(void)
{
    return s_pay_points_btn && lv_obj_has_state(s_pay_points_btn, LV_STATE_CHECKED);
}

static bool item_selected(int i)
{
    return s_item_btns[i] && lv_obj_has_state(s_item_btns[i], LV_STATE_CHECKED);
}

static void update_total(void)
{
    uint64_t total_cents = 0;
    int64_t total_points = 0;
    for (int i = 0; i < s_menu.count; i++) {
        if (item_selected(i)) {
            total_cents += s_menu.items[i].price;
            total_points += s_menu.items[i].points_price;
        }
    }
    if (pay_with_points()) {
        lv_label_set_text_fmt(s_total_label, "Вкупно: %lld поени", (long long)total_points);
    } else {
        char amount[32];
        ui_fmt_money(amount, sizeof(amount), total_cents, ui_main_get_currency());
        lv_label_set_text_fmt(s_total_label, "Вкупно: %s", amount);
    }
}

static void selection_cb(lv_event_t *e)
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

// Checkable toggle: panel/border normally, accent fill while CHECKED.
static lv_obj_t *make_toggle(lv_obj_t *parent, const char *text, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_set_style_bg_color(btn, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(btn, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_border_color(btn, UI_COL_ACCENT, LV_STATE_CHECKED);
    // text_color is inherited: state-dependent color goes on the button so
    // the label follows it (the label itself never enters CHECKED state).
    lv_obj_set_style_text_color(btn, UI_COL_TEXT, 0);
    lv_obj_set_style_text_color(btn, UI_COL_ON_ACCENT, LV_STATE_CHECKED);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(lbl, text); // copied by LVGL
    lv_obj_center(lbl);
    return btn;
}

void ui_order_open(const cms_menu_t *menu)
{
    if (s_backdrop) {
        return;
    }
    s_menu = *menu;
    memset(s_item_btns, 0, sizeof(s_item_btns));

    lv_obj_t *panel = ui_modal_create(450, 300, &s_backdrop);

    // Top row: title left, running total right.
    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &Sf_Compact_14, 0);
    lv_label_set_text(title, "Нарачка на пијалоци");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    s_total_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_total_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_total_label, UI_COL_ACCENT, 0);
    lv_obj_align(s_total_label, LV_ALIGN_TOP_RIGHT, 0, 0);

    // Drink grid: big toggle targets, two per row.
    lv_obj_t *grid = lv_obj_create(panel);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, LV_PCT(100), 190);
    lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, 8, 0);
    lv_obj_set_style_pad_column(grid, 8, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);

    for (int i = 0; i < s_menu.count; i++) {
        const cms_menu_item_t *it = &s_menu.items[i];
        char price[32], txt[96];
        ui_fmt_money(price, sizeof(price), it->price, "");
        snprintf(txt, sizeof(txt), "%s\n%s", it->name,
                 it->avail ? price : "нема");
        if (it->avail && it->points_price > 0) {
            snprintf(txt, sizeof(txt), "%s\n%s / %ldп", it->name, price,
                     (long)it->points_price);
        }

        lv_obj_t *btn = make_toggle(grid, txt, 205, 52);
        if (!it->avail) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
            lv_obj_set_style_text_color(lv_obj_get_child(btn, 0), UI_COL_MUTED, 0);
            continue; // stays NULL in s_item_btns: never selectable
        }
        lv_obj_add_event_cb(btn, selection_cb, LV_EVENT_VALUE_CHANGED, NULL);
        s_item_btns[i] = btn;
    }

    // Bottom row: pay-with-points toggle + cancel + confirm.
    lv_obj_t *actions = lv_obj_create(panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(actions, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_pay_points_btn = make_toggle(actions, "КОРИСТИ\nПОЕНИ", 135, 50);
    lv_obj_add_event_cb(s_pay_points_btn, selection_cb, LV_EVENT_VALUE_CHANGED, NULL);

    lv_obj_t *cancel = lv_button_create(actions);
    lv_obj_set_size(cancel, 135, 50);
    lv_obj_set_style_bg_color(cancel, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(cancel, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(cancel, 1, 0);
    lv_obj_add_event_cb(cancel, cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_obj_set_style_text_font(cl, &Sf_Compact_14, 0);
    lv_label_set_text(cl, "ОТКАЖИ");
    lv_obj_center(cl);

    lv_obj_t *confirm = lv_button_create(actions);
    lv_obj_set_size(confirm, 135, 50);
    lv_obj_set_style_bg_color(confirm, UI_COL_ACCENT, 0);
    lv_obj_add_event_cb(confirm, confirm_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *ol = lv_label_create(confirm);
    lv_obj_set_style_text_font(ol, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(ol, UI_COL_ON_ACCENT, 0);
    lv_label_set_text(ol, "НАРАЧАЈ");
    lv_obj_center(ol);

    update_total();
}

size_t ui_order_get_selection(cms_order_item_t *out, size_t max, bool *pay_points)
{
    size_t n = 0;
    for (int i = 0; i < s_menu.count && n < max; i++) {
        if (item_selected(i)) {
            out[n].drink = s_menu.items[i].id;
            out[n].qty = 1;
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
        s_pay_points_btn = NULL;
        memset(s_item_btns, 0, sizeof(s_item_btns));
    }
}

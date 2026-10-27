// Order status screen: a countdown ring with the minutes left in the middle.
//
// This is a pure renderer. The deadline and the clock live in app.c, which hands
// a cms_order_state_t here about once a second; nothing on this screen keeps
// time of its own, so there is no timer to leak and no second clock to disagree.
#include <stdio.h>

#include "app/app.h"
#include "ui.h"
#include "ui_internal.h"

#define RING_SIZE   232
#define RING_WIDTH  16

static lv_obj_t *s_arc;
static lv_obj_t *s_minutes;     // big ASCII digits, or "--" before the first ETA
static lv_obj_t *s_unit;        // "мин" under the number
static lv_obj_t *s_status;      // Macedonian status line under the ring
static lv_obj_t *s_number;      // "Нарачка #42"

// ------------------------------------------------------------ shared helpers

int32_t ui_order_minutes(int32_t eta_s)
{
    if (eta_s <= 0) {
        return 0;
    }
    // Round up: a drink 10 seconds away is "1 мин", not "0 мин". Only a true
    // zero reads as zero, and the caller shows "Наскоро" for that instead.
    return (eta_s + 59) / 60;
}

int32_t ui_order_minutes_upper(int32_t lo)
{
    // Widens with the wait: a two-minute window around a 30-minute estimate
    // would be a precision we do not have. Never narrower than +2.
    int32_t spread = lo / 2;
    return lo + (spread > 2 ? spread : 2);
}

bool ui_order_eta_text(const cms_order_state_t *o, char *out, size_t n)
{
    bool counting = (o->status == CMS_ORDER_RECEIVED || o->status == CMS_ORDER_ACCEPTED);
    int32_t lo = ui_order_minutes(o->eta_s);
    if (!counting || lo <= 0) {
        return false;
    }
    if (o->promised) {
        // The bar committed to this one, so state it plainly.
        snprintf(out, n, "%ld", (long)lo);
    } else {
        snprintf(out, n, "%ld-%ld", (long)lo, (long)ui_order_minutes_upper(lo));
    }
    return true;
}

int32_t ui_order_arc_value(const cms_order_state_t *o)
{
    // A finished order is a full ring regardless of what the clock said.
    if (o->status == CMS_ORDER_FULFILLED || o->status == CMS_ORDER_CANCELLED) {
        return UI_ARC_SPAN;
    }
    if (o->total_s <= 0) {
        return UI_ARC_SPAN;
    }
    int32_t eta = o->eta_s < 0 ? 0 : o->eta_s;
    if (eta > o->total_s) {
        eta = o->total_s;
    }
    // Fills as the drink gets closer: 0 when just ordered, full when due.
    return UI_ARC_SPAN - (int32_t)(((int64_t)eta * UI_ARC_SPAN) / o->total_s);
}

lv_color_t ui_order_color(const cms_order_state_t *o)
{
    switch (o->status) {
    case CMS_ORDER_FULFILLED:
        return UI_COL_OK;
    case CMS_ORDER_CANCELLED:
        return UI_COL_ERR;
    default:
        break;
    }
    // Gold once the bar has actually committed to a time; muted while the
    // number is only our own estimate, so the two never look equally certain.
    return o->promised ? UI_COL_ACCENT : UI_COL_MUTED;
}

static const char *status_text(const cms_order_state_t *o)
{
    switch (o->status) {
    case CMS_ORDER_RECEIVED:
        return "Примена";
    case CMS_ORDER_ACCEPTED:
        // Past the promised time but not poured yet: never show a negative
        // number, and never keep claiming a minute count that has run out.
        return o->eta_s > 0 ? "Се подготвува" : "Наскоро";
    case CMS_ORDER_FULFILLED:
        // Comma, not an em dash: Sf_Compact_14 is ASCII + Cyrillic only.
        return "Готово, подигнете на бар";
    case CMS_ORDER_CANCELLED:
        return "Откажана";
    }
    return "";
}

// ------------------------------------------------------------ the screen

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_show_main(NULL);
}

lv_obj_t *ui_order_status_create(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, UI_COL_BG, LV_PART_MAIN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    s_number = lv_label_create(scr);
    lv_obj_set_style_text_font(s_number, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_number, UI_COL_MUTED, 0);
    lv_label_set_text(s_number, "");
    lv_obj_align(s_number, LV_ALIGN_TOP_MID, 0, 8);

    s_arc = lv_arc_create(scr);
    lv_obj_set_size(s_arc, RING_SIZE, RING_SIZE);
    lv_obj_align(s_arc, LV_ALIGN_CENTER, 0, -4);
    // A full ring starting at twelve o'clock. Angles are lv_value_precise_t,
    // which is int32_t here because LV_USE_FLOAT is off.
    lv_arc_set_bg_angles(s_arc, 0, 360);
    lv_arc_set_rotation(s_arc, 270);
    lv_arc_set_range(s_arc, 0, UI_ARC_SPAN);
    lv_arc_set_value(s_arc, 0);
    lv_obj_set_style_arc_width(s_arc, RING_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_arc, RING_WIDTH, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_arc, UI_COL_PANEL, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_arc, UI_COL_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(s_arc, true, LV_PART_INDICATOR);
    // Display only: lv_arc is a draggable input widget by default, and a
    // player dragging the ring must not be able to rewrite the ETA.
    lv_obj_remove_flag(s_arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_opa(s_arc, LV_OPA_TRANSP, LV_PART_KNOB);

    // Children of the arc, so they track the ring rather than being pinned to
    // coordinates read before the layout settled.
    s_minutes = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_minutes, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(s_minutes, UI_COL_TEXT, 0);
    lv_label_set_text(s_minutes, "--");
    lv_obj_align(s_minutes, LV_ALIGN_CENTER, 0, -14);

    // "мин" needs the Cyrillic font, so it cannot share the label above.
    s_unit = lv_label_create(s_arc);
    lv_obj_set_style_text_font(s_unit, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_unit, UI_COL_MUTED, 0);
    lv_label_set_text(s_unit, "мин");
    lv_obj_align(s_unit, LV_ALIGN_CENTER, 0, 26);

    s_status = lv_label_create(scr);
    lv_obj_set_style_text_font(s_status, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_status, UI_COL_TEXT, 0);
    lv_label_set_text(s_status, "");
    lv_obj_align(s_status, LV_ALIGN_BOTTOM_MID, 0, -56);

    lv_obj_t *back = lv_button_create(scr);
    lv_obj_set_size(back, 140, 44);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_bg_color(back, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(back, UI_COL_MUTED, 0);
    lv_obj_set_style_border_width(back, 1, 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back);
    lv_obj_set_style_text_font(back_lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(back_lbl, UI_COL_TEXT, 0);
    lv_label_set_text(back_lbl, "НАЗАД");
    lv_obj_center(back_lbl);

    return scr;
}

void ui_order_status_render(const cms_order_state_t *o)
{
    if (!s_arc || !o) {
        return;
    }

    lv_arc_set_value(s_arc, ui_order_arc_value(o));
    lv_obj_set_style_arc_color(s_arc, ui_order_color(o), LV_PART_INDICATOR);

    // "Нарачка #" is 16 bytes on its own (Cyrillic is two bytes a letter).
    char buf[48];
    snprintf(buf, sizeof(buf), "Нарачка #%lu", (unsigned long)o->num);
    lv_label_set_text(s_number, buf);
    lv_label_set_text(s_status, status_text(o));

    bool counting = (o->status == CMS_ORDER_RECEIVED || o->status == CMS_ORDER_ACCEPTED);
    if (ui_order_eta_text(o, buf, sizeof(buf))) {
        lv_label_set_text(s_minutes, buf);
        lv_obj_remove_flag(s_unit, LV_OBJ_FLAG_HIDDEN);
    } else {
        // Due, poured or cancelled: the status line says it better than a number.
        lv_label_set_text(s_minutes, counting ? LV_SYMBOL_BELL : LV_SYMBOL_OK);
        lv_obj_add_flag(s_unit, LV_OBJ_FLAG_HIDDEN);
    }
}

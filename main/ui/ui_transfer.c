// Transfer modal: preset amount picker, then progress while the
// CMS-debit → SAS-AFT → CMS-commit chain runs.
#include <stdio.h>

#include "app/app.h"
#include "ui.h"
#include "ui_internal.h"

static lv_obj_t *s_backdrop;
static lv_obj_t *s_picker;        // container with presets + confirm/cancel
static lv_obj_t *s_progress_label;
static lv_obj_t *s_confirm_label;
static uint64_t s_selected_cents;
static uint64_t s_balance_cents;

// Preset amounts in MKD (1 MKD = 100 дени/cents).
static const uint32_t k_presets_mkd[] = { 100, 200, 500, 1000 };

static void update_confirm_label(void)
{
    if (s_selected_cents == 0) {
        lv_label_set_text(s_confirm_label, "Изберете износ");
        return;
    }
    char amount[32];
    ui_fmt_money(amount, sizeof(amount), s_selected_cents, ui_main_get_currency());
    lv_label_set_text_fmt(s_confirm_label, "Потврди: %s", amount);
}

static void preset_cb(lv_event_t *e)
{
    s_selected_cents = (uint64_t)(uintptr_t)lv_event_get_user_data(e);
    if (s_selected_cents > s_balance_cents) {
        s_selected_cents = s_balance_cents;
    }
    update_confirm_label();
}

static void cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_transfer_close();
}

static void confirm_cb(lv_event_t *e)
{
    (void)e;
    if (s_selected_cents == 0) {
        return;
    }
    // Switch the modal to progress mode; the app drives the text updates
    // and closes it when the transfer chain resolves.
    lv_obj_add_flag(s_picker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_progress_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_progress_label, "Обработка...");
    app_on_ui_transfer_confirm(s_selected_cents);
}

static lv_obj_t *small_button(lv_obj_t *parent, const char *text,
                              lv_event_cb_t cb, void *user_data, bool primary)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, LV_SIZE_CONTENT, 44);
    lv_obj_set_style_pad_hor(btn, 14, 0);
    if (primary) {
        lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, 0);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(lbl, primary ? UI_COL_ON_ACCENT : UI_COL_TEXT, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

void ui_transfer_open(uint64_t balance_cents)
{
    if (s_backdrop) {
        return; // already open
    }
    s_balance_cents = balance_cents;
    s_selected_cents = 0;

    lv_obj_t *panel = ui_modal_create(410, 280, &s_backdrop);

    lv_obj_t *title = lv_label_create(panel);
    lv_obj_set_style_text_font(title, &Sf_Compact_14, 0);
    lv_label_set_text(title, "Префрли средства на автоматот");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_picker = lv_obj_create(panel);
    lv_obj_remove_style_all(s_picker);
    lv_obj_set_size(s_picker, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_align(s_picker, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_flex_flow(s_picker, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_picker, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_picker, 14, 0);

    lv_obj_t *presets = lv_obj_create(s_picker);
    lv_obj_remove_style_all(presets);
    lv_obj_set_size(presets, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(presets, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(presets, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(presets, 8, 0);

    for (size_t i = 0; i < sizeof(k_presets_mkd) / sizeof(k_presets_mkd[0]); i++) {
        uint64_t cents = (uint64_t)k_presets_mkd[i] * 100;
        char txt[16];
        snprintf(txt, sizeof(txt), "%u", (unsigned)k_presets_mkd[i]);
        lv_obj_t *btn = small_button(presets, txt, preset_cb, (void *)(uintptr_t)cents, false);
        if (cents > balance_cents) {
            lv_obj_add_state(btn, LV_STATE_DISABLED);
        }
    }
    // "Everything on the card"
    lv_obj_t *all_btn = small_button(presets, "Преостанато", preset_cb,
                                     (void *)(uintptr_t)balance_cents, false);
    if (balance_cents == 0) {
        lv_obj_add_state(all_btn, LV_STATE_DISABLED);
    }

    lv_obj_t *actions = lv_obj_create(s_picker);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    small_button(actions, "Откажи", cancel_cb, NULL, false);
    lv_obj_t *confirm = small_button(actions, "Изберете износ", confirm_cb, NULL, true);
    s_confirm_label = lv_obj_get_child(confirm, 0);

    s_progress_label = lv_label_create(panel);
    lv_obj_set_style_text_font(s_progress_label, &Sf_Compact_14, 0);
    lv_label_set_text(s_progress_label, "");
    lv_obj_center(s_progress_label);
    lv_obj_add_flag(s_progress_label, LV_OBJ_FLAG_HIDDEN);
}

void ui_transfer_progress(const char *step_text)
{
    if (s_backdrop && s_progress_label) {
        lv_label_set_text(s_progress_label, step_text);
    }
}

void ui_transfer_close(void)
{
    if (s_backdrop) {
        lv_obj_delete(s_backdrop);
        s_backdrop = NULL;
        s_picker = NULL;
        s_progress_label = NULL;
        s_confirm_label = NULL;
    }
}

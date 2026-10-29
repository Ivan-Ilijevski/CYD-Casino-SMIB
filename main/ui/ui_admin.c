// Admin panel: network setup for staff, behind the cabinet door switch and a
// build-time passcode.
//
// Two stages on the top layer. The keypad comes first and the panel is only
// built once the passcode matches, so a player poking at an open cabinet never
// sees the configuration at all.
//
// Everything here runs on the app/LVGL task, like the rest of ui/.
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "board.h"
#include "app/app.h"
#include "cms/cms.h"
#include "cms/cms_link.h"
#include "drivers/net.h"
#include "drivers/settings.h"
#include "ui.h"
#include "ui_internal.h"

static const char *TAG = "ui_admin";

// Wrong codes allowed before the keypad locks, and for how long. The door is
// already a physical lock, so this only has to stop idle guessing.
#define ADMIN_MAX_TRIES   3
#define ADMIN_LOCKOUT_MS  30000
#define ADMIN_CODE_MAX    12

// How often the status block and the scan button re-read the drivers.
#define ADMIN_REFRESH_MS  1000

// Keyboard height; the panel shrinks to this much less when one is up.
#define ADMIN_KB_HEIGHT   140

static lv_obj_t *s_backdrop;        // covers both stages

// --- passcode stage ---
static lv_obj_t *s_pin_panel;
static lv_obj_t *s_pin_dots;
static lv_obj_t *s_pin_keys;
static lv_timer_t *s_pin_timer;     // ticks the lockout countdown
static char s_pin[ADMIN_CODE_MAX + 1];
// Deliberately NOT reset when the panel closes: otherwise closing and
// reopening would clear a lockout, and the lockout would mean nothing.
static int s_tries;
static uint32_t s_locked_until_ms;

// --- panel stage ---
static lv_obj_t *s_panel;
static lv_obj_t *s_status_label;
static lv_obj_t *s_ssid_ta;
static lv_obj_t *s_pass_ta;
static lv_obj_t *s_host_ta;
static lv_obj_t *s_port_ta;
static lv_obj_t *s_ap_dropdown;

static lv_obj_t *s_keyboard;
static lv_timer_t *s_refresh_timer;
static bool s_scan_pending;
// The scan results behind the dropdown, so picking an entry reads the SSID from
// here by index instead of parsing it back out of the option label (which
// carries the signal strength, and which an SSID could itself contain).
static net_ap_t s_aps[NET_SCAN_MAX];
static size_t s_ap_count;

static void pin_render(void);
static void panel_build(void);
static void refresh_cb(lv_timer_t *t);

// ---------------------------------------------------------------- helpers

// `w <= 0` sizes the button to its label plus padding, like ui_transfer.c's
// small_button. Fixed widths clip Cyrillic labels at this 20 px face, and the
// clipping is silent.
static lv_obj_t *admin_button(lv_obj_t *parent, const char *text, bool primary,
                              lv_event_cb_t cb, void *user_data, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_size(btn, w > 0 ? w : LV_SIZE_CONTENT, h);
    if (w <= 0) {
        lv_obj_set_style_pad_hor(btn, 10, 0);
    }
    if (primary) {
        lv_obj_set_style_bg_color(btn, UI_COL_ACCENT, 0);
    } else {
        lv_obj_set_style_bg_color(btn, UI_COL_PANEL, 0);
        lv_obj_set_style_border_color(btn, UI_COL_ACCENT, 0);
        lv_obj_set_style_border_width(btn, 1, 0);
    }
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(lbl, primary ? UI_COL_ON_ACCENT : UI_COL_TEXT, 0);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

// A labelled single-line text field. `password` masks it.
static lv_obj_t *field_row(lv_obj_t *parent, const char *label, const char *value,
                           int max_len, bool password, bool numeric)
{
    lv_obj_t *row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 8, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    // Sf_Compact_14 is a 20 px face, so ~11 px per character: "Лозинка" needs
    // about 85 px. A label with an explicit width wraps by default, and a
    // wrapped label makes its row two lines tall, which pushed the action
    // buttons off a 320 px screen. Hence 96 px AND clipping rather than
    // wrapping, so a longer label can never silently break the layout again.
    lv_obj_t *lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(lbl, UI_COL_MUTED, 0);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_CLIP);
    lv_obj_set_width(lbl, 96);
    lv_label_set_text(lbl, label);

    lv_obj_t *ta = lv_textarea_create(row);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_max_length(ta, max_len);
    lv_textarea_set_password_mode(ta, password);
    lv_textarea_set_text(ta, value ? value : "");
    lv_obj_set_style_text_font(ta, &Sf_Compact_14, 0);
    lv_obj_set_height(ta, 32);
    lv_obj_set_flex_grow(ta, 1);
    // Remembered so the keyboard comes up in the right mode for this field.
    lv_obj_set_user_data(ta, (void *)(uintptr_t)(numeric ? 1 : 0));
    return ta;
}

// ---------------------------------------------------------------- passcode

static void pin_render(void)
{
    char dots[ADMIN_CODE_MAX + 1];
    size_t n = strlen(s_pin);
    for (size_t i = 0; i < n; i++) {
        dots[i] = '*';
    }
    dots[n] = '\0';

    uint32_t now = lv_tick_get();
    if (s_locked_until_ms && now < s_locked_until_ms) {
        lv_label_set_text_fmt(s_pin_dots, "Блокирано (%us)",
                              (unsigned)((s_locked_until_ms - now + 999) / 1000));
        lv_obj_set_style_text_color(s_pin_dots, UI_COL_ERR, 0);
        lv_obj_add_state(s_pin_keys, LV_STATE_DISABLED);
        return;
    }
    if (s_locked_until_ms) {
        // Lockout just elapsed.
        s_locked_until_ms = 0;
        s_tries = 0;
        lv_obj_remove_state(s_pin_keys, LV_STATE_DISABLED);
    }
    lv_obj_set_style_text_color(s_pin_dots, UI_COL_TEXT, 0);
    lv_label_set_text(s_pin_dots, n ? dots : "Внесете код");
}

static void pin_submit(void)
{
    if (strcmp(s_pin, SMIB_ADMIN_PASSCODE) == 0) {
        ESP_LOGI(TAG, "admin unlocked");
        s_pin[0] = '\0';
        s_tries = 0;
        lv_timer_delete(s_pin_timer);
        s_pin_timer = NULL;
        lv_obj_delete(s_pin_panel);
        s_pin_panel = NULL;
        s_pin_dots = NULL;
        s_pin_keys = NULL;
        panel_build();
        return;
    }
    s_pin[0] = '\0';
    if (++s_tries >= ADMIN_MAX_TRIES) {
        s_locked_until_ms = lv_tick_get() + ADMIN_LOCKOUT_MS;
        ESP_LOGW(TAG, "admin keypad locked after %d wrong codes", s_tries);
    } else {
        ui_toast("Погрешен код", false);
    }
    pin_render();
}

static void pin_key_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    const char *txt = lv_buttonmatrix_get_button_text(bm, lv_buttonmatrix_get_selected_button(bm));
    if (!txt) {
        return;
    }
    if (strcmp(txt, "C") == 0) {
        s_pin[0] = '\0';
    } else if (strcmp(txt, "OK") == 0) {
        pin_submit();
        return;
    } else {
        size_t n = strlen(s_pin);
        if (n < ADMIN_CODE_MAX) {
            s_pin[n] = txt[0];
            s_pin[n + 1] = '\0';
        }
    }
    pin_render();
}

static void pin_cancel_cb(lv_event_t *e)
{
    (void)e;
    ui_admin_close();
}

// A locked keypad emits no events, so without this the countdown would freeze
// at its first value and the lockout would never lift on its own -- staff
// would have to close and reopen the panel to get back in.
static void pin_tick_cb(lv_timer_t *t)
{
    (void)t;
    if (s_locked_until_ms) {
        pin_render();
    }
}

static void pin_build(void)
{
    s_pin_panel = ui_modal_create(300, 290, &s_backdrop);
    s_pin[0] = '\0';

    lv_obj_t *title = lv_label_create(s_pin_panel);
    lv_obj_set_style_text_font(title, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(title, UI_COL_TEXT, 0);
    lv_label_set_text(title, "Админ пристап");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 4);

    lv_obj_t *cancel = admin_button(s_pin_panel, "Откажи", false, pin_cancel_cb, NULL, 84, 32);
    lv_obj_align(cancel, LV_ALIGN_TOP_RIGHT, 0, 0);

    s_pin_dots = lv_label_create(s_pin_panel);
    lv_obj_set_style_text_font(s_pin_dots, &Sf_Compact_14, 0);
    lv_obj_align(s_pin_dots, LV_ALIGN_TOP_MID, 0, 40);

    // Digits and "C"/"OK" are all ASCII, so Sf_Compact_14 covers the keypad.
    static const char *const kmap[] = {
        "1", "2", "3", "\n",
        "4", "5", "6", "\n",
        "7", "8", "9", "\n",
        "C", "0", "OK", "",
    };
    s_pin_keys = lv_buttonmatrix_create(s_pin_panel);
    lv_buttonmatrix_set_map(s_pin_keys, kmap);
    lv_obj_set_size(s_pin_keys, 240, 180);
    lv_obj_align(s_pin_keys, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_pin_keys, &Sf_Compact_14, 0);
    lv_obj_set_style_bg_opa(s_pin_keys, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_pin_keys, 0, 0);
    lv_obj_set_style_pad_all(s_pin_keys, 0, 0);
    lv_obj_set_style_bg_color(s_pin_keys, UI_COL_PANEL, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_pin_keys, UI_COL_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_pin_keys, UI_COL_ACCENT, LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_pin_keys, 1, LV_PART_ITEMS);
    lv_obj_set_style_radius(s_pin_keys, 6, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_pin_keys, pin_key_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_pin_timer = lv_timer_create(pin_tick_cb, 250, NULL);
    pin_render();
}

// ---------------------------------------------------------------- keyboard

static void keyboard_hide(void)
{
    if (!s_keyboard) {
        return;
    }
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_set_height(s_panel, 300);
    lv_obj_align(s_panel, LV_ALIGN_CENTER, 0, 0);
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        keyboard_hide();
    }
}

static void ta_event_cb(lv_event_t *e)
{
    lv_obj_t *ta = lv_event_get_target(e);
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        bool numeric = (uintptr_t)lv_obj_get_user_data(ta) != 0;
        lv_keyboard_set_mode(s_keyboard, numeric ? LV_KEYBOARD_MODE_NUMBER
                                                 : LV_KEYBOARD_MODE_TEXT_LOWER);
        lv_keyboard_set_textarea(s_keyboard, ta);
        lv_obj_remove_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        // Reflow the panel above the keyboard so the field being typed into is
        // still visible on a 320 px-tall screen.
        lv_obj_set_height(s_panel, 320 - ADMIN_KB_HEIGHT - 12);
        lv_obj_align(s_panel, LV_ALIGN_TOP_MID, 0, 6);
        lv_obj_scroll_to_view(ta, LV_ANIM_OFF);
    } else if (code == LV_EVENT_DEFOCUSED) {
        keyboard_hide();
    }
}

// ---------------------------------------------------------------- actions

static void scan_cb(lv_event_t *e)
{
    (void)e;
    if (net_scan_start() == ESP_OK) {
        s_scan_pending = true;
        s_ap_count = 0; // no stale entry can be picked while this runs
        lv_dropdown_set_options(s_ap_dropdown, "Скенирање...");
    } else {
        ui_toast("Скенирањето не е можно сега", false);
    }
}

static void ap_pick_cb(lv_event_t *e)
{
    (void)e;
    uint32_t i = lv_dropdown_get_selected(s_ap_dropdown);
    if (s_scan_pending || i >= s_ap_count) {
        return; // the placeholder option, or a stale pick mid-scan
    }
    lv_textarea_set_text(s_ssid_ta, s_aps[i].ssid);
}

static void save_cb(lv_event_t *e)
{
    (void)e;
    keyboard_hide();

    smib_settings_t s;
    memset(&s, 0, sizeof(s));
    snprintf(s.wifi_ssid, sizeof(s.wifi_ssid), "%s", lv_textarea_get_text(s_ssid_ta));
    snprintf(s.wifi_pass, sizeof(s.wifi_pass), "%s", lv_textarea_get_text(s_pass_ta));
    snprintf(s.cms_host, sizeof(s.cms_host), "%s", lv_textarea_get_text(s_host_ta));

    if (s.wifi_ssid[0] == '\0') {
        ui_toast("Внесете име на мрежата", false);
        return;
    }
    if (s.cms_host[0] == '\0') {
        ui_toast("Внесете адреса на CMS", false);
        return;
    }
    // Port is typed on a numeric keypad, so this only has to catch an empty
    // field and an out-of-range number.
    const char *port_txt = lv_textarea_get_text(s_port_ta);
    long port = 0;
    if (port_txt[0] == '\0' || sscanf(port_txt, "%ld", &port) != 1 ||
        port < 1 || port > 65535) {
        ui_toast("Портата мора да е 1-65535", false);
        return;
    }
    s.cms_port = (uint16_t)port;

    if (!app_on_ui_admin_save(&s)) {
        return; // app.c already explained why (money chain in flight)
    }
    ui_toast("Зачувано, поврзување...", true);
}

static void reset_cb(lv_event_t *e)
{
    (void)e;
    keyboard_hide();
    if (settings_reset() != ESP_OK) {
        ui_toast("Ресетирањето не успеа", false);
        return;
    }
    // Show what the board will come up with, without applying it: staff still
    // press Save (or Restart) to commit, so a mis-tap changes nothing live.
    smib_settings_t s;
    settings_load(&s);
    lv_textarea_set_text(s_ssid_ta, s.wifi_ssid);
    lv_textarea_set_text(s_pass_ta, s.wifi_pass);
    lv_textarea_set_text(s_host_ta, s.cms_host);
    char port[8];
    snprintf(port, sizeof(port), "%u", (unsigned)s.cms_port);
    lv_textarea_set_text(s_port_ta, port);
    ui_toast("Фабрички вредности вчитани", true);
}

static void restart_cb(lv_event_t *e)
{
    (void)e;
    if (!app_can_change_network()) {
        ui_toast("Трансакција во тек", false);
        return;
    }
    ESP_LOGW(TAG, "restart from the admin panel");
    esp_restart();
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_admin_close();
}

// ---------------------------------------------------------------- status

static void refresh_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_status_label) {
        return;
    }

    char ip[16] = "-";
    net_ip(ip, sizeof(ip));
    int rssi = net_rssi();
    char host[CMS_HOST_MAX];
    uint16_t port = 0;
    cms_link_get_endpoint(host, sizeof(host), &port);

    lv_label_set_text_fmt(s_status_label,
                          "WiFi: %s   IP: %s   Сигнал: %d dBm\n"
                          "CMS: %s  (%s:%u)\n"
                          "Верзија: %s   Уред: %s",
                          net_state_name(net_state()), ip, rssi,
                          cms_link_connected() ? (cms_link_up() ? "поврзан" : "без одговор")
                                               : "без врска",
                          host[0] ? host : "-", (unsigned)port,
                          SMIB_FW_VERSION, SMIB_DEVICE_ID);

    if (s_scan_pending && !net_scan_busy()) {
        s_scan_pending = false;
        s_ap_count = net_scan_results(s_aps, NET_SCAN_MAX);
        // lv_dropdown takes one newline-separated string. snprintf returns the
        // length it WANTED to write, so clamp rather than adding it blindly --
        // otherwise a full buffer walks `off` past the end and the remaining
        // size underflows.
        char opts[NET_SCAN_MAX * (NET_SSID_MAX + 16)];
        size_t off = 0;
        for (size_t i = 0; i < s_ap_count; i++) {
            int wrote = snprintf(opts + off, sizeof(opts) - off, "%s%s  %ddBm%s",
                                 i ? "\n" : "", s_aps[i].ssid, (int)s_aps[i].rssi,
                                 s_aps[i].secure ? " *" : "");
            if (wrote < 0 || (size_t)wrote >= sizeof(opts) - off) {
                // Truncated: drop this entry and every one after it, so the
                // option list and s_aps stay index-aligned for ap_pick_cb.
                opts[off] = '\0';
                s_ap_count = i;
                break;
            }
            off += (size_t)wrote;
        }
        lv_dropdown_set_options(s_ap_dropdown, s_ap_count ? opts : "Нема мрежи");
    }
}

// ---------------------------------------------------------------- panel

static void panel_build(void)
{
    smib_settings_t cfg;
    settings_load(&cfg);

    s_panel = lv_obj_create(s_backdrop);
    lv_obj_set_size(s_panel, 460, 300);
    lv_obj_center(s_panel);
    lv_obj_set_style_bg_color(s_panel, UI_COL_PANEL, 0);
    lv_obj_set_style_border_color(s_panel, UI_COL_ACCENT, 0);
    lv_obj_set_style_border_width(s_panel, 1, 0);
    lv_obj_set_style_radius(s_panel, 10, 0);
    lv_obj_set_style_text_color(s_panel, UI_COL_TEXT, 0);
    lv_obj_set_style_pad_all(s_panel, 8, 0);
    lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_panel, 4, 0);

    // No section headings. The screen is 320 px tall and the status block plus
    // four fields plus the action row already fill it; three headings would
    // push "Зачувај и поврзи" below the fold, where staff would never find it.
    // The field labels carry the grouping instead.
    s_status_label = lv_label_create(s_panel);
    lv_obj_set_style_text_font(s_status_label, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(s_status_label, UI_COL_TEXT, 0);
    lv_obj_set_width(s_status_label, LV_PCT(100));
    lv_label_set_text(s_status_label, "");

    lv_obj_t *ap_row = lv_obj_create(s_panel);
    lv_obj_remove_style_all(ap_row);
    lv_obj_set_size(ap_row, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(ap_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(ap_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(ap_row, 8, 0);
    lv_obj_remove_flag(ap_row, LV_OBJ_FLAG_SCROLLABLE);

    s_ap_dropdown = lv_dropdown_create(ap_row);
    lv_dropdown_set_options(s_ap_dropdown, "Притиснете Скенирај");
    lv_obj_set_style_text_font(s_ap_dropdown, &Sf_Compact_14, 0);
    lv_obj_set_height(s_ap_dropdown, 32);
    lv_obj_set_flex_grow(s_ap_dropdown, 1);
    lv_obj_add_event_cb(s_ap_dropdown, ap_pick_cb, LV_EVENT_VALUE_CHANGED, NULL);
    admin_button(ap_row, "Скенирај", false, scan_cb, NULL, 100, 32);

    // The SSID stays a text field even with the scan list above it, so a
    // hidden network can be typed in — the scan only fills this in.
    s_ssid_ta = field_row(s_panel, "Мрежа", cfg.wifi_ssid, NET_SSID_MAX - 1, false, false);
    s_pass_ta = field_row(s_panel, "Лозинка", cfg.wifi_pass, NET_PASS_MAX - 1, true, false);

    // Host and port share a row: two more full rows would not fit above the
    // action buttons.
    s_host_ta = field_row(s_panel, "CMS", cfg.cms_host, SETTINGS_HOST_MAX - 1,
                          false, false);
    lv_obj_t *host_row = lv_obj_get_parent(s_host_ta);
    lv_obj_t *port_lbl = lv_label_create(host_row);
    lv_obj_set_style_text_font(port_lbl, &Sf_Compact_14, 0);
    lv_obj_set_style_text_color(port_lbl, UI_COL_MUTED, 0);
    lv_label_set_text(port_lbl, ":");

    char port[8];
    snprintf(port, sizeof(port), "%u", (unsigned)cfg.cms_port);
    s_port_ta = lv_textarea_create(host_row);
    lv_textarea_set_one_line(s_port_ta, true);
    lv_textarea_set_max_length(s_port_ta, 5);
    lv_textarea_set_text(s_port_ta, port);
    lv_obj_set_style_text_font(s_port_ta, &Sf_Compact_14, 0);
    lv_obj_set_size(s_port_ta, 78, 32);
    lv_obj_set_user_data(s_port_ta, (void *)(uintptr_t)1); // numeric keyboard

    lv_obj_t *tas[] = { s_ssid_ta, s_pass_ta, s_host_ta, s_port_ta };
    for (size_t i = 0; i < sizeof(tas) / sizeof(tas[0]); i++) {
        lv_obj_add_event_cb(tas[i], ta_event_cb, LV_EVENT_ALL, NULL);
    }

    lv_obj_t *actions = lv_obj_create(s_panel);
    lv_obj_remove_style_all(actions);
    lv_obj_set_size(actions, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(actions, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(actions, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_remove_flag(actions, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_top(actions, 4, 0);

    // Content-sized: four Cyrillic labels at this face do not fit any set of
    // fixed widths that also leaves gaps in a 444 px row.
    admin_button(actions, "Зачувај", true, save_cb, NULL, 0, 38);
    admin_button(actions, "Фабрички", false, reset_cb, NULL, 0, 38);
    admin_button(actions, "Рестарт", false, restart_cb, NULL, 0, 38);
    admin_button(actions, "Затвори", false, close_cb, NULL, 0, 38);

    // One keyboard for every field, on the top layer so it draws over the
    // panel. Montserrat, not Sf_Compact_14: that font has no LV_SYMBOL glyphs
    // and its backspace key would render as a tofu box.
    s_keyboard = lv_keyboard_create(lv_layer_top());
    lv_obj_set_size(s_keyboard, LV_PCT(100), ADMIN_KB_HEIGHT);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_keyboard, &lv_font_montserrat_14, 0);
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_cb, LV_EVENT_ALL, NULL);

    s_refresh_timer = lv_timer_create(refresh_cb, ADMIN_REFRESH_MS, NULL);
    refresh_cb(NULL); // don't show an empty status block for the first second
}

// ---------------------------------------------------------------- api

void ui_admin_open(void)
{
    if (s_backdrop) {
        return; // already open
    }
    ESP_LOGI(TAG, "admin panel opened");
    pin_build();
}

void ui_admin_close(void)
{
    if (!s_backdrop) {
        return;
    }
    if (s_refresh_timer) {
        lv_timer_delete(s_refresh_timer);
        s_refresh_timer = NULL;
    }
    if (s_pin_timer) {
        lv_timer_delete(s_pin_timer);
        s_pin_timer = NULL;
    }
    if (s_keyboard) {
        lv_obj_delete(s_keyboard); // lives on the top layer, not under the backdrop
        s_keyboard = NULL;
    }
    lv_obj_delete(s_backdrop);
    s_backdrop = NULL;
    s_pin_panel = NULL;
    s_pin_dots = NULL;
    s_pin_keys = NULL;
    s_panel = NULL;
    s_status_label = NULL;
    s_ssid_ta = s_pass_ta = s_host_ta = s_port_ta = NULL;
    s_ap_dropdown = NULL;

    s_scan_pending = false;
    s_ap_count = 0;
    s_pin[0] = '\0';
    ESP_LOGI(TAG, "admin panel closed");
}

bool ui_admin_is_open(void)
{
    return s_backdrop != NULL;
}

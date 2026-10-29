#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "drivers/audio.h"
#include "drivers/door.h"
#include "drivers/net.h"
#include "drivers/settings.h"
#include "cms/cms.h"
#include "cms/cms_link.h"
#include "sas/sas.h"
#include "ui/ui.h"
#include "app_events.h"
#include "app.h"

static const char *TAG = "app";

// Auto-logout an authed session after this much combined inactivity (no
// touch AND no machine activity). Protects a balance whose owner walked
// away — the card no longer has to stay on the reader.
#define APP_IDLE_LOGOUT_US  (5 * 60 * 1000000LL)
// The card must sit on the reader this long before auth is requested, so a
// quick accidental tap does nothing. Marked by three rising tones spread
// evenly across the window.
#define APP_CARD_HOLD_US    (1500 * 1000LL)
#define APP_HOLD_BEEPS      3

// A finished order (poured or cancelled) stays on the ring this long before the
// screen goes back to the main one, so "Готово" is actually read.
#define ORDER_DWELL_US      (20 * 1000000LL)
// A cooking order holds the session open until its promised time, plus this
// much slack. Deliberately NOT indefinite: if the bar never marks the order
// fulfilled, the normal idle logout must still fire. Nothing is lost by it --
// the next card tap resyncs the order back from the CMS.
#define ORDER_GRACE_US      (5 * 60 * 1000000LL)
// How often the countdown re-renders. The ring shows whole minutes, but a
// 1 Hz tick keeps the arc moving smoothly enough and costs one small redraw.
#define ORDER_UI_PERIOD_US  (1000 * 1000LL)

// Funds-transfer chain: CMS debit (hold) -> SAS AFT -> CMS commit.
// Cashout chain:        SAS AFT from machine -> CMS credit.
// Money safety rules:
//   - AFT failed  -> rollback the CMS hold (retry up to 3x).
//   - AFT succeeded but commit unanswered -> NEVER rollback; retry commit
//     until the CMS confirms (commit is idempotent by txn).
//   - Cashout AFT succeeded -> the credits are off the machine; retry the
//     CMS credit until confirmed (credit is idempotent by txn).
typedef enum {
    XFER_NONE,
    XFER_DEBIT,     // waiting for debit_res
    XFER_AFT,       // waiting for SAS AFT result
    XFER_COMMIT,    // waiting for debit_commit_res (retries forever)
    XFER_ROLLBACK,  // waiting for debit_rollback_res (retries 3x)
    XFER_CASHOUT,   // waiting for the SAS AFT-from-machine result
    XFER_CREDIT,    // waiting for credit_res (retries forever)
} xfer_state_t;

static QueueHandle_t s_queue;
static app_state_t s_state = ST_IDLE;
static player_info_t s_player;
static char s_auth_uid[32];         // card that opened (or is opening) the session
static bool s_cms_link;
static bool s_sas_link;

static xfer_state_t s_xfer = XFER_NONE;
static char s_xfer_txn[24];
static uint64_t s_xfer_amount;
static int s_rollback_retries;

static bool s_menu_requested;
static bool s_order_pending;

// The one order the terminal is tracking, and the clock the ring counts on.
//
// There is no persistence anywhere in this firmware, so this is lost on reset;
// recovery is cms_orders_req() after the next auth, which is also what makes a
// returning player see their order again.
typedef struct {
    bool active;
    cms_order_state_t st;
    int64_t zero_us;        // when eta_s reaches 0 (esp_timer_get_time basis)
    int64_t settled_us;     // when fulfilled/cancelled landed; 0 while cooking
} order_track_t;

static order_track_t s_order;
static int64_t s_order_ui_us;

static void order_clear(void);

static int64_t s_last_sas_activity_us;
static int64_t s_hold_start_us;
static int s_hold_beeps;

// Cabinet door, polled in app_tick. Starts "shut" so the first poll after boot
// reports an edge if it is actually open.
static bool s_door_open;

static const char *state_name(app_state_t st)
{
    switch (st) {
    case ST_IDLE:           return "IDLE";
    case ST_HOLDING:        return "HOLDING";
    case ST_AUTHENTICATING: return "AUTHENTICATING";
    case ST_AUTHED:         return "AUTHED";
    }
    return "?";
}

static void set_state(app_state_t st)
{
    if (st != s_state) {
        ESP_LOGI(TAG, "state %s -> %s", state_name(s_state), state_name(st));
        s_state = st;
    }
}

bool app_post_event(const app_event_t *e)
{
    if (!s_queue || xQueueSend(s_queue, e, 0) != pdTRUE) {
        ESP_LOGW(TAG, "event queue full, dropping evt type=%d", e->type);
        return false;
    }
    return true;
}

static void update_offline_badge(void)
{
    ui_set_offline(!s_cms_link || !s_sas_link);
}

// Tears down any session UI and returns to the idle screen.
static void end_session(void)
{
    ui_transfer_close();
    ui_order_close();
    ui_hide_spinner();
    memset(&s_player, 0, sizeof(s_player));
    s_auth_uid[0] = '\0';
    s_menu_requested = false;
    s_order_pending = false;
    order_clear();
    set_state(ST_IDLE);
    ui_show_idle();
}

// ---------------------------------------------------------------- order ring

// Hands the tracked order (or its absence) to the UI, recomputing the minutes
// left from the local clock so the ring ticks between CMS messages.
static void order_render(void)
{
    if (!s_order.active) {
        ui_order_status_clear();
        return;
    }
    cms_order_state_t view = s_order.st;
    if (view.status == CMS_ORDER_RECEIVED || view.status == CMS_ORDER_ACCEPTED) {
        int64_t left_us = s_order.zero_us - esp_timer_get_time();
        view.eta_s = left_us > 0 ? (int32_t)(left_us / 1000000) : 0;
    }
    ui_order_status_set(&view);
}

static void order_clear(void)
{
    memset(&s_order, 0, sizeof(s_order));
    ui_order_status_clear();
}

// Takes on an order from order_res or order_push.
static void order_adopt(const cms_order_state_t *o)
{
    // A repeat push for the same order updates it and a newer order supersedes
    // an older one, but a straggler for an order already replaced is ignored --
    // otherwise a late push could resurrect a finished order.
    if (s_order.active && o->num < s_order.st.num) {
        return;
    }
    int64_t now = esp_timer_get_time();
    bool settled = (o->status == CMS_ORDER_FULFILLED || o->status == CMS_ORDER_CANCELLED);

    s_order.active = true;
    s_order.st = *o;
    s_order.zero_us = now + (int64_t)o->eta_s * 1000000;
    s_order.settled_us = settled ? now : 0;
    order_render();
}

// Called when a transfer/cashout chain fully resolves (success or failure).
static void finish_transfer(void)
{
    s_xfer = XFER_NONE;
    s_xfer_txn[0] = '\0';
    ui_transfer_close();
    ui_hide_spinner();
}

// ---------------------------------------------------------------- card

static void handle_card_present(const char *uid)
{
    ESP_LOGI(TAG, "card present, uid=%s (state=%s)", uid, state_name(s_state));
    if (s_state != ST_IDLE) {
        // Sessions survive card removal; a tap can't displace one. It ends
        // via ИСПЛАТИ, a CMS logout push or the inactivity timeout.
        audio_play(CUE_SCAN);
        bool same = uid[0] && strcmp(uid, s_auth_uid) == 0;
        ui_toast(same ? "Веќе сте најавени" : "Активна е друга сесија", false);
        return;
    }
    audio_play(CUE_HOLD1); // tone 1 of the hold countdown
    if (!s_cms_link || uid[0] == '\0') {
        ui_toast("Нема врска со системот", false);
        audio_play(CUE_ERROR);
        return;
    }
    // Auth is NOT requested yet: the card must survive the hold window
    // first (app_tick sends it).
    snprintf(s_auth_uid, sizeof(s_auth_uid), "%s", uid);
    set_state(ST_HOLDING);
    s_hold_start_us = esp_timer_get_time();
    s_hold_beeps = 1;
    ui_show_spinner("Држете ја картичката...");
}

static void handle_card_removed(void)
{
    ESP_LOGI(TAG, "card removed (state=%s, xfer=%d)", state_name(s_state), s_xfer);
    if (s_state == ST_HOLDING) {
        // Removed before the hold window elapsed: no auth was requested.
        end_session();
        ui_toast("Држете ја картичката подолго", false);
        audio_play(CUE_ERROR);
        return;
    }
    if (s_state == ST_AUTHENTICATING) {
        // The card must stay on the reader until auth completes.
        end_session();
        ui_toast("Картичката е тргната предвреме", false);
        audio_play(CUE_ERROR);
    }
    // ST_AUTHED: the session lives on without the card.
}

// ---------------------------------------------------------------- auth

static void handle_auth_res(const cms_event_t *ev)
{
    if (s_state != ST_AUTHENTICATING) {
        return;
    }
    ui_hide_spinner();
    if (!ev->ok) {
        set_state(ST_IDLE);
        ui_toast(ev->err_msg[0] ? ev->err_msg : "Најавата не успеа", false);
        audio_play(CUE_AUTH_FAIL);
        return;
    }
    snprintf(s_player.sid, sizeof(s_player.sid), "%s", ev->sid);
    snprintf(s_player.name, sizeof(s_player.name), "%s", ev->player_name);
    snprintf(s_player.cur, sizeof(s_player.cur), "%s", ev->cur[0] ? ev->cur : "MKD");
    s_player.balance = ev->balance_valid ? ev->balance : 0;
    s_player.points = ev->points_valid ? ev->points : 0;
    set_state(ST_AUTHED);
    s_last_sas_activity_us = esp_timer_get_time(); // fresh 5-min idle window
    ui_show_main(&s_player);
    ui_toast("Може да ја тргнете картичката", true);
    audio_play(CUE_AUTH_OK);
    // Anything still cooking from before this session (or before a reboot)
    // comes back here. Filtered by card on the CMS side, not by sid -- this sid
    // is brand new, so a session-scoped query would always come back empty.
    cms_orders_req(s_player.sid);
}

// ---------------------------------------------------------------- ordering

void app_on_ui_order_open(void)
{
    if (s_state != ST_AUTHED || s_xfer != XFER_NONE) {
        return;
    }
    if (cms_fetch_menu() != ESP_OK) {
        ui_toast("Менито е недостапно", false);
        return;
    }
    s_menu_requested = true;
    ui_show_spinner("Вчитување на менито...");
}

static void handle_menu_res(const cms_event_t *ev)
{
    if (!s_menu_requested || s_state != ST_AUTHED) {
        return;
    }
    s_menu_requested = false;
    ui_hide_spinner();
    cms_menu_t menu;
    if (!ev->ok || cms_get_menu(&menu) != ESP_OK) {
        ui_toast(ev->err_msg[0] ? ev->err_msg : "Менито е недостапно", false);
        audio_play(CUE_ERROR);
        return;
    }
    ui_order_open(&menu);
}

void app_on_ui_order_confirm(void)
{
    // The same guards app_on_ui_order_open uses, plus s_order_pending: the
    // modal closing is what used to stop a double-tap sending two order_reqs,
    // which is not a guarantee worth relying on for something that charges a card.
    if (s_state != ST_AUTHED || s_xfer != XFER_NONE || s_order_pending) {
        return;
    }
    cms_order_item_t items[CMS_MENU_MAX_ITEMS];
    bool pay_points = false;
    size_t n = ui_order_get_selection(items, CMS_MENU_MAX_ITEMS, &pay_points);
    if (n == 0) {
        ui_toast("Изберете барем еден пијалок", false);
        return;
    }
    if (cms_order(s_player.sid, items, n, pay_points) != ESP_OK) {
        ui_toast("Нарачката не е испратена", false);
        audio_play(CUE_ERROR);
        return;
    }
    s_order_pending = true;
    ui_order_close();
    ui_show_spinner("Нарачката се испраќа...");
}

static void handle_order_res(const cms_event_t *ev)
{
    if (!s_order_pending) {
        return;
    }
    s_order_pending = false;
    ui_hide_spinner();
    if (!ev->ok) {
        ui_toast(ev->err_msg[0] ? ev->err_msg : "Нарачката не успеа", false);
        audio_play(CUE_ERROR);
        return;
    }
    if (ev->balance_valid) {
        s_player.balance = ev->balance;
    }
    if (ev->points_valid) {
        s_player.points = ev->points;
    }
    ui_update_balance(s_player.balance, s_player.points);
    audio_play(CUE_ORDER_OK);

    // The order_res already carries an estimate built from the CMS's cached
    // per-drink averages, so the ring starts counting immediately instead of
    // waiting for the bar to accept.
    if (ev->order_valid) {
        order_adopt(&ev->order);
        ui_show_order_status();
    } else {
        // An older CMS that answers without the order fields: keep the
        // behaviour the terminal has always had rather than showing nothing.
        ui_toast("Нарачката е примена", true);
    }
}

// Status/ETA changed, or a resync is replaying this card's open orders.
static void handle_order_push(const cms_event_t *ev)
{
    if (s_state != ST_AUTHED || !ev->order_valid) {
        return;
    }
    order_adopt(&ev->order);
}

static void handle_orders_res(const cms_event_t *ev)
{
    if (s_state != ST_AUTHED) {
        return;
    }
    // n == 0 is the authoritative "nothing is cooking", and the only thing that
    // clears a stale order after a reboot. A non-zero count needs no action:
    // the order_push frames that carry the detail follow in the same burst.
    if (ev->ok && ev->orders_n == 0) {
        order_clear();
    }
}

void app_on_ui_order_status_open(void)
{
    if (s_state == ST_AUTHED && s_order.active) {
        ui_show_order_status();
    }
}

// ---------------------------------------------------------------- transfer

void app_on_ui_transfer_confirm(uint64_t amount_cents)
{
    if (s_state != ST_AUTHED || s_xfer != XFER_NONE || amount_cents == 0) {
        return;
    }
    if (!s_sas_link) {
        ui_transfer_close();
        ui_toast("Автоматот не е поврзан", false);
        audio_play(CUE_ERROR);
        return;
    }
    if (cms_debit(s_player.sid, amount_cents, s_xfer_txn) != ESP_OK) {
        ui_transfer_close();
        ui_toast("Префрлањето не успеа", false);
        audio_play(CUE_ERROR);
        return;
    }
    s_xfer = XFER_DEBIT;
    s_xfer_amount = amount_cents;
    s_rollback_retries = 0;
    ui_transfer_progress("Резервација на средствата...");
    ESP_LOGI(TAG, "transfer start: %llu cents, txn %s",
             (unsigned long long)amount_cents, s_xfer_txn);
}

static bool txn_matches(const char *txn)
{
    return txn[0] && strcmp(txn, s_xfer_txn) == 0;
}

static void handle_debit_res(const cms_event_t *ev)
{
    if (s_xfer != XFER_DEBIT || !txn_matches(ev->txn)) {
        return;
    }
    if (!ev->ok) {
        // Nothing moved. (On a timeout the CMS side expires unclaimed holds.)
        ui_toast(ev->err_msg[0] ? ev->err_msg : "Префрлањето не успеа", false);
        audio_play(CUE_ERROR);
        finish_transfer();
        return;
    }
    if (ev->balance_valid) {
        s_player.balance = ev->balance;
        ui_update_balance(s_player.balance, s_player.points);
    }
    s_xfer = XFER_AFT;
    ui_transfer_progress("Префрлање на автоматот...");
    if (sas_aft_transfer(s_xfer_amount, s_xfer_txn) != ESP_OK) {
        ESP_LOGW(TAG, "AFT not accepted, rolling back txn %s", s_xfer_txn);
        s_xfer = XFER_ROLLBACK;
        ui_transfer_progress("Враќање на средствата...");
        cms_rollback(s_xfer_txn, "aft_unavailable");
    }
}

static void handle_aft_result(const sas_event_t *ev)
{
    if (!txn_matches(ev->txn)) {
        return;
    }
    if (s_xfer == XFER_AFT) {
        if (ev->ok) {
            s_xfer = XFER_COMMIT;
            ui_transfer_progress("Финализирање...");
            cms_commit(s_xfer_txn);
            return;
        }
        ESP_LOGW(TAG, "AFT failed (status %02X), rolling back txn %s",
                 ev->aft_status, s_xfer_txn);
        s_xfer = XFER_ROLLBACK;
        ui_transfer_progress("Враќање на средствата...");
        cms_rollback(s_xfer_txn, "aft_failed");
        return;
    }
    if (s_xfer != XFER_CASHOUT) {
        return;
    }
    if (!ev->ok) {
        // Nothing left the machine.
        ESP_LOGW(TAG, "cashout AFT failed (status %02X), txn %s",
                 ev->aft_status, s_xfer_txn);
        finish_transfer();
        ui_toast("Исплатата не успеа", false);
        audio_play(CUE_ERROR);
        return;
    }
    if (ev->credits_cents == 0) {
        // Machine was empty: nothing to credit, just close the session.
        ESP_LOGI(TAG, "cashout: machine empty, logging out (txn %s)", s_xfer_txn);
        finish_transfer();
        if (s_player.sid[0]) {
            cms_logout(s_player.sid, "cashout");
        }
        end_session();
        ui_toast("Одјавени сте", false);
        return;
    }
    // The credits are off the machine — the CMS credit MUST land now.
    s_xfer = XFER_CREDIT;
    s_xfer_amount = ev->credits_cents;
    ui_show_spinner("Ажурирање на салдото...");
    cms_credit(s_player.sid, s_xfer_amount, s_xfer_txn);
}

static void handle_commit_res(const cms_event_t *ev)
{
    if (s_xfer != XFER_COMMIT || !txn_matches(ev->txn)) {
        return;
    }
    if (!ev->ok) {
        if (strcmp(ev->err_code, "timeout") == 0) {
            // Machine already has the credits: keep retrying, never roll back.
            ESP_LOGW(TAG, "commit timeout for %s, retrying", s_xfer_txn);
            cms_commit(s_xfer_txn);
            return;
        }
        // The CMS actively refused an idempotent commit: needs staff.
        ESP_LOGE(TAG, "COMMIT REFUSED for txn %s amount %llu: %s/%s",
                 s_xfer_txn, (unsigned long long)s_xfer_amount,
                 ev->err_code, ev->err_msg);
        ui_toast("Грешка — контактирајте персонал", false);
        audio_play(CUE_ERROR);
        finish_transfer();
        return;
    }
    if (ev->balance_valid) {
        s_player.balance = ev->balance;
    }
    if (ev->points_valid) {
        s_player.points = ev->points;
    }
    ui_update_balance(s_player.balance, s_player.points);

    char amount[32], msg[80];
    ui_fmt_money(amount, sizeof(amount), s_xfer_amount, "МКД");
    snprintf(msg, sizeof(msg), "Успешно префрлени %s", amount);
    ui_toast(msg, true);
    audio_play(CUE_TRANSFER_OK);
    ESP_LOGI(TAG, "transfer complete: txn %s", s_xfer_txn);
    finish_transfer();
}

// ---------------------------------------------------------------- cashout

void app_on_ui_cashout(void)
{
    if (s_state != ST_AUTHED || s_xfer != XFER_NONE) {
        return;
    }
    if (!s_sas_link) {
        ui_toast("Автоматот не е поврзан", false);
        audio_play(CUE_ERROR);
        return;
    }
    cms_make_txn(s_xfer_txn);
    if (sas_aft_cashout(s_xfer_txn) != ESP_OK) {
        s_xfer_txn[0] = '\0';
        ui_toast("Исплатата не успеа", false);
        audio_play(CUE_ERROR);
        return;
    }
    s_xfer = XFER_CASHOUT;
    s_xfer_amount = 0;
    ui_show_spinner("Исплата од автоматот...");
    ESP_LOGI(TAG, "cashout start: txn %s", s_xfer_txn);
}

static void handle_credit_res(const cms_event_t *ev)
{
    if (s_xfer != XFER_CREDIT || !txn_matches(ev->txn)) {
        return;
    }
    if (!ev->ok) {
        if (strcmp(ev->err_code, "timeout") == 0) {
            // The machine already gave up the credits: never drop this.
            ESP_LOGW(TAG, "credit timeout for %s, retrying", s_xfer_txn);
            cms_credit(s_player.sid, s_xfer_amount, s_xfer_txn);
            return;
        }
        // The CMS actively refused an idempotent credit: needs staff.
        ESP_LOGE(TAG, "CREDIT REFUSED for txn %s amount %llu: %s/%s",
                 s_xfer_txn, (unsigned long long)s_xfer_amount,
                 ev->err_code, ev->err_msg);
        finish_transfer();
        ui_toast("Грешка — контактирајте персонал", false);
        audio_play(CUE_ERROR);
        return;
    }
    char amount[32], msg[80];
    ui_fmt_money(amount, sizeof(amount), s_xfer_amount, "МКД");
    snprintf(msg, sizeof(msg), "Исплатени %s", amount);
    ESP_LOGI(TAG, "cashout complete: txn %s", s_xfer_txn);
    finish_transfer();
    if (s_player.sid[0]) {
        cms_logout(s_player.sid, "cashout");
    }
    end_session();
    ui_toast(msg, true);
    audio_play(CUE_TRANSFER_OK);
}

static void handle_rollback_res(const cms_event_t *ev)
{
    if (s_xfer != XFER_ROLLBACK || !txn_matches(ev->txn)) {
        return;
    }
    if (!ev->ok) {
        if (++s_rollback_retries < 3) {
            ESP_LOGW(TAG, "rollback retry %d for txn %s", s_rollback_retries, s_xfer_txn);
            cms_rollback(s_xfer_txn, "aft_failed");
            return;
        }
        ESP_LOGE(TAG, "ROLLBACK FAILED for txn %s amount %llu — funds held in CMS",
                 s_xfer_txn, (unsigned long long)s_xfer_amount);
        ui_toast("Грешка — контактирајте персонал", false);
        audio_play(CUE_ERROR);
        finish_transfer();
        return;
    }
    if (ev->balance_valid) {
        s_player.balance = ev->balance;
        ui_update_balance(s_player.balance, s_player.points);
    }
    ui_toast("Префрлањето не успеа, средствата се вратени", false);
    audio_play(CUE_ERROR);
    finish_transfer();
}

// ---------------------------------------------------------------- links

static void handle_cms_link(bool up)
{
    s_cms_link = up;
    update_offline_badge();
    if (up) {
        // A commit/credit may be waiting out a link outage.
        if (s_xfer == XFER_COMMIT) {
            ESP_LOGI(TAG, "CMS link back, re-sending commit for %s", s_xfer_txn);
            cms_commit(s_xfer_txn);
        } else if (s_xfer == XFER_CREDIT) {
            ESP_LOGI(TAG, "CMS link back, re-sending credit for %s", s_xfer_txn);
            cms_credit(s_player.sid, s_xfer_amount, s_xfer_txn);
        }
        // Any order_push sent while the link was down was dropped on the floor
        // (the CMS pushes to a socket, not to a queue), so re-ask rather than
        // leave the ring frozen on a stale number.
        if (s_state == ST_AUTHED && s_player.sid[0]) {
            cms_orders_req(s_player.sid);
        }
        return;
    }
    if (s_state == ST_IDLE) {
        return;
    }
    if (s_xfer == XFER_COMMIT) {
        // AFT already succeeded — hold the session until the commit lands.
        ui_transfer_progress("Финализирање... (нема врска)");
        return;
    }
    if (s_xfer == XFER_CREDIT) {
        // Same for a cashout credit: the machine already paid out.
        ui_show_spinner("Ажурирање на салдото... (нема врска)");
        return;
    }
    if (s_xfer != XFER_NONE) {
        // Debit/rollback in flight: their timeouts will resolve the chain.
        return;
    }
    end_session();
    ui_toast("Врската со системот е изгубена", false);
}

// ---------------------------------------------------------------- admin

bool app_can_change_network(void)
{
    return s_xfer == XFER_NONE;
}

bool app_on_ui_admin_save(const smib_settings_t *s)
{
    // The money rules come first: a commit or a cashout credit that has
    // already left the machine must not lose its transport mid-retry.
    if (!app_can_change_network()) {
        ESP_LOGW(TAG, "admin save refused: transfer in flight (xfer=%d)", s_xfer);
        ui_toast("Трансакција во тек, обидете се повторно", false);
        return false;
    }
    if (settings_save(s) != ESP_OK) {
        ui_toast("Зачувувањето не успеа", false);
        audio_play(CUE_ERROR);
        return false;
    }
    ESP_LOGI(TAG, "admin applied: ssid '%s', cms %s:%u",
             s->wifi_ssid, s->cms_host, (unsigned)s->cms_port);
    net_connect(s->wifi_ssid, s->wifi_pass);
    cms_link_set_endpoint(s->cms_host, s->cms_port);
    return true;
}

// Cabinet door edges. Closing the cabinet also closes the panel, so the
// terminal can't be left sitting on the configuration screen.
static void handle_door(bool open)
{
    s_door_open = open;
    ESP_LOGI(TAG, "cabinet door %s", open ? "OPEN" : "shut");
    ui_set_door_open(open);
    if (!open) {
        ui_admin_close();
    }
}

// ---------------------------------------------------------------- dispatch

static void handle_cms_event(const cms_event_t *ev)
{
    switch (ev->type) {
    case CMS_EVT_LINK_UP:
        handle_cms_link(true);
        break;
    case CMS_EVT_LINK_DOWN:
        handle_cms_link(false);
        break;
    case CMS_EVT_AUTH_RES:
        handle_auth_res(ev);
        break;
    case CMS_EVT_MENU_RES:
        handle_menu_res(ev);
        break;
    case CMS_EVT_ORDER_RES:
        handle_order_res(ev);
        break;
    case CMS_EVT_ORDER_PUSH:
        handle_order_push(ev);
        break;
    case CMS_EVT_ORDERS_RES:
        handle_orders_res(ev);
        break;
    case CMS_EVT_DEBIT_RES:
        handle_debit_res(ev);
        break;
    case CMS_EVT_COMMIT_RES:
        handle_commit_res(ev);
        break;
    case CMS_EVT_ROLLBACK_RES:
        handle_rollback_res(ev);
        break;
    case CMS_EVT_CREDIT_RES:
        handle_credit_res(ev);
        break;
    case CMS_EVT_BALANCE_PUSH:
        if (s_state == ST_AUTHED) {
            if (ev->balance_valid) {
                s_player.balance = ev->balance;
            }
            if (ev->points_valid) {
                s_player.points = ev->points;
            }
            ui_update_balance(s_player.balance, s_player.points);
        }
        break;
    case CMS_EVT_LOGOUT_PUSH:
        if (s_state != ST_IDLE && s_xfer == XFER_NONE) {
            end_session();
            ui_toast("Одјавени сте", false);
        }
        break;
    }
}

static void handle_sas_event(const sas_event_t *ev)
{
    switch (ev->type) {
    case SAS_EVT_LINK_UP:
        s_sas_link = true;
        update_offline_badge();
        break;
    case SAS_EVT_LINK_DOWN:
        s_sas_link = false;
        update_offline_badge();
        break;
    case SAS_EVT_AFT_RESULT:
        handle_aft_result(ev);
        break;
    case SAS_EVT_EXCEPTION:
        // Machine activity (game start/end etc.) keeps the session alive.
        s_last_sas_activity_us = esp_timer_get_time();
        ESP_LOGI(TAG, "EGM exception %02X", ev->exception);
        break;
    case SAS_EVT_CREDITS:
        s_last_sas_activity_us = esp_timer_get_time();
        ESP_LOGI(TAG, "EGM credits: %llu cents", (unsigned long long)ev->credits_cents);
        break;
    }
}

static void app_handle_event(const app_event_t *e)
{
    switch (e->type) {
    case APP_EVT_CARD_PRESENT:
        handle_card_present(e->u.uid);
        break;
    case APP_EVT_CARD_REMOVED:
        handle_card_removed();
        break;
    case APP_EVT_CMS:
        handle_cms_event(&e->u.cms);
        break;
    case APP_EVT_SAS:
        handle_sas_event(&e->u.sas);
        break;
    }
}

void app_init(void)
{
    s_queue = xQueueCreate(16, sizeof(app_event_t));
    configASSERT(s_queue);
}

void app_tick(void)
{
    app_event_t e;
    while (xQueueReceive(s_queue, &e, 0) == pdTRUE) {
        app_handle_event(&e);
    }
    cms_tick();

    bool door = door_is_open();
    if (door != s_door_open) {
        handle_door(door);
    }

    int64_t now = esp_timer_get_time();

    // Hold window: three rising tones spread across it, then request auth
    // once the card has stayed on the reader the whole time.
    if (s_state == ST_HOLDING) {
        if (s_hold_beeps < APP_HOLD_BEEPS &&
            now - s_hold_start_us >=
                (int64_t)s_hold_beeps * (APP_CARD_HOLD_US / APP_HOLD_BEEPS)) {
            audio_play((audio_cue_t)(CUE_HOLD1 + s_hold_beeps));
            s_hold_beeps++;
        }
        if (now - s_hold_start_us >= APP_CARD_HOLD_US) {
            if (cms_auth(s_auth_uid) != ESP_OK) {
                end_session();
                ui_toast("Грешка при најава", false);
                audio_play(CUE_ERROR);
            } else {
                set_state(ST_AUTHENTICATING);
            }
        }
    }

    // Order countdown. The deadline lives here rather than in an lv_timer so
    // there is exactly one clock behind the ring, and the UI stays a pure
    // renderer of it.
    if (s_order.active && now - s_order_ui_us >= ORDER_UI_PERIOD_US) {
        s_order_ui_us = now;
        if (s_order.settled_us && now - s_order.settled_us >= ORDER_DWELL_US) {
            // Poured or cancelled, and read by now.
            order_clear();
        } else {
            order_render();
        }
    }

    // Walk-away protection: quiet authed session -> logout. Never while a
    // money chain or a pending menu/order request is in flight, and not while
    // a drink is still coming -- but a cooking order only defers the logout
    // until its promised time plus some slack, so an order the bar forgets
    // cannot pin a session open for the rest of the night.
    bool order_holds_session =
        s_order.active && !s_order.settled_us && now < s_order.zero_us + ORDER_GRACE_US;
    if (s_state == ST_AUTHED && s_xfer == XFER_NONE &&
        !s_menu_requested && !s_order_pending && !order_holds_session &&
        ui_touch_idle_ms() > APP_IDLE_LOGOUT_US / 1000 &&
        now - s_last_sas_activity_us > APP_IDLE_LOGOUT_US) {
        ESP_LOGI(TAG, "inactivity logout (sid %s)", s_player.sid);
        if (s_player.sid[0]) {
            cms_logout(s_player.sid, "inactivity");
        }
        end_session();
        ui_toast("Одјавени сте", false);
    }
}

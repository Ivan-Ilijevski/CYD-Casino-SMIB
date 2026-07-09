#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_log.h"

#include "drivers/audio.h"
#include "cms/cms.h"
#include "sas/sas.h"
#include "ui/ui.h"
#include "app_events.h"
#include "app.h"

static const char *TAG = "app";

// Funds-transfer chain: CMS debit (hold) -> SAS AFT -> CMS commit.
// Money safety rules:
//   - AFT failed  -> rollback the CMS hold (retry up to 3x).
//   - AFT succeeded but commit unanswered -> NEVER rollback; retry commit
//     until the CMS confirms (commit is idempotent by txn).
typedef enum {
    XFER_NONE,
    XFER_DEBIT,     // waiting for debit_res
    XFER_AFT,       // waiting for SAS AFT result
    XFER_COMMIT,    // waiting for debit_commit_res (retries forever)
    XFER_ROLLBACK,  // waiting for debit_rollback_res (retries 3x)
} xfer_state_t;

static QueueHandle_t s_queue;
static app_state_t s_state = ST_IDLE;
static player_info_t s_player;
static bool s_cms_link;
static bool s_sas_link;

static xfer_state_t s_xfer = XFER_NONE;
static char s_xfer_txn[24];
static uint64_t s_xfer_amount;
static int s_rollback_retries;
static bool s_logout_pending;

static bool s_menu_requested;
static bool s_order_pending;

static const char *state_name(app_state_t st)
{
    switch (st) {
    case ST_IDLE:           return "IDLE";
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
    s_menu_requested = false;
    s_order_pending = false;
    s_logout_pending = false;
    set_state(ST_IDLE);
    ui_show_idle();
}

// Called when a transfer chain fully resolves (success or failure).
static void finish_transfer(void)
{
    s_xfer = XFER_NONE;
    s_xfer_txn[0] = '\0';
    ui_transfer_close();
    if (s_logout_pending) {
        ESP_LOGI(TAG, "deferred logout after transfer resolution");
        if (s_player.sid[0]) {
            cms_logout(s_player.sid, "card_removed");
        }
        end_session();
    }
}

// ---------------------------------------------------------------- card

static void handle_card_present(const char *uid)
{
    ESP_LOGI(TAG, "card present, uid=%s (state=%s)", uid, state_name(s_state));
    if (s_state != ST_IDLE) {
        return;
    }
    audio_play(CUE_SCAN);
    if (!s_cms_link || uid[0] == '\0') {
        ui_toast("Нема врска со системот", false);
        audio_play(CUE_ERROR);
        return;
    }
    if (cms_auth(uid) != ESP_OK) {
        ui_toast("Грешка при најава", false);
        audio_play(CUE_ERROR);
        return;
    }
    set_state(ST_AUTHENTICATING);
    ui_show_spinner("Проверка на картичката…");
}

static void handle_card_removed(void)
{
    ESP_LOGI(TAG, "card removed (state=%s, xfer=%d)", state_name(s_state), s_xfer);
    if (s_state == ST_IDLE) {
        return;
    }
    if (s_xfer != XFER_NONE) {
        // Never abort a money chain mid-flight; resolve first, then log out.
        s_logout_pending = true;
        return;
    }
    if (s_player.sid[0]) {
        cms_logout(s_player.sid, "card_removed");
    }
    end_session();
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
    ui_show_main(&s_player);
    audio_play(CUE_AUTH_OK);
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
    ui_show_spinner("Вчитување на менито…");
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
    if (s_state != ST_AUTHED) {
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
    ui_show_spinner("Нарачката се испраќа…");
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
    ui_toast("Нарачката е примена", true);
    audio_play(CUE_ORDER_OK);
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
    ui_transfer_progress("Резервација на средствата…");
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
    ui_transfer_progress("Префрлање на автоматот…");
    if (sas_aft_transfer(s_xfer_amount, s_xfer_txn) != ESP_OK) {
        ESP_LOGW(TAG, "AFT not accepted, rolling back txn %s", s_xfer_txn);
        s_xfer = XFER_ROLLBACK;
        ui_transfer_progress("Враќање на средствата…");
        cms_rollback(s_xfer_txn, "aft_unavailable");
    }
}

static void handle_aft_result(const sas_event_t *ev)
{
    if (s_xfer != XFER_AFT || !txn_matches(ev->txn)) {
        return;
    }
    if (ev->ok) {
        s_xfer = XFER_COMMIT;
        ui_transfer_progress("Финализирање…");
        cms_commit(s_xfer_txn);
        return;
    }
    ESP_LOGW(TAG, "AFT failed (status %02X), rolling back txn %s",
             ev->aft_status, s_xfer_txn);
    s_xfer = XFER_ROLLBACK;
    ui_transfer_progress("Враќање на средствата…");
    cms_rollback(s_xfer_txn, "aft_failed");
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
        // A commit may be waiting out a link outage.
        if (s_xfer == XFER_COMMIT) {
            ESP_LOGI(TAG, "CMS link back, re-sending commit for %s", s_xfer_txn);
            cms_commit(s_xfer_txn);
        }
        return;
    }
    if (s_state == ST_IDLE) {
        return;
    }
    if (s_xfer == XFER_COMMIT) {
        // AFT already succeeded — hold the session until the commit lands.
        ui_transfer_progress("Финализирање… (нема врска)");
        return;
    }
    if (s_xfer != XFER_NONE) {
        // Debit/rollback in flight: their timeouts will resolve the chain.
        return;
    }
    end_session();
    ui_toast("Врската со системот е изгубена", false);
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
    case CMS_EVT_DEBIT_RES:
        handle_debit_res(ev);
        break;
    case CMS_EVT_COMMIT_RES:
        handle_commit_res(ev);
        break;
    case CMS_EVT_ROLLBACK_RES:
        handle_rollback_res(ev);
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
        ESP_LOGI(TAG, "EGM exception %02X", ev->exception);
        break;
    case SAS_EVT_CREDITS:
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
}

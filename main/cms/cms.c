#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "cJSON.h"

#include "board.h"
#include "mux/mux.h"
#include "app/app_events.h"
#include "cms.h"

static const char *TAG = "cms";

#define CMS_PENDING_SLOTS      8
#define CMS_REQUEST_TIMEOUT_US (3 * 1000 * 1000)
#define CMS_PING_PERIOD_US     (2 * 1000 * 1000)
#define CMS_LINK_TIMEOUT_US    (6 * 1000 * 1000)

typedef struct {
    uint32_t id;            // 0 = slot free
    cms_evt_type_t res_type;
    int64_t deadline_us;
    char txn[24];
} pending_t;

static SemaphoreHandle_t s_lock;    // guards pending table, menu, last_rx
static pending_t s_pending[CMS_PENDING_SLOTS];
static cms_menu_t s_menu;
static int64_t s_last_rx_us = -CMS_LINK_TIMEOUT_US;
static uint32_t s_next_id = 1;
static uint32_t s_next_txn = 1;

// Only touched from the app task (cms_tick).
static bool s_link_up;
static int64_t s_last_ping_us;

bool cms_link_up(void)
{
    return s_link_up;
}

// ---------------------------------------------------------------- TX side

static esp_err_t send_json(cJSON *root)
{
    char *text = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!text) {
        return ESP_ERR_NO_MEM;
    }
    size_t len = strlen(text);
    esp_err_t err = len <= MUX_MAX_PAYLOAD
                        ? mux_send(MUX_CH_CMS, (const uint8_t *)text, (uint16_t)len)
                        : ESP_ERR_INVALID_SIZE;
    cJSON_free(text);
    return err;
}

static cJSON *make_envelope(const char *type, uint32_t id, cJSON **out_d)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "t", type);
    cJSON_AddNumberToObject(root, "id", id);
    cJSON *d = cJSON_AddObjectToObject(root, "d");
    *out_d = d;
    return root;
}

static esp_err_t track_pending(uint32_t id, cms_evt_type_t res_type, const char *txn)
{
    esp_err_t err = ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < CMS_PENDING_SLOTS; i++) {
        if (s_pending[i].id == 0) {
            s_pending[i].id = id;
            s_pending[i].res_type = res_type;
            s_pending[i].deadline_us = esp_timer_get_time() + CMS_REQUEST_TIMEOUT_US;
            snprintf(s_pending[i].txn, sizeof(s_pending[i].txn), "%s", txn ? txn : "");
            err = ESP_OK;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "pending table full");
    }
    return err;
}

static void untrack_pending(uint32_t id)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < CMS_PENDING_SLOTS; i++) {
        if (s_pending[i].id == id) {
            s_pending[i].id = 0;
            break;
        }
    }
    xSemaphoreGive(s_lock);
}

// Sends a request and registers the expected response. On send failure the
// pending entry is not created and the error is returned to the caller.
static esp_err_t request(cJSON *root, uint32_t id, cms_evt_type_t res_type,
                         const char *txn)
{
    esp_err_t err = track_pending(id, res_type, txn);
    if (err != ESP_OK) {
        cJSON_Delete(root);
        return err;
    }
    err = send_json(root);
    if (err != ESP_OK) {
        untrack_pending(id);
    }
    return err;
}

static esp_err_t send_hello(void)
{
    cJSON *d;
    cJSON *root = make_envelope("hello", s_next_id++, &d);
    cJSON_AddStringToObject(d, "dev", SMIB_DEVICE_ID);
    cJSON_AddStringToObject(d, "fw", SMIB_FW_VERSION);
    cJSON_AddNumberToObject(d, "proto", 1);
    return send_json(root);
}

esp_err_t cms_auth(const char *uid)
{
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("auth_req", id, &d);
    cJSON_AddStringToObject(d, "uid", uid);
    return request(root, id, CMS_EVT_AUTH_RES, NULL);
}

esp_err_t cms_logout(const char *sid, const char *reason)
{
    cJSON *d;
    cJSON *root = make_envelope("logout", s_next_id++, &d);
    cJSON_AddStringToObject(d, "sid", sid);
    cJSON_AddStringToObject(d, "reason", reason);
    return send_json(root); // fire-and-forget: no pending entry
}

esp_err_t cms_debit(const char *sid, uint64_t cents, char txn_out[24])
{
    snprintf(txn_out, 24, "SMIB-%06u", (unsigned)(s_next_txn++));
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("debit_req", id, &d);
    cJSON_AddStringToObject(d, "sid", sid);
    cJSON_AddStringToObject(d, "txn", txn_out);
    cJSON_AddNumberToObject(d, "amount", (double)cents);
    return request(root, id, CMS_EVT_DEBIT_RES, txn_out);
}

esp_err_t cms_commit(const char *txn)
{
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("debit_commit", id, &d);
    cJSON_AddStringToObject(d, "txn", txn);
    return request(root, id, CMS_EVT_COMMIT_RES, txn);
}

esp_err_t cms_rollback(const char *txn, const char *reason)
{
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("debit_rollback", id, &d);
    cJSON_AddStringToObject(d, "txn", txn);
    cJSON_AddStringToObject(d, "reason", reason);
    return request(root, id, CMS_EVT_ROLLBACK_RES, txn);
}

esp_err_t cms_fetch_menu(void)
{
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("menu_req", id, &d);
    return request(root, id, CMS_EVT_MENU_RES, NULL);
}

esp_err_t cms_order(const char *sid, const cms_order_item_t *items, size_t n,
                    bool pay_points)
{
    uint32_t id = s_next_id++;
    cJSON *d;
    cJSON *root = make_envelope("order_req", id, &d);
    cJSON_AddStringToObject(d, "sid", sid);
    cJSON_AddStringToObject(d, "pay", pay_points ? "points" : "cash");
    cJSON *arr = cJSON_AddArrayToObject(d, "items");
    for (size_t i = 0; i < n; i++) {
        cJSON *it = cJSON_CreateObject();
        cJSON_AddNumberToObject(it, "drink", items[i].drink);
        cJSON_AddNumberToObject(it, "qty", items[i].qty);
        cJSON_AddItemToArray(arr, it);
    }
    return request(root, id, CMS_EVT_ORDER_RES, NULL);
}

esp_err_t cms_get_menu(cms_menu_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_menu;
    xSemaphoreGive(s_lock);
    return out->count > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

// ---------------------------------------------------------------- RX side
// Runs on the mux_rx task: parse, copy into POD event, post to the app task.

static void json_scopy(const cJSON *obj, const char *key, char *dst, size_t n)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        snprintf(dst, n, "%s", item->valuestring);
    } else {
        dst[0] = '\0';
    }
}

static bool json_u64(const cJSON *obj, const char *key, uint64_t *out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        *out = (uint64_t)item->valuedouble;
        return true;
    }
    return false;
}

static bool json_i32(const cJSON *obj, const char *key, int32_t *out)
{
    const cJSON *item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item)) {
        *out = (int32_t)item->valuedouble;
        return true;
    }
    return false;
}

static void fill_common(cms_event_t *ev, const cJSON *d)
{
    const cJSON *ok = cJSON_GetObjectItem(d, "ok");
    ev->ok = cJSON_IsTrue(ok);
    const cJSON *err = cJSON_GetObjectItem(d, "err");
    if (cJSON_IsObject(err)) {
        json_scopy(err, "code", ev->err_code, sizeof(ev->err_code));
        json_scopy(err, "msg", ev->err_msg, sizeof(ev->err_msg));
    }
    json_scopy(d, "sid", ev->sid, sizeof(ev->sid));
    json_scopy(d, "txn", ev->txn, sizeof(ev->txn));
    ev->balance_valid = json_u64(d, "balance", &ev->balance);
    ev->points_valid = json_i32(d, "points", &ev->points);
}

static void parse_menu(const cJSON *d)
{
    cms_menu_t menu = { 0 };
    const cJSON *items = cJSON_GetObjectItem(d, "items");
    const cJSON *it;
    cJSON_ArrayForEach(it, items) {
        if (menu.count >= CMS_MENU_MAX_ITEMS) {
            break;
        }
        cms_menu_item_t *m = &menu.items[menu.count];
        if (!json_i32(it, "drink", &m->id) || !json_u64(it, "price", &m->price)) {
            continue;
        }
        json_scopy(it, "name", m->name, sizeof(m->name));
        json_i32(it, "points_price", &m->points_price);
        m->avail = cJSON_IsTrue(cJSON_GetObjectItem(it, "avail"));
        menu.count++;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_menu = menu;
    xSemaphoreGive(s_lock);
}

typedef struct {
    const char *name;
    cms_evt_type_t type;
    bool is_push;
} msg_map_t;

static const msg_map_t k_msg_map[] = {
    { "auth_res",           CMS_EVT_AUTH_RES,     false },
    { "debit_res",          CMS_EVT_DEBIT_RES,    false },
    { "debit_commit_res",   CMS_EVT_COMMIT_RES,   false },
    { "debit_rollback_res", CMS_EVT_ROLLBACK_RES, false },
    { "menu_res",           CMS_EVT_MENU_RES,     false },
    { "order_res",          CMS_EVT_ORDER_RES,    false },
    { "balance_push",       CMS_EVT_BALANCE_PUSH, true },
    { "logout_push",        CMS_EVT_LOGOUT_PUSH,  true },
};

static void cms_rx_cb(const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)ctx;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_last_rx_us = esp_timer_get_time();
    xSemaphoreGive(s_lock);

    cJSON *root = cJSON_ParseWithLength((const char *)payload, len);
    if (!root) {
        ESP_LOGW(TAG, "bad JSON (%u bytes)", len);
        return;
    }

    const cJSON *t = cJSON_GetObjectItem(root, "t");
    const cJSON *idj = cJSON_GetObjectItem(root, "id");
    const cJSON *d = cJSON_GetObjectItem(root, "d");
    if (!cJSON_IsString(t) || !cJSON_IsObject(d)) {
        cJSON_Delete(root);
        return;
    }
    uint32_t id = cJSON_IsNumber(idj) ? (uint32_t)idj->valuedouble : 0;

    // pong / hello_res only refresh the link (already done above).
    if (strcmp(t->valuestring, "pong") == 0 || strcmp(t->valuestring, "hello_res") == 0) {
        cJSON_Delete(root);
        return;
    }

    const msg_map_t *m = NULL;
    for (size_t i = 0; i < sizeof(k_msg_map) / sizeof(k_msg_map[0]); i++) {
        if (strcmp(t->valuestring, k_msg_map[i].name) == 0) {
            m = &k_msg_map[i];
            break;
        }
    }
    if (!m) {
        ESP_LOGW(TAG, "unknown CMS message '%s'", t->valuestring);
        cJSON_Delete(root);
        return;
    }

    if (!m->is_push) {
        untrack_pending(id);
    }

    app_event_t e = { .type = APP_EVT_CMS };
    e.u.cms.type = m->type;
    fill_common(&e.u.cms, d);
    if (m->is_push) {
        e.u.cms.ok = true;
    }

    if (m->type == CMS_EVT_AUTH_RES && e.u.cms.ok) {
        const cJSON *player = cJSON_GetObjectItem(d, "player");
        if (cJSON_IsObject(player)) {
            json_scopy(player, "name", e.u.cms.player_name, sizeof(e.u.cms.player_name));
            json_scopy(player, "cur", e.u.cms.cur, sizeof(e.u.cms.cur));
            e.u.cms.balance_valid = json_u64(player, "balance", &e.u.cms.balance);
            e.u.cms.points_valid = json_i32(player, "points", &e.u.cms.points);
        }
    } else if (m->type == CMS_EVT_MENU_RES && e.u.cms.ok) {
        parse_menu(d);
    }

    cJSON_Delete(root);
    app_post_event(&e);
}

// ---------------------------------------------------------------- tick

void cms_tick(void)
{
    int64_t now = esp_timer_get_time();

    // Keepalive
    if (now - s_last_ping_us > CMS_PING_PERIOD_US) {
        s_last_ping_us = now;
        cJSON *d;
        cJSON *root = make_envelope("ping", s_next_id++, &d);
        send_json(root);
    }

    // Link edges
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int64_t last_rx = s_last_rx_us;
    xSemaphoreGive(s_lock);
    bool up = (now - last_rx) < CMS_LINK_TIMEOUT_US;
    if (up != s_link_up) {
        s_link_up = up;
        ESP_LOGI(TAG, "CMS link %s", up ? "up" : "down");
        if (up) {
            send_hello();
        }
        app_event_t e = { .type = APP_EVT_CMS };
        e.u.cms.type = up ? CMS_EVT_LINK_UP : CMS_EVT_LINK_DOWN;
        e.u.cms.ok = up;
        app_post_event(&e);
    }

    // Request timeouts
    while (1) {
        cms_event_t timed_out;
        bool found = false;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        for (int i = 0; i < CMS_PENDING_SLOTS; i++) {
            if (s_pending[i].id != 0 && now > s_pending[i].deadline_us) {
                memset(&timed_out, 0, sizeof(timed_out));
                timed_out.type = s_pending[i].res_type;
                timed_out.ok = false;
                snprintf(timed_out.err_code, sizeof(timed_out.err_code), "timeout");
                snprintf(timed_out.err_msg, sizeof(timed_out.err_msg), "Нема одговор од системот");
                snprintf(timed_out.txn, sizeof(timed_out.txn), "%s", s_pending[i].txn);
                s_pending[i].id = 0;
                found = true;
                break;
            }
        }
        xSemaphoreGive(s_lock);
        if (!found) {
            break;
        }
        ESP_LOGW(TAG, "CMS request timed out (res type %d, txn '%s')",
                 timed_out.type, timed_out.txn);
        app_event_t e = { .type = APP_EVT_CMS };
        e.u.cms = timed_out;
        app_post_event(&e);
    }
}

esp_err_t cms_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    return mux_register_rx(MUX_CH_CMS, cms_rx_cb, NULL);
}

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "mux/mux.h"
#include "mux/crc16.h"
#include "app/app_events.h"
#include "sas_defs.h"
#include "sas.h"

static const char *TAG = "sas";

#define SAS_POLL_PERIOD_MS      100
#define SAS_RESPONSE_TIMEOUT_MS 300
#define SAS_LINK_FAIL_COUNT     5
#define SAS_AFT_POLL_MS         500
#define SAS_AFT_PENDING_CAP_US  (10 * 1000 * 1000)
#define SAS_MAX_FRAME           128

typedef struct {
    uint16_t len;
    uint8_t buf[SAS_MAX_FRAME];
} sas_frame_t;

typedef struct {
    uint64_t amount_cents;
    char txn[24];
} aft_job_t;

static QueueHandle_t s_rx_queue;   // mux_rx task -> sas_task
static QueueHandle_t s_aft_queue;  // app task -> sas_task (depth 1)
static SemaphoreHandle_t s_status_lock;
static sas_status_t s_status;

// sas_task-only state
static bool s_link;
static int s_timeouts;
static bool s_info_polled;
static uint32_t s_cycle;
static bool s_general_b;

// ---------------------------------------------------------------- helpers

static void post_sas_event(const sas_event_t *ev)
{
    app_event_t e = { .type = APP_EVT_SAS };
    e.u.sas = *ev;
    app_post_event(&e);
}

void sas_get_status(sas_status_t *out)
{
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    *out = s_status;
    xSemaphoreGive(s_status_lock);
}

static void sas_rx_cb(const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)ctx;
    if (len == 0 || len > SAS_MAX_FRAME) {
        ESP_LOGW(TAG, "oversized/empty SAS frame (%u)", len);
        return;
    }
    sas_frame_t f;
    f.len = len;
    memcpy(f.buf, payload, len);
    if (xQueueSend(s_rx_queue, &f, 0) != pdTRUE) {
        ESP_LOGW(TAG, "rx queue full, frame dropped");
    }
}

// Sends raw SAS bytes as one mux frame and waits for the EGM's reply.
static bool sas_exchange(const uint8_t *tx, size_t tx_len, sas_frame_t *rx)
{
    xQueueReset(s_rx_queue); // discard any stale/unsolicited frame
    if (mux_send(MUX_CH_SAS, tx, (uint16_t)tx_len) != ESP_OK) {
        return false;
    }
    return xQueueReceive(s_rx_queue, rx, pdMS_TO_TICKS(SAS_RESPONSE_TIMEOUT_MS)) == pdTRUE;
}

// Long poll: appends the SAS CRC to cmd+body, sends, validates the echo and
// CRC of the response. Returns response length (or -1).
static int sas_long_poll(uint8_t cmd, const uint8_t *body, size_t body_len,
                         sas_frame_t *rx)
{
    uint8_t tx[SAS_MAX_FRAME];
    size_t n = 0;
    tx[n++] = SAS_EGM_ADDR;
    tx[n++] = cmd;
    if (body_len) {
        memcpy(tx + n, body, body_len);
        n += body_len;
    }
    uint16_t crc = crc16_kermit(tx, n);
    tx[n++] = (uint8_t)(crc & 0xFF);
    tx[n++] = (uint8_t)(crc >> 8);

    if (!sas_exchange(tx, n, rx)) {
        return -1;
    }
    if (rx->len < 4 || rx->buf[0] != SAS_EGM_ADDR || rx->buf[1] != cmd) {
        ESP_LOGW(TAG, "long poll %02X: bad echo", cmd);
        return -1;
    }
    uint16_t rx_crc = (uint16_t)rx->buf[rx->len - 2] | ((uint16_t)rx->buf[rx->len - 1] << 8);
    if (crc16_kermit(rx->buf, rx->len - 2) != rx_crc) {
        ESP_LOGW(TAG, "long poll %02X: bad CRC", cmd);
        return -1;
    }
    return rx->len;
}

// ---------------------------------------------------------------- link

static void mark_response_good(void)
{
    s_timeouts = 0;
    if (!s_link) {
        s_link = true;
        s_info_polled = false;
        ESP_LOGI(TAG, "SAS link up");
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_status.link_up = true;
        xSemaphoreGive(s_status_lock);
        sas_event_t ev = { .type = SAS_EVT_LINK_UP };
        post_sas_event(&ev);
    }
}

static void mark_response_timeout(void)
{
    if (++s_timeouts >= SAS_LINK_FAIL_COUNT && s_link) {
        s_link = false;
        ESP_LOGW(TAG, "SAS link down (%d consecutive timeouts)", s_timeouts);
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_status.link_up = false;
        xSemaphoreGive(s_status_lock);
        sas_event_t ev = { .type = SAS_EVT_LINK_DOWN };
        post_sas_event(&ev);
    }
}

// ---------------------------------------------------------------- polls

static void do_general_poll(void)
{
    uint8_t poll = s_general_b ? SAS_GENERAL_POLL_B : SAS_GENERAL_POLL_A;
    s_general_b = !s_general_b;

    sas_frame_t rx;
    if (!sas_exchange(&poll, 1, &rx)) {
        mark_response_timeout();
        return;
    }
    mark_response_good();
    if (rx.len != 1) {
        ESP_LOGW(TAG, "general poll: unexpected %u-byte response", rx.len);
        return;
    }
    uint8_t exc = rx.buf[0];
    if (exc != SAS_EXC_NONE) {
        ESP_LOGI(TAG, "exception %02X", exc);
        sas_event_t ev = { .type = SAS_EVT_EXCEPTION, .exception = exc };
        post_sas_event(&ev);
    }
}

static void do_current_credits(void)
{
    sas_frame_t rx;
    if (sas_long_poll(SAS_CMD_CURRENT_CREDITS, NULL, 0, &rx) < 0) {
        mark_response_timeout();
        return;
    }
    mark_response_good();
    if (rx.len != 2 + 4 + 2) {
        return;
    }
    uint64_t credits = sas_bcd_to_u64(rx.buf + 2, 4);
    bool changed = false;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    changed = (s_status.credits_cents != credits);
    s_status.credits_cents = credits;
    xSemaphoreGive(s_status_lock);
    if (changed) {
        sas_event_t ev = { .type = SAS_EVT_CREDITS, .credits_cents = credits };
        post_sas_event(&ev);
    }
}

static void do_total_meters(void)
{
    sas_frame_t rx;
    if (sas_long_poll(SAS_CMD_TOTAL_METERS, NULL, 0, &rx) < 0) {
        mark_response_timeout();
        return;
    }
    mark_response_good();
    if (rx.len != 2 + 5 * 4 + 2) {
        return;
    }
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    s_status.m_coin_in  = sas_bcd_to_u64(rx.buf + 2, 4);
    s_status.m_coin_out = sas_bcd_to_u64(rx.buf + 6, 4);
    s_status.m_drop     = sas_bcd_to_u64(rx.buf + 10, 4);
    s_status.m_jackpot  = sas_bcd_to_u64(rx.buf + 14, 4);
    s_status.m_games    = sas_bcd_to_u64(rx.buf + 18, 4);
    xSemaphoreGive(s_status_lock);
}

static void do_info_polls(void)
{
    sas_frame_t rx;

    // 0x1F machine ID/info: game id (2 ASCII) + additional id (3) + denom +
    // max bet + progressive group + game options (2) + paytable (6) + base% (4)
    if (sas_long_poll(SAS_CMD_MACHINE_ID, NULL, 0, &rx) >= 2 + 20 + 2) {
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        memcpy(s_status.game_id, rx.buf + 2, 2);
        s_status.game_id[2] = '\0';
        xSemaphoreGive(s_status_lock);
    }

    // 0x54: length + SAS version (3 ASCII) + serial (ASCII, <= 40)
    if (sas_long_poll(SAS_CMD_VERSION_SERIAL, NULL, 0, &rx) >= 2 + 1 + 3 + 2) {
        uint8_t body_len = rx.buf[2];
        if (body_len >= 3 && (size_t)body_len + 5 <= rx.len) {
            xSemaphoreTake(s_status_lock, portMAX_DELAY);
            memcpy(s_status.sas_version, rx.buf + 3, 3);
            s_status.sas_version[3] = '\0';
            size_t serial_len = body_len - 3;
            if (serial_len > sizeof(s_status.serial) - 1) {
                serial_len = sizeof(s_status.serial) - 1;
            }
            memcpy(s_status.serial, rx.buf + 6, serial_len);
            s_status.serial[serial_len] = '\0';
            xSemaphoreGive(s_status_lock);
            ESP_LOGI(TAG, "EGM SAS v%s serial '%s'", s_status.sas_version, s_status.serial);
        }
    }

    // 0x73 AFT registration interrogate: length=1, code 0xFF
    uint8_t body[] = { 0x01, 0xFF };
    // (length byte + interrogate code; some EGMs expect the length prefix)
    if (sas_long_poll(SAS_CMD_AFT_REGISTER, body, sizeof(body), &rx) >= 2 + 1 + 1 + 4 + 2) {
        uint8_t reg_status = rx.buf[3];
        uint32_t asset = (uint32_t)rx.buf[4] | ((uint32_t)rx.buf[5] << 8) |
                         ((uint32_t)rx.buf[6] << 16) | ((uint32_t)rx.buf[7] << 24);
        xSemaphoreTake(s_status_lock, portMAX_DELAY);
        s_status.aft_reg_status = reg_status;
        s_status.asset_number = asset;
        xSemaphoreGive(s_status_lock);
        if (reg_status == SAS_AFT_REG_NOT_REG) {
            ESP_LOGW(TAG, "EGM not AFT-registered (status 0x80); transfers may be refused");
        }
        ESP_LOGI(TAG, "AFT reg status %02X asset %u", reg_status, (unsigned)asset);
    }

    s_info_polled = true;
}

// ---------------------------------------------------------------- AFT

// Parses a 0x72 response; returns transfer status byte or -1.
static int parse_aft_response(const sas_frame_t *rx, uint64_t *out_cashable)
{
    // [0]=addr [1]=0x72 [2]=len [3]=buffer pos [4]=transfer status ...
    if (rx->len < 5 + 2) {
        return -1;
    }
    if (out_cashable && rx->len >= 2 + 1 + 4 + 15 + 2) {
        // cashable BCD at offset 7 (buffer pos, status, receipt status, type)
        *out_cashable = sas_bcd_to_u64(rx->buf + 7, 5);
    }
    return rx->buf[4];
}

static void run_aft_job(const aft_job_t *job)
{
    ESP_LOGI(TAG, "AFT transfer %llu cents, txn %s",
             (unsigned long long)job->amount_cents, job->txn);

    uint32_t asset;
    xSemaphoreTake(s_status_lock, portMAX_DELAY);
    asset = s_status.asset_number;
    xSemaphoreGive(s_status_lock);

    // Build the 0x72 full-transfer body (pragmatic subset, zero reg key).
    uint8_t body[SAS_MAX_FRAME];
    size_t n = 0;
    body[n++] = 0; // length placeholder
    body[n++] = SAS_AFT_CODE_FULL;          // transfer code
    body[n++] = 0x00;                       // transaction index
    body[n++] = 0x00;                       // transfer type: in-house cashable -> EGM
    sas_u64_to_bcd(job->amount_cents, body + n, 5); n += 5;  // cashable
    memset(body + n, 0, 5); n += 5;         // restricted
    memset(body + n, 0, 5); n += 5;         // nonrestricted
    body[n++] = 0x00;                       // transfer flags
    body[n++] = (uint8_t)(asset & 0xFF);    // asset number, little-endian
    body[n++] = (uint8_t)((asset >> 8) & 0xFF);
    body[n++] = (uint8_t)((asset >> 16) & 0xFF);
    body[n++] = (uint8_t)((asset >> 24) & 0xFF);
    memset(body + n, 0, 20); n += 20;       // registration key (unregistered)
    size_t txn_len = strlen(job->txn);
    body[n++] = (uint8_t)txn_len;
    memcpy(body + n, job->txn, txn_len); n += txn_len;
    memset(body + n, 0, 4); n += 4;         // expiration BCD
    body[n++] = 0x00; body[n++] = 0x00;     // pool ID
    body[n++] = 0x00;                       // receipt data length
    body[n++] = 0x00; body[n++] = 0x00;     // lock timeout
    body[0] = (uint8_t)(n - 1);             // length of everything after it

    sas_frame_t rx;
    int status;
    if (sas_long_poll(SAS_CMD_AFT_TRANSFER, body, n, &rx) < 0) {
        // No answer to the request itself: interrogate below before failing.
        status = SAS_AFT_STATUS_PENDING;
    } else {
        status = parse_aft_response(&rx, NULL);
        if (status < 0) {
            status = SAS_AFT_STATUS_PENDING;
        }
    }

    int64_t start = esp_timer_get_time();
    while (status == SAS_AFT_STATUS_PENDING &&
           esp_timer_get_time() - start < SAS_AFT_PENDING_CAP_US) {
        vTaskDelay(pdMS_TO_TICKS(SAS_AFT_POLL_MS));
        uint8_t interrogate[] = { 0x02, SAS_AFT_CODE_INTERROGATE, 0x00 };
        if (sas_long_poll(SAS_CMD_AFT_TRANSFER, interrogate, sizeof(interrogate), &rx) >= 0) {
            int st = parse_aft_response(&rx, NULL);
            if (st >= 0) {
                status = st;
            }
        }
    }

    bool ok = (status == SAS_AFT_STATUS_FULL_OK);
    ESP_LOGI(TAG, "AFT result: %s (status %02X) txn %s",
             ok ? "success" : "FAILED", (unsigned)status, job->txn);

    sas_event_t ev = { .type = SAS_EVT_AFT_RESULT, .ok = ok,
                       .aft_status = (uint8_t)status };
    snprintf(ev.txn, sizeof(ev.txn), "%s", job->txn);
    post_sas_event(&ev);
}

esp_err_t sas_aft_transfer(uint64_t amount_cents, const char *txn_id)
{
    if (!s_link) {
        return ESP_ERR_INVALID_STATE;
    }
    aft_job_t job = { .amount_cents = amount_cents };
    snprintf(job.txn, sizeof(job.txn), "%s", txn_id);
    return xQueueSend(s_aft_queue, &job, 0) == pdTRUE ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// ---------------------------------------------------------------- task

static void sas_task(void *arg)
{
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(SAS_POLL_PERIOD_MS));

        aft_job_t job;
        if (xQueueReceive(s_aft_queue, &job, 0) == pdTRUE) {
            run_aft_job(&job);
            continue;
        }

        if (s_link && !s_info_polled) {
            do_info_polls();
            continue;
        }

        s_cycle++;
        if (s_link && s_cycle % 50 == 0) {
            do_total_meters();
        } else if (s_link && s_cycle % 10 == 0) {
            do_current_credits();
        } else {
            do_general_poll();
        }
    }
}

esp_err_t sas_init(void)
{
    s_status_lock = xSemaphoreCreateMutex();
    s_rx_queue = xQueueCreate(4, sizeof(sas_frame_t));
    s_aft_queue = xQueueCreate(1, sizeof(aft_job_t));
    if (!s_status_lock || !s_rx_queue || !s_aft_queue) {
        return ESP_ERR_NO_MEM;
    }
    ESP_ERROR_CHECK(mux_register_rx(MUX_CH_SAS, sas_rx_cb, NULL));
    BaseType_t ok = xTaskCreate(sas_task, "sas", 4096, NULL, 9, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

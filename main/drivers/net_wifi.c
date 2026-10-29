#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"

#include "net.h"

static const char *TAG = "net";

// How long to wait before re-associating after a disconnect. Long enough not
// to hammer a missing AP, short enough that a router reboot heals on its own.
#define NET_RETRY_DELAY_US (2 * 1000 * 1000)

static esp_netif_t *s_netif;
static volatile net_state_t s_state = NET_DOWN;
static esp_timer_handle_t s_retry_timer;
static bool s_want_connect;         // false = parked, don't retry

static SemaphoreHandle_t s_lock;    // guards the scan results and s_ip
static net_ap_t s_scan[NET_SCAN_MAX];
static size_t s_scan_count;
static volatile bool s_scan_busy;
static char s_ip[16];

net_state_t net_state(void)
{
    return s_state;
}

const char *net_state_name(net_state_t st)
{
    switch (st) {
    case NET_DOWN:       return "DOWN";
    case NET_CONNECTING: return "CONNECTING";
    case NET_UP:         return "UP";
    }
    return "?";
}

bool net_ip(char *out, size_t out_size)
{
    bool have;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    have = s_ip[0] != '\0';
    if (have && out && out_size > 0) {
        snprintf(out, out_size, "%s", s_ip);
    }
    xSemaphoreGive(s_lock);
    return have;
}

int net_rssi(void)
{
    wifi_ap_record_t ap;
    if (s_state == NET_UP && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        return ap.rssi;
    }
    return 0;
}

// ---------------------------------------------------------------- events

static void retry_cb(void *arg)
{
    (void)arg;
    if (s_want_connect) {
        ESP_LOGI(TAG, "re-associating");
        esp_wifi_connect();
    }
}

static void schedule_retry(void)
{
    if (!s_want_connect) {
        return;
    }
    esp_timer_stop(s_retry_timer); // harmless when not running
    esp_timer_start_once(s_retry_timer, NET_RETRY_DELAY_US);
}

static void store_scan_results(void)
{
    uint16_t n = NET_SCAN_MAX;
    wifi_ap_record_t recs[NET_SCAN_MAX];
    if (esp_wifi_scan_get_ap_records(&n, recs) != ESP_OK) {
        n = 0;
    }
    esp_wifi_clear_ap_list();

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_scan_count = 0;
    // esp_wifi returns records strongest first, so the first sighting of an
    // SSID is the best one and later duplicates (mesh, repeaters) are dropped.
    for (uint16_t i = 0; i < n && s_scan_count < NET_SCAN_MAX; i++) {
        const char *ssid = (const char *)recs[i].ssid;
        if (ssid[0] == '\0') {
            continue; // hidden network: nothing to show or tap
        }
        bool dup = false;
        for (size_t j = 0; j < s_scan_count && !dup; j++) {
            dup = strcmp(s_scan[j].ssid, ssid) == 0;
        }
        if (dup) {
            continue;
        }
        net_ap_t *ap = &s_scan[s_scan_count++];
        snprintf(ap->ssid, sizeof(ap->ssid), "%s", ssid);
        ap->rssi = recs[i].rssi;
        ap->secure = recs[i].authmode != WIFI_AUTH_OPEN;
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "scan done, %u networks", (unsigned)s_scan_count);
}

static void wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)data;
    if (base == WIFI_EVENT) {
        switch (id) {
        case WIFI_EVENT_STA_START:
            if (s_want_connect) {
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED:
            xSemaphoreTake(s_lock, portMAX_DELAY);
            s_ip[0] = '\0';
            xSemaphoreGive(s_lock);
            s_state = s_want_connect ? NET_CONNECTING : NET_DOWN;
            ESP_LOGW(TAG, "disconnected");
            schedule_retry();
            break;
        case WIFI_EVENT_SCAN_DONE:
            store_scan_results();
            s_scan_busy = false;
            break;
        default:
            break;
        }
        return;
    }
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *ev = (const ip_event_got_ip_t *)data;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(s_ip, sizeof(s_ip), IPSTR, IP2STR(&ev->ip_info.ip));
        xSemaphoreGive(s_lock);
        s_state = NET_UP;
        ESP_LOGI(TAG, "got IP " IPSTR, IP2STR(&ev->ip_info.ip));
    }
}

// ---------------------------------------------------------------- api

esp_err_t net_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_netif = esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    // Credentials live in our own NVS namespace (drivers/settings.h), so don't
    // let esp_wifi keep a second copy that could silently win at boot.
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                        wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                        wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    const esp_timer_create_args_t targs = {
        .callback = retry_cb,
        .name = "net_retry",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_retry_timer));
    return ESP_OK;
}

esp_err_t net_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        // Park: no SSID configured yet. Staff set one from the admin panel.
        s_want_connect = false;
        esp_timer_stop(s_retry_timer);
        esp_wifi_disconnect();
        s_state = NET_DOWN;
        ESP_LOGW(TAG, "no SSID configured, radio parked");
        return ESP_ERR_INVALID_ARG;
    }

    wifi_config_t cfg = { 0 };
    snprintf((char *)cfg.sta.ssid, sizeof(cfg.sta.ssid), "%s", ssid);
    snprintf((char *)cfg.sta.password, sizeof(cfg.sta.password), "%s", pass ? pass : "");
    // An open AP must not be rejected, and anything WPA2 or better must not be
    // downgraded, so the floor sits at WPA for a configured password.
    cfg.sta.threshold.authmode =
        (pass && pass[0]) ? WIFI_AUTH_WPA_PSK : WIFI_AUTH_OPEN;

    s_want_connect = true;
    esp_timer_stop(s_retry_timer);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_ip[0] = '\0';
    xSemaphoreGive(s_lock);

    esp_wifi_disconnect(); // no-op when idle; drops an old AP when re-pointing
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &cfg);
    if (err != ESP_OK) {
        return err;
    }
    s_state = NET_CONNECTING;
    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        // Not fatal: the disconnect above may still be in flight, and the
        // retry timer picks it up.
        ESP_LOGW(TAG, "connect returned %s, will retry", esp_err_to_name(err));
        schedule_retry();
    }
    return ESP_OK;
}

esp_err_t net_scan_start(void)
{
    if (s_scan_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_busy = true;
    esp_err_t err = esp_wifi_scan_start(NULL, false /* async */);
    if (err != ESP_OK) {
        s_scan_busy = false;
    }
    return err;
}

bool net_scan_busy(void)
{
    return s_scan_busy;
}

size_t net_scan_results(net_ap_t *out, size_t max)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    size_t n = s_scan_count < max ? s_scan_count : max;
    memcpy(out, s_scan, n * sizeof(net_ap_t));
    xSemaphoreGive(s_lock);
    return n;
}

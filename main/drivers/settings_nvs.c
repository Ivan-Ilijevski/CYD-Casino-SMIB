#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

#include "cms/cms_link.h"   // CMS_HOST_MAX
#include "net.h"
#include "settings.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE "smib"
#define KEY_SSID "wifi_ssid"
#define KEY_PASS "wifi_pass"
#define KEY_HOST "cms_host"
#define KEY_PORT "cms_port"

// The settings struct is what the admin panel edits and what net/cms_link
// consume, so a mismatch would silently truncate a long SSID or hostname.
_Static_assert(SETTINGS_SSID_MAX == NET_SSID_MAX, "SSID buffer sizes diverged");
_Static_assert(SETTINGS_PASS_MAX == NET_PASS_MAX, "password buffer sizes diverged");
_Static_assert(SETTINGS_HOST_MAX == CMS_HOST_MAX, "CMS host buffer sizes diverged");

esp_err_t settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // A new or repartitioned flash. Erasing loses saved settings, which
        // then fall back to the .env defaults — the same place a fresh board
        // starts, so nothing needs staff attention.
        ESP_LOGW(TAG, "NVS unusable (%s), erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

// Reads one string key, leaving `dst` untouched (i.e. at its default) when the
// key is absent or does not fit.
static void load_str(nvs_handle_t h, const char *key, char *dst, size_t size)
{
    char buf[SETTINGS_PASS_MAX > SETTINGS_HOST_MAX ? SETTINGS_PASS_MAX : SETTINGS_HOST_MAX];
    size_t len = sizeof(buf);
    if (size > sizeof(buf)) {
        return;
    }
    if (nvs_get_str(h, key, buf, &len) == ESP_OK) {
        snprintf(dst, size, "%s", buf);
    }
}

void settings_load(smib_settings_t *out)
{
    // Compiled-in factory defaults from .env (cmake/smib_env.cmake).
    memset(out, 0, sizeof(*out));
    snprintf(out->wifi_ssid, sizeof(out->wifi_ssid), "%s", SMIB_WIFI_SSID_DEFAULT);
    snprintf(out->wifi_pass, sizeof(out->wifi_pass), "%s", SMIB_WIFI_PASS_DEFAULT);
    snprintf(out->cms_host, sizeof(out->cms_host), "%s", SMIB_CMS_HOST_DEFAULT);
    out->cms_port = SMIB_CMS_PORT_DEFAULT;

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        // Nothing saved yet is the normal first-boot case, not a problem.
        ESP_LOGI(TAG, "no saved settings (%s), using .env defaults",
                 esp_err_to_name(err));
        return;
    }
    load_str(h, KEY_SSID, out->wifi_ssid, sizeof(out->wifi_ssid));
    load_str(h, KEY_PASS, out->wifi_pass, sizeof(out->wifi_pass));
    load_str(h, KEY_HOST, out->cms_host, sizeof(out->cms_host));
    uint16_t port = 0;
    if (nvs_get_u16(h, KEY_PORT, &port) == ESP_OK && port != 0) {
        out->cms_port = port;
    }
    nvs_close(h);
    ESP_LOGI(TAG, "loaded: ssid '%s', cms %s:%u",
             out->wifi_ssid, out->cms_host, (unsigned)out->cms_port);
}

esp_err_t settings_save(const smib_settings_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    // First error wins, but every field is attempted so a partial write is
    // still internally consistent with what the commit reports.
    esp_err_t first = ESP_OK;
    const struct { const char *key; const char *val; } strs[] = {
        { KEY_SSID, s->wifi_ssid },
        { KEY_PASS, s->wifi_pass },
        { KEY_HOST, s->cms_host },
    };
    for (size_t i = 0; i < sizeof(strs) / sizeof(strs[0]); i++) {
        esp_err_t e = nvs_set_str(h, strs[i].key, strs[i].val);
        if (e != ESP_OK && first == ESP_OK) {
            first = e;
        }
    }
    esp_err_t e = nvs_set_u16(h, KEY_PORT, s->cms_port);
    if (e != ESP_OK && first == ESP_OK) {
        first = e;
    }
    e = nvs_commit(h);
    if (e != ESP_OK && first == ESP_OK) {
        first = e;
    }
    nvs_close(h);
    if (first == ESP_OK) {
        ESP_LOGI(TAG, "saved: ssid '%s', cms %s:%u",
                 s->wifi_ssid, s->cms_host, (unsigned)s->cms_port);
    } else {
        ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(first));
    }
    return first;
}

esp_err_t settings_reset(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        return err;
    }
    err = nvs_erase_all(h);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);
    ESP_LOGW(TAG, "settings reset to .env defaults (%s)", esp_err_to_name(err));
    return err;
}

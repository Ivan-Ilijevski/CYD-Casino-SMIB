// drivers/settings.h on the desktop: NVS replaced by a small file.
//
// Persisting across runs is the point — saving from the admin panel and then
// hitting `R` (reset) should come back with what you saved, exactly as the
// board does. The file sits next to the binary so a build tree carries its own
// state and two sims don't fight over one.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"

#include "cms/cms_link.h"   // CMS_HOST_MAX
#include "drivers/net.h"
#include "drivers/settings.h"
#include "sim.h"

static const char *TAG = "settings";

#define SIM_SETTINGS_PATH "smib-sim-settings.txt"

_Static_assert(SETTINGS_SSID_MAX == NET_SSID_MAX, "SSID buffer sizes diverged");
_Static_assert(SETTINGS_PASS_MAX == NET_PASS_MAX, "password buffer sizes diverged");
_Static_assert(SETTINGS_HOST_MAX == CMS_HOST_MAX, "CMS host buffer sizes diverged");

esp_err_t settings_init(void)
{
    return ESP_OK;
}

// Copies a value into `dst` only when it is non-empty, so a blank line in the
// file leaves the .env default in place — same as a missing NVS key.
static void set_if_present(char *dst, size_t size, const char *val)
{
    if (val && val[0]) {
        snprintf(dst, size, "%s", val);
    }
}

void settings_load(smib_settings_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->wifi_ssid, sizeof(out->wifi_ssid), "%s", SMIB_WIFI_SSID_DEFAULT);
    snprintf(out->wifi_pass, sizeof(out->wifi_pass), "%s", SMIB_WIFI_PASS_DEFAULT);
    snprintf(out->cms_host, sizeof(out->cms_host), "%s", SMIB_CMS_HOST_DEFAULT);
    out->cms_port = SMIB_CMS_PORT_DEFAULT;

    FILE *f = fopen(SIM_SETTINGS_PATH, "r");
    if (!f) {
        ESP_LOGI(TAG, "no saved settings, using .env defaults");
        return;
    }
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\r\n")] = '\0';
        char *eq = strchr(line, '=');
        if (!eq) {
            continue;
        }
        *eq = '\0';
        const char *key = line;
        const char *val = eq + 1;
        if (strcmp(key, "wifi_ssid") == 0) {
            set_if_present(out->wifi_ssid, sizeof(out->wifi_ssid), val);
        } else if (strcmp(key, "wifi_pass") == 0) {
            set_if_present(out->wifi_pass, sizeof(out->wifi_pass), val);
        } else if (strcmp(key, "cms_host") == 0) {
            set_if_present(out->cms_host, sizeof(out->cms_host), val);
        } else if (strcmp(key, "cms_port") == 0) {
            unsigned port = 0;
            if (sscanf(val, "%u", &port) == 1 && port > 0 && port <= 65535) {
                out->cms_port = (uint16_t)port;
            }
        }
    }
    fclose(f);
    ESP_LOGI(TAG, "loaded: ssid '%s', cms %s:%u",
             out->wifi_ssid, out->cms_host, (unsigned)out->cms_port);
}

esp_err_t settings_save(const smib_settings_t *s)
{
    FILE *f = fopen(SIM_SETTINGS_PATH, "w");
    if (!f) {
        ESP_LOGE(TAG, "cannot write %s", SIM_SETTINGS_PATH);
        return ESP_FAIL;
    }
    fprintf(f, "wifi_ssid=%s\n", s->wifi_ssid);
    fprintf(f, "wifi_pass=%s\n", s->wifi_pass);
    fprintf(f, "cms_host=%s\n", s->cms_host);
    fprintf(f, "cms_port=%u\n", (unsigned)s->cms_port);
    fclose(f);
    ESP_LOGI(TAG, "saved: ssid '%s', cms %s:%u",
             s->wifi_ssid, s->cms_host, (unsigned)s->cms_port);
    sim_printf("settings saved to %s", SIM_SETTINGS_PATH);
    return ESP_OK;
}

esp_err_t settings_reset(void)
{
    remove(SIM_SETTINGS_PATH);
    ESP_LOGW(TAG, "settings reset to .env defaults");
    return ESP_OK;
}

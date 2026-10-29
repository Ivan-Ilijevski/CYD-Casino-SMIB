// drivers/net.h on the desktop: a fake Wi-Fi radio.
//
// The host is already on a network — cms_link's socket connects for real — so
// this exists to drive the admin panel: a scan list to pick from, a connect
// that takes a moment, and a way to fail on demand (`wifi down`) so the
// panel's "no connection" path can be seen without unplugging anything.

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"

#include "cms/cms_link.h"
#include "drivers/net.h"
#include "drivers/settings.h"
#include "sim.h"

static const char *TAG = "net";

// How long a fake association takes, and a fake scan. Long enough that the
// panel's CONNECTING state and "Скенирање..." are actually visible.
#define SIM_CONNECT_US (1500 * 1000)
#define SIM_SCAN_US    (1200 * 1000)

// What the fake scan finds. The first one is what .env points at by default,
// so the normal path is "scan, pick the top entry, save".
static const net_ap_t k_fake_aps[] = {
    { .ssid = "club-ap",      .rssi = -42, .secure = true },
    { .ssid = "club-guest",   .rssi = -55, .secure = false },
    { .ssid = "bar-backoffice", .rssi = -67, .secure = true },
    { .ssid = "EGM-service",  .rssi = -81, .secure = true },
};

static net_state_t s_state = NET_DOWN;
static char s_ssid[NET_SSID_MAX];
static int64_t s_connect_at_us;     // when CONNECTING should become UP; 0 = idle
static int64_t s_scan_done_us;      // when the fake scan finishes; 0 = not scanning
static bool s_forced_down;          // `wifi down` from the console

esp_err_t net_init(void)
{
    ESP_LOGI(TAG, "simulated Wi-Fi radio");
    return ESP_OK;
}

// The sim has no event loop for this, so state advances lazily whenever
// anything asks. Every caller is on the app thread, so no locking is needed.
static void advance(void)
{
    int64_t now = esp_timer_get_time();
    if (s_connect_at_us && now >= s_connect_at_us) {
        s_connect_at_us = 0;
        if (s_forced_down) {
            s_state = NET_DOWN;
            ESP_LOGW(TAG, "association failed (forced down)");
        } else {
            s_state = NET_UP;
            ESP_LOGI(TAG, "got IP 10.0.0.42");
        }
    }
    if (s_scan_done_us && now >= s_scan_done_us) {
        s_scan_done_us = 0;
        ESP_LOGI(TAG, "scan done, %u networks",
                 (unsigned)(sizeof(k_fake_aps) / sizeof(k_fake_aps[0])));
    }
}

net_state_t net_state(void)
{
    advance();
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
    if (net_state() != NET_UP) {
        return false;
    }
    if (out && out_size > 0) {
        // Not the host's real address: cms_link connects over loopback or the
        // host's own LAN, and a made-up one makes clear this is the fake radio.
        snprintf(out, out_size, "10.0.0.42");
    }
    return true;
}

int net_rssi(void)
{
    if (net_state() != NET_UP) {
        return 0;
    }
    for (size_t i = 0; i < sizeof(k_fake_aps) / sizeof(k_fake_aps[0]); i++) {
        if (strcmp(k_fake_aps[i].ssid, s_ssid) == 0) {
            return k_fake_aps[i].rssi;
        }
    }
    return -60; // a network the fake scan doesn't know about
}

esp_err_t net_connect(const char *ssid, const char *pass)
{
    (void)pass; // the fake radio accepts any password
    if (!ssid || ssid[0] == '\0') {
        s_state = NET_DOWN;
        s_connect_at_us = 0;
        s_ssid[0] = '\0';
        ESP_LOGW(TAG, "no SSID configured, radio parked");
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(s_ssid, sizeof(s_ssid), "%s", ssid);
    s_state = NET_CONNECTING;
    s_connect_at_us = esp_timer_get_time() + SIM_CONNECT_US;
    ESP_LOGI(TAG, "connecting to '%s'", ssid);
    return ESP_OK;
}

esp_err_t net_scan_start(void)
{
    if (s_scan_done_us) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_done_us = esp_timer_get_time() + SIM_SCAN_US;
    return ESP_OK;
}

bool net_scan_busy(void)
{
    advance();
    return s_scan_done_us != 0;
}

size_t net_scan_results(net_ap_t *out, size_t max)
{
    size_t n = sizeof(k_fake_aps) / sizeof(k_fake_aps[0]);
    if (n > max) {
        n = max;
    }
    memcpy(out, k_fake_aps, n * sizeof(net_ap_t));
    return n;
}

// ---------------------------------------------------------------- sim API

// The host's own network is real and cms_link's socket knows nothing about
// this fake radio, so taking Wi-Fi "down" has to park the CMS endpoint as well
// — otherwise the socket would stay up and the offline badge would never
// appear, which is the whole point of the command.
bool sim_net_command(const char *arg)
{
    if (strcmp(arg, "up") == 0) {
        s_forced_down = false;
        smib_settings_t cfg;
        settings_load(&cfg);
        net_connect(cfg.wifi_ssid, cfg.wifi_pass);
        cms_link_set_endpoint(cfg.cms_host, cfg.cms_port);
        sim_printf("wifi: up, CMS back to %s:%u", cfg.cms_host, (unsigned)cfg.cms_port);
        return true;
    }
    if (strcmp(arg, "down") == 0) {
        s_forced_down = true;
        s_state = NET_DOWN;
        s_connect_at_us = 0;
        cms_link_set_endpoint("", 0);
        sim_printf("wifi: down, CMS socket parked (offline badge should appear)");
        return true;
    }
    sim_printf("usage: wifi up|down");
    return false;
}

const char *sim_net_status(void)
{
    static char buf[64];
    snprintf(buf, sizeof(buf), "%s%s%s", net_state_name(net_state()),
             s_ssid[0] ? " " : "", s_ssid);
    return buf;
}

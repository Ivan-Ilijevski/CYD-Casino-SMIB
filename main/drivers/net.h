// Wi-Fi station: the network the CMS link rides on.
//
// Platform-backed like the other drivers/ modules — net_wifi.c on the board,
// sim_net.c in the desktop simulator — so the admin panel can be exercised
// without hardware.
//
// Deliberately pull-only: the admin panel polls state/ip/rssi from an LVGL
// timer while it is open rather than receiving events. Nothing outside the
// panel cares about Wi-Fi state (the offline badge follows the CMS link, which
// has its own liveness), so an event type and a queue hop would buy nothing.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define NET_SSID_MAX 33     // 32 chars + NUL
#define NET_PASS_MAX 65     // 64 chars + NUL
#define NET_SCAN_MAX 12     // as many APs as the panel's list can show

typedef enum {
    NET_DOWN,
    NET_CONNECTING,
    NET_UP,             // associated AND holding a DHCP lease
} net_state_t;

typedef struct {
    char ssid[NET_SSID_MAX];
    int8_t rssi;        // dBm
    bool secure;
} net_ap_t;

esp_err_t net_init(void);

// Associates with `ssid`. Returns as soon as the attempt starts; watch
// net_state(). An empty ssid parks the radio (NET_DOWN, no retries).
esp_err_t net_connect(const char *ssid, const char *pass);

net_state_t net_state(void);
const char *net_state_name(net_state_t st);

// Dotted IPv4 of the station, written only when NET_UP.
bool net_ip(char *out, size_t out_size);

// Signal strength of the current AP in dBm, 0 when not connected.
int net_rssi(void);

// Starts an async scan. net_scan_busy() falls false when results are ready.
// Scanning briefly interrupts an established link, which is why only the admin
// panel asks for one.
esp_err_t net_scan_start(void);
bool net_scan_busy(void);

// Copies up to `max` APs, strongest first, deduplicated by SSID. Returns the
// count written.
size_t net_scan_results(net_ap_t *out, size_t max);

// Persisted terminal configuration: the Wi-Fi to join and where the CMS is.
//
// Two layers. The compiled-in values from .env (cmake/smib_env.cmake) are the
// factory defaults, so a freshly flashed board joins the network with nobody
// typing on a 480x320 resistive panel. Once staff save from the admin panel the
// values live in NVS and win from then on.
//
// The admin passcode is NOT here: it stays a build-time constant
// (SMIB_ADMIN_PASSCODE) so a stolen terminal cannot be re-pointed at another
// CMS by someone who can reach its flash.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define SETTINGS_SSID_MAX 33    // must match NET_SSID_MAX
#define SETTINGS_PASS_MAX 65    // must match NET_PASS_MAX
#define SETTINGS_HOST_MAX 64    // must match CMS_HOST_MAX

typedef struct {
    char wifi_ssid[SETTINGS_SSID_MAX];
    char wifi_pass[SETTINGS_PASS_MAX];
    char cms_host[SETTINGS_HOST_MAX];   // dotted IP or hostname
    uint16_t cms_port;
} smib_settings_t;

// Brings the backing store up (NVS on the board). Call before settings_load().
esp_err_t settings_init(void);

// Always fills `out`: a saved value per field, else the .env default. Never
// fails — an unreadable store just yields the defaults.
void settings_load(smib_settings_t *out);

esp_err_t settings_save(const smib_settings_t *s);

// Drops every saved value, so the next load returns the .env defaults.
esp_err_t settings_reset(void);

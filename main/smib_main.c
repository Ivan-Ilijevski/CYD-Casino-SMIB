#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"

#include "lvgl.h"

#include "board.h"
#include "drivers/display.h"
#include "drivers/door.h"
#include "drivers/net.h"
#include "drivers/nfc.h"
#include "drivers/audio.h"
#include "drivers/settings.h"
#include "mux/mux.h"
#include "mux/log_bridge.h"
#include "cms/cms.h"
#include "cms/cms_link.h"
#include "sas/sas.h"
#include "app/app.h"
#include "ui/ui.h"

static const char *TAG = "smib";

void app_main(void)
{
    display_backlight_on();
    // Bring the mux up first so log_bridge_install() is live before the rest
    // of the bring-up: with CONFIG_ESP_CONSOLE_NONE, LOG frames on the mux are
    // the only way to see where boot stops. The mux now carries SAS and logs
    // only — CMS rides Wi-Fi (cms_link).
    ESP_ERROR_CHECK(mux_init(MUX_BAUD));
    log_bridge_install();
    ESP_LOGI(TAG, "SMIB fw %s booted", SMIB_FW_VERSION);

    // Settings before the radio and the socket: both are pointed at whatever
    // staff last saved, falling back to the .env factory defaults.
    ESP_ERROR_CHECK(settings_init());
    smib_settings_t cfg;
    settings_load(&cfg);

    display_init();
    audio_init();
    ESP_ERROR_CHECK(door_init());
    app_init();

    ESP_ERROR_CHECK(net_init());
    // An unconfigured board parks the radio and says so; staff fix it from the
    // admin panel, so this must not stop the boot.
    net_connect(cfg.wifi_ssid, cfg.wifi_pass);

    ESP_ERROR_CHECK(cms_link_init());
    cms_link_set_endpoint(cfg.cms_host, cfg.cms_port);
    ESP_ERROR_CHECK(cms_init());
    ESP_ERROR_CHECK(sas_init());
    ui_init();
    ui_show_idle();
    ui_set_offline(true); // until CMS + SAS links come up
    nfc_init();

    int64_t last_heartbeat_us = 0;
    while (1) {
        app_tick();
        int64_t now = esp_timer_get_time();
        if (now - last_heartbeat_us > 1000000) {
            last_heartbeat_us = now;
            // Free heap is in here because Wi-Fi is the biggest allocation on
            // this firmware and the E32R40T has no PSRAM to absorb it.
            ESP_LOGI(TAG, "heartbeat uptime=%llds heap=%uB net=%s",
                     now / 1000000, (unsigned)esp_get_free_heap_size(),
                     net_state_name(net_state()));
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

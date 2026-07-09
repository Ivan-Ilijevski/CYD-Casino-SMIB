#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl.h"

#include "board.h"
#include "drivers/display.h"
#include "drivers/nfc.h"
#include "drivers/audio.h"
#include "mux/mux.h"
#include "mux/log_bridge.h"
#include "cms/cms.h"
#include "sas/sas.h"
#include "app/app.h"
#include "ui/ui.h"

static const char *TAG = "smib";

void app_main(void)
{
    display_backlight_on();
    display_init();
    ESP_ERROR_CHECK(mux_init(MUX_BAUD));
    log_bridge_install();
    ESP_LOGI(TAG, "SMIB fw %s booted", SMIB_FW_VERSION);
    audio_init();
    app_init();
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
            ESP_LOGI(TAG, "heartbeat uptime=%llds", now / 1000000);
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

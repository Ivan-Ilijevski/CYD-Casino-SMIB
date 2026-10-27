#include <stdio.h>
#include <string.h>
#include "esp_err.h"
#include "esp_log.h"
#include "driver/spi_master.h"

#include "rc522.h"
#include "driver/rc522_spi.h"
#include "rc522_picc.h"

#include "board.h"
#include "app/app_events.h"
#include "nfc.h"

static const char *TAG = "nfc";

static rc522_driver_handle_t rc522_driver = NULL;
static rc522_handle_t rc522_scanner = NULL;

// Runs in the rc522 library's own event task, not the LVGL/main loop, so it
// must not call any LVGL APIs directly. It only posts an app event; the app
// task (same thread as lv_timer_handler) reacts to it.
static void on_picc_state_changed(void *arg, esp_event_base_t base, int32_t event_id, void *data)
{
    rc522_picc_state_changed_event_t *event = (rc522_picc_state_changed_event_t *)data;
    rc522_picc_t *picc = event->picc;

    if (picc->state == RC522_PICC_STATE_ACTIVE) {
        app_event_t e = { .type = APP_EVT_CARD_PRESENT };
        char uid_str[RC522_PICC_UID_STR_BUFFER_SIZE_MAX] = {0};
        if (picc->uid.length > 0 &&
            rc522_picc_uid_to_str(&picc->uid, uid_str, sizeof(uid_str)) == ESP_OK) {
            snprintf(e.u.uid, sizeof(e.u.uid), "%s", uid_str);
        }
        ESP_LOGI(TAG, "Card detected, UID: %s", e.u.uid);
        app_post_event(&e);
    } else if (picc->state == RC522_PICC_STATE_IDLE && event->old_state >= RC522_PICC_STATE_ACTIVE) {
        ESP_LOGI(TAG, "Card has been removed");
        app_event_t e = { .type = APP_EVT_CARD_REMOVED };
        app_post_event(&e);
    }
}

void nfc_init(void)
{
    // RC522 lives on its own VSPI bus (SPI3_HOST), separate from the
    // LCD/touch SPI2_HOST bus, to avoid adding contention to the 40MHz LCD
    // feed and the touch driver's 1MHz reads.
    rc522_spi_config_t driver_config = {
        .host_id = SPI3_HOST,
        .bus_config = &(spi_bus_config_t){
            .miso_io_num = PIN_NUM_RC522_MISO,
            .mosi_io_num = PIN_NUM_RC522_MOSI,
            .sclk_io_num = PIN_NUM_RC522_SCK,
        },
        .dev_config = {
            .spics_io_num = PIN_NUM_RC522_CS,
        },
        .rst_io_num = PIN_NUM_RC522_RST,
    };

    // A reader that is absent or not answering must not take the terminal
    // down. nfc_init() is the last step before the LVGL loop, so aborting here
    // used to reboot before a single frame was ever flushed — the screen stayed
    // on the panel's power-on noise. Log and come up without card scanning
    // instead; rc522_start() leaving the state at CREATED just idles its task.
    esp_err_t err = rc522_spi_create(&driver_config, &rc522_driver);
    if (err == ESP_OK) {
        err = rc522_driver_install(rc522_driver);
    }
    if (err == ESP_OK) {
        rc522_config_t scanner_config = {
            .driver = rc522_driver,
        };
        err = rc522_create(&scanner_config, &rc522_scanner);
    }
    if (err == ESP_OK) {
        err = rc522_register_events(rc522_scanner, RC522_EVENT_PICC_STATE_CHANGED,
                                    on_picc_state_changed, NULL);
    }
    if (err == ESP_OK) {
        err = rc522_start(rc522_scanner);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "RC522 unavailable (%s); card scanning disabled", esp_err_to_name(err));
        return;
    }
    ESP_LOGI(TAG, "RC522 initialized");
}

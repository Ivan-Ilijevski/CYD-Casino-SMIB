#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "board.h"
#include "door.h"

static const char *TAG = "door";

// A microswitch rattles for a few milliseconds. app_tick polls every 5 ms, so
// this is ten samples of agreement before a change is believed.
#define DOOR_DEBOUNCE_US (50 * 1000)

static bool s_stable;       // debounced: true = open
static bool s_last_raw;
static int64_t s_since_us;

#if PIN_NUM_DOOR >= 0
static bool read_raw(void)
{
    return gpio_get_level((gpio_num_t)PIN_NUM_DOOR) != 0; // HIGH = released = open
}
#endif

esp_err_t door_init(void)
{
#if PIN_NUM_DOOR >= 0
    gpio_config_t cfg = {
        .pin_bit_mask = 1ULL << PIN_NUM_DOOR,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&cfg);
    if (err != ESP_OK) {
        return err;
    }
    // Seed the debouncer from the real level so a board that boots with the
    // cabinet already open shows the banner on the first frame.
    s_stable = read_raw();
    s_last_raw = s_stable;
    s_since_us = esp_timer_get_time();
    ESP_LOGI(TAG, "door switch on GPIO%d, currently %s",
             PIN_NUM_DOOR, s_stable ? "OPEN" : "shut");
    return ESP_OK;
#else
    ESP_LOGW(TAG, "no door switch on this board; admin panel unreachable");
    return ESP_OK;
#endif
}

bool door_is_open(void)
{
#if PIN_NUM_DOOR >= 0
    bool raw = read_raw();
    int64_t now = esp_timer_get_time();
    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_since_us = now;
    } else if (raw != s_stable && now - s_since_us >= DOOR_DEBOUNCE_US) {
        s_stable = raw;
    }
#endif
    return s_stable;
}

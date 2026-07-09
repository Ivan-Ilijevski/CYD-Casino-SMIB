#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/dac_cosine.h"
#include "esp_log.h"

#include "board.h"
#include "audio.h"

static const char *TAG = "audio";

typedef struct {
    uint16_t freq_hz; // 0 = rest (silence)
    uint16_t ms;
} tone_t;

#define MAX_TONES 4

typedef struct {
    tone_t tones[MAX_TONES];
} cue_pattern_t;

static const cue_pattern_t k_patterns[] = {
    [CUE_SCAN]        = { .tones = { {1200, 80} } },
    [CUE_AUTH_OK]     = { .tones = { {880, 90}, {0, 25}, {1320, 140} } },
    [CUE_AUTH_FAIL]   = { .tones = { {440, 160}, {0, 25}, {330, 220} } },
    [CUE_ERROR]       = { .tones = { {440, 160}, {0, 25}, {330, 220} } },
    [CUE_TRANSFER_OK] = { .tones = { {880, 80}, {1100, 80}, {1470, 160} } },
    [CUE_ORDER_OK]    = { .tones = { {1050, 70}, {0, 20}, {1570, 110} } },
};

static QueueHandle_t s_queue;

static void amp_enable(bool on)
{
    // Active low: 0 = amp on. Muted when idle to avoid hiss.
    gpio_set_level(PIN_NUM_AUDIO_EN, on ? 0 : 1);
}

static void play_tone(uint16_t freq_hz, uint16_t ms)
{
    if (freq_hz == 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    dac_cosine_handle_t handle = NULL;
    dac_cosine_config_t cfg = {
        .chan_id = DAC_CHAN_1, // GPIO26
        .freq_hz = freq_hz,
        .clk_src = DAC_COSINE_CLK_SRC_DEFAULT,
        .atten = DAC_COSINE_ATTEN_DEFAULT,
        .phase = DAC_COSINE_PHASE_0,
        .offset = 0,
        // The cosine generator frequency is a shared RTC divider; force it
        // since we are its only user and change it per tone.
        .flags.force_set_freq = true,
    };
    esp_err_t err = dac_cosine_new_channel(&cfg, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "dac_cosine_new_channel: %s", esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    dac_cosine_start(handle);
    vTaskDelay(pdMS_TO_TICKS(ms));
    dac_cosine_stop(handle);
    dac_cosine_del_channel(handle);
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_cue_t cue;
    while (1) {
        if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if ((size_t)cue >= sizeof(k_patterns) / sizeof(k_patterns[0])) {
            continue;
        }
        amp_enable(true);
        const cue_pattern_t *p = &k_patterns[cue];
        for (int i = 0; i < MAX_TONES && (p->tones[i].freq_hz || p->tones[i].ms); i++) {
            play_tone(p->tones[i].freq_hz, p->tones[i].ms);
        }
        // Only mute if no follow-up cue is already queued.
        if (uxQueueMessagesWaiting(s_queue) == 0) {
            amp_enable(false);
        }
    }
}

void audio_init(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_NUM_AUDIO_EN,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    amp_enable(false);

    s_queue = xQueueCreate(8, sizeof(audio_cue_t));
    configASSERT(s_queue);
    BaseType_t ok = xTaskCreate(audio_task, "audio", 2560, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "audio cues ready (DAC ch1 / GPIO%d, amp en GPIO%d)",
             PIN_NUM_AUDIO_DAC, PIN_NUM_AUDIO_EN);
}

void audio_play(audio_cue_t cue)
{
    if (s_queue && xQueueSend(s_queue, &cue, 0) != pdTRUE) {
        ESP_LOGD(TAG, "audio queue full, cue %d dropped", cue);
    }
}

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"

#include "board.h"
#include "audio.h"

static const char *TAG = "audio";

// The amp (GPIO4 shutdown, active low) needs time to wake from shutdown —
// its pop-suppression soft-start swallows the head of whatever plays first.
// Warm it up before the first tone and keep it live between nearby cues so
// short beeps (e.g. the 0.5 s-spaced hold countdown) come out intact.
#define AMP_WARMUP_MS 150
#define AMP_LINGER_MS 1500

typedef struct {
    uint16_t freq_hz; // 0 = rest (silence)
    uint16_t ms;
} tone_t;

#define MAX_TONES 4

typedef struct {
    tone_t tones[MAX_TONES];
} cue_pattern_t;

static const cue_pattern_t k_patterns[] = {
    [CUE_SCAN]        = { .tones = { {1200, 120} } },
    [CUE_HOLD1]       = { .tones = { {1000, 150} } },
    [CUE_HOLD2]       = { .tones = { {1150, 150} } },
    [CUE_HOLD3]       = { .tones = { {1300, 150} } },
    [CUE_AUTH_OK]     = { .tones = { {1320, 900} } },
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

// LEDC square wave instead of the DAC sine: the DAC cosine generator tops
// out at 0 dB attenuation and was too quiet through the fixed-gain amp; a
// 50% square at the same peak carries more RMS energy and reads much louder.
#define TONE_MODE  LEDC_LOW_SPEED_MODE
#define TONE_TIMER LEDC_TIMER_0
#define TONE_CH    LEDC_CHANNEL_0
#define TONE_DUTY  (1 << 9) // 50% of the 10-bit resolution

static void play_tone(uint16_t freq_hz, uint16_t ms)
{
    if (freq_hz == 0) {
        vTaskDelay(pdMS_TO_TICKS(ms));
        return;
    }
    ledc_set_freq(TONE_MODE, TONE_TIMER, freq_hz);
    ledc_set_duty(TONE_MODE, TONE_CH, TONE_DUTY);
    ledc_update_duty(TONE_MODE, TONE_CH);
    vTaskDelay(pdMS_TO_TICKS(ms));
    ledc_set_duty(TONE_MODE, TONE_CH, 0);
    ledc_update_duty(TONE_MODE, TONE_CH);
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_cue_t cue;
    while (1) {
        if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        amp_enable(true);
        vTaskDelay(pdMS_TO_TICKS(AMP_WARMUP_MS));
        // Play cues until the queue stays empty for the linger window, then
        // mute. Cues inside the window start on an already-settled amp.
        do {
            if ((size_t)cue >= sizeof(k_patterns) / sizeof(k_patterns[0])) {
                continue;
            }
            const cue_pattern_t *p = &k_patterns[cue];
            for (int i = 0; i < MAX_TONES && (p->tones[i].freq_hz || p->tones[i].ms); i++) {
                play_tone(p->tones[i].freq_hz, p->tones[i].ms);
            }
        } while (xQueueReceive(s_queue, &cue, pdMS_TO_TICKS(AMP_LINGER_MS)) == pdTRUE);
        amp_enable(false);
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

    ledc_timer_config_t tcfg = {
        .speed_mode = TONE_MODE,
        .timer_num = TONE_TIMER,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .freq_hz = 1000, // placeholder; set per tone
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&tcfg));
    ledc_channel_config_t ccfg = {
        .gpio_num = PIN_NUM_AUDIO_DAC,
        .speed_mode = TONE_MODE,
        .channel = TONE_CH,
        .timer_sel = TONE_TIMER,
        .duty = 0, // silent (pin held low) until a tone plays
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ccfg));

    s_queue = xQueueCreate(8, sizeof(audio_cue_t));
    configASSERT(s_queue);
    BaseType_t ok = xTaskCreate(audio_task, "audio", 2560, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "audio cues ready (LEDC square / GPIO%d, amp en GPIO%d)",
             PIN_NUM_AUDIO_DAC, PIN_NUM_AUDIO_EN);
}

void audio_play(audio_cue_t cue)
{
    if (s_queue && xQueueSend(s_queue, &cue, 0) != pdTRUE) {
        ESP_LOGD(TAG, "audio queue full, cue %d dropped", cue);
    }
}

// ES3C35P audio: tones generated in software and played through the ES8311
// codec over I2S into the SC8002B amp. Same cues and amp handling as the
// E32R40T's LEDC driver (audio_ledc.c); only the tone output differs.
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

#include "board.h"
#include "audio.h"
#include "i2c_bus.h"

static const char *TAG = "audio";

// The amp (IO1 shutdown, active low) needs time to wake from shutdown —
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

// Square wave, as on the E32R40T: more RMS energy than a sine at the same
// peak, so cues read louder. Codec volume scales the final level.
#define SAMPLE_RATE 16000
#define TONE_AMPL   12000
#define CODEC_VOL   85      // 0..100
#define CHUNK       256     // samples per codec write

static QueueHandle_t s_queue;
static esp_codec_dev_handle_t s_codec;

static void amp_enable(bool on)
{
    // Active low: 0 = amp on. Muted when idle to avoid hiss.
    gpio_set_level(PIN_NUM_AUDIO_EN, on ? 0 : 1);
}

// Blocks for the tone's duration: the codec write paces on the I2S DMA.
static void play_tone(uint16_t freq_hz, uint16_t ms)
{
    int16_t buf[CHUNK];
    uint32_t total = (uint32_t)SAMPLE_RATE * ms / 1000;
    for (uint32_t n = 0; n < total;) {
        uint32_t len = total - n < CHUNK ? total - n : CHUNK;
        for (uint32_t i = 0; i < len; i++, n++) {
            // Half-period index parity: n * 2f / fs counts half periods.
            buf[i] = freq_hz == 0 ? 0
                   : ((n * 2u * freq_hz / SAMPLE_RATE) & 1) ? -TONE_AMPL : TONE_AMPL;
        }
        esp_codec_dev_write(s_codec, buf, len * sizeof(int16_t));
    }
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

static i2s_chan_handle_t init_i2s(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true; // underrun plays silence, not the last buffer
    i2s_chan_handle_t tx;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx, NULL));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = PIN_NUM_I2S_MCLK,
            .bclk = PIN_NUM_I2S_BCLK,
            .ws = PIN_NUM_I2S_LRCK,
            .dout = PIN_NUM_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx, &std_cfg));
    return tx;
}

static void init_codec(i2s_chan_handle_t tx)
{
    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_NUM_0,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_get(),
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = audio_codec_new_gpio(),
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1, // amp enable is driven here, with warmup/linger
        .use_mclk = true,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    configASSERT(data_if && ctrl_if && codec_if);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    configASSERT(s_codec);

    esp_codec_dev_sample_info_t fs = {
        .bits_per_sample = 16,
        .channel = 1,
        .sample_rate = SAMPLE_RATE,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL);
    esp_codec_dev_set_out_vol(s_codec, CODEC_VOL);
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

    init_codec(init_i2s());

    s_queue = xQueueCreate(8, sizeof(audio_cue_t));
    configASSERT(s_queue);
    // Larger stack than the LEDC driver: play_tone keeps a sample buffer.
    BaseType_t ok = xTaskCreate(audio_task, "audio", 3584, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "audio cues ready (ES8311 over I2S, amp en GPIO%d)", PIN_NUM_AUDIO_EN);
}

void audio_play(audio_cue_t cue)
{
    if (s_queue && xQueueSend(s_queue, &cue, 0) != pdTRUE) {
        ESP_LOGD(TAG, "audio queue full, cue %d dropped", cue);
    }
}

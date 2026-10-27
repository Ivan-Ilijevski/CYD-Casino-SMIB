// Speaker stand-in: the firmware's cue patterns played as square waves on the
// Mac's audio output, from a task with the board's amp warm-up/linger timing.
// Replaces main/drivers/audio.c.

#include <SDL.h>
#include <stdatomic.h>
#include <stdlib.h>

#include "drivers/audio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sim.h"

static const char *TAG = "audio";

#define AMP_WARMUP_MS 150
#define AMP_LINGER_MS 1500
#define SAMPLE_RATE   44100
#define AMPLITUDE     2500 // square waves are loud; keep it gentle
#define RAMP_SAMPLES  64   // fade in/out to avoid clicks

typedef struct {
    uint16_t freq_hz; // 0 = rest (silence)
    uint16_t ms;
} tone_t;

#define MAX_TONES 4

// Mirror of k_patterns in main/drivers/audio.c: keep the two in sync.
static const struct {
    const char *name;
    tone_t tones[MAX_TONES];
} k_patterns[] = {
    [CUE_SCAN]        = { "SCAN",        { {1200, 120} } },
    [CUE_HOLD1]       = { "HOLD1",       { {1000, 150} } },
    [CUE_HOLD2]       = { "HOLD2",       { {1150, 150} } },
    [CUE_HOLD3]       = { "HOLD3",       { {1300, 150} } },
    [CUE_AUTH_OK]     = { "AUTH_OK",     { {1320, 900} } },
    [CUE_AUTH_FAIL]   = { "AUTH_FAIL",   { {440, 160}, {0, 25}, {330, 220} } },
    [CUE_ERROR]       = { "ERROR",       { {440, 160}, {0, 25}, {330, 220} } },
    [CUE_TRANSFER_OK] = { "TRANSFER_OK", { {880, 80}, {1100, 80}, {1470, 160} } },
    [CUE_ORDER_OK]    = { "ORDER_OK",    { {1050, 70}, {0, 20}, {1570, 110} } },
};
#define CUE_COUNT (sizeof(k_patterns) / sizeof(k_patterns[0]))

static QueueHandle_t s_queue;
static SDL_AudioDeviceID s_dev;
static atomic_bool s_mute;

static void play_tone(uint16_t freq_hz, uint16_t ms)
{
    if (freq_hz && s_dev && !atomic_load(&s_mute)) {
        int n = SAMPLE_RATE * ms / 1000;
        int16_t *pcm = malloc((size_t)n * sizeof(int16_t));
        if (pcm) {
            for (int i = 0; i < n; i++) {
                int ramp = i < RAMP_SAMPLES ? i : n - i < RAMP_SAMPLES ? n - i : RAMP_SAMPLES;
                int level = AMPLITUDE * ramp / RAMP_SAMPLES;
                pcm[i] = (int16_t)(((int64_t)i * 2 * freq_hz / SAMPLE_RATE) % 2 ? -level : level);
            }
            SDL_QueueAudio(s_dev, pcm, (Uint32)((size_t)n * sizeof(int16_t)));
            free(pcm);
        }
    }
    vTaskDelay(pdMS_TO_TICKS(ms)); // same pacing as the LEDC version
}

static void audio_task(void *arg)
{
    (void)arg;
    audio_cue_t cue;
    while (1) {
        if (xQueueReceive(s_queue, &cue, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(AMP_WARMUP_MS));
        do {
            if ((size_t)cue >= CUE_COUNT) {
                continue;
            }
            sim_printf("\xe2\x99\xaa %s%s", k_patterns[cue].name,
                       atomic_load(&s_mute) || !s_dev ? " (muted)" : "");
            const tone_t *t = k_patterns[cue].tones;
            for (int i = 0; i < MAX_TONES && (t[i].freq_hz || t[i].ms); i++) {
                play_tone(t[i].freq_hz, t[i].ms);
            }
        } while (xQueueReceive(s_queue, &cue, pdMS_TO_TICKS(AMP_LINGER_MS)) == pdTRUE);
    }
}

void sim_audio_toggle_mute(void)
{
    bool mute = !atomic_load(&s_mute);
    atomic_store(&s_mute, mute);
    sim_printf("sound %s", mute ? "off" : "on");
}

void audio_init(void)
{
    atomic_store(&s_mute, g_sim.mute);
    SDL_AudioSpec want = { .freq = SAMPLE_RATE, .format = AUDIO_S16SYS, .channels = 1,
                           .samples = 512 };
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0 ||
        (s_dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0)) == 0) {
        sim_printf("no audio output (%s); cues are only printed", SDL_GetError());
    } else {
        SDL_PauseAudioDevice(s_dev, 0);
    }

    s_queue = xQueueCreate(8, sizeof(audio_cue_t));
    configASSERT(s_queue);
    BaseType_t ok = xTaskCreate(audio_task, "audio", 2560, NULL, 5, NULL);
    configASSERT(ok == pdPASS);
    ESP_LOGI(TAG, "audio cues ready (sim: Mac audio output)");
}

void audio_play(audio_cue_t cue)
{
    if (s_queue && xQueueSend(s_queue, &cue, 0) != pdTRUE) {
        ESP_LOGD(TAG, "audio queue full, cue %d dropped", cue);
    }
}

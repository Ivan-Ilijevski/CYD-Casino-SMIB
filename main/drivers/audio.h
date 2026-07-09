// Audio cues on the onboard speaker (DAC cosine tones through the amp).
#pragma once

typedef enum {
    CUE_SCAN,         // card detected, auth started
    CUE_AUTH_OK,      // session opened
    CUE_AUTH_FAIL,    // auth rejected / timed out
    CUE_ERROR,        // generic failure (transfer/order failed)
    CUE_TRANSFER_OK,  // funds landed on the machine
    CUE_ORDER_OK,     // drink order accepted
} audio_cue_t;

void audio_init(void);

// Non-blocking: queues the cue for the audio task. Safe from any task.
void audio_play(audio_cue_t cue);

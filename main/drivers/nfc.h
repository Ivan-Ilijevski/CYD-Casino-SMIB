// RC522 NFC reader (SPI3/VSPI). Card presence drives the auth flow.
#pragma once

#include <stdbool.h>
#include <stddef.h>

// Starts the RC522 scanner; card presence changes are delivered as
// APP_EVT_CARD_PRESENT / APP_EVT_CARD_REMOVED via app_post_event().
void nfc_init(void);

// Initial prompt shown on the idle screen.
#define CARD_PROMPT_TEXT "Доближете ја картичката"

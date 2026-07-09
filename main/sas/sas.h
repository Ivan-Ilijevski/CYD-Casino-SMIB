// SAS host engine: polls the gaming machine over mux channel 0x01, tracks
// link/credits/meters, and executes AFT fund transfers.
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

typedef struct {
    bool link_up;
    uint32_t asset_number;
    uint8_t aft_reg_status;
    uint64_t credits_cents;                // from 0x1A
    char game_id[3];
    char serial[41];
    char sas_version[4];
    uint64_t m_coin_in, m_coin_out, m_drop, m_jackpot, m_games; // from 0x19
} sas_status_t;

esp_err_t sas_init(void);

// Async: queues an AFT transfer of amount_cents to the machine; the outcome
// arrives as an APP_EVT_SAS / SAS_EVT_AFT_RESULT event carrying txn_id.
// Returns ESP_ERR_INVALID_STATE if a transfer is already in flight or the
// SAS link is down.
esp_err_t sas_aft_transfer(uint64_t amount_cents, const char *txn_id);

// Mutex-protected snapshot.
void sas_get_status(sas_status_t *out);

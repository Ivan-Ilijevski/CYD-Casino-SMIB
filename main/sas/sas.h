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
    uint8_t denom_code;                    // accounting denomination, from 0x1F
    uint64_t credits_cents;                // from 0x1A, converted to cents via denom
    char game_id[3];
    char serial[41];
    char sas_version[4];
    // from 0x19, converted to cents via the accounting denomination
    uint64_t m_coin_in, m_coin_out, m_drop, m_jackpot, m_games;
} sas_status_t;

esp_err_t sas_init(void);

// Async: queues an AFT transfer of amount_cents to the machine; the outcome
// arrives as an APP_EVT_SAS / SAS_EVT_AFT_RESULT event carrying txn_id.
// Returns ESP_ERR_INVALID_STATE if a transfer is already in flight or the
// SAS link is down.
esp_err_t sas_aft_transfer(uint64_t amount_cents, const char *txn_id);

// Async: cashes out the machine's current credits (AFT from the EGM). The
// amount is read fresh (0x1A) when the job runs; the SAS_EVT_AFT_RESULT
// event reports it in credits_cents (ok with 0 = nothing to move).
esp_err_t sas_aft_cashout(const char *txn_id);

// Mutex-protected snapshot.
void sas_get_status(sas_status_t *out);

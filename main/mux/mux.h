// Framed multiplexer over UART0 (the USB serial link to the slot machine).
//
// Frame: [0x7E][chan][len_lo][len_hi][payload...][crc_lo][crc_hi]
//   - CRC-16/KERMIT over chan+len+payload (sync byte excluded), LSB first.
//   - No byte-stuffing: length-driven parsing, resync by scanning for 0x7E
//     and rejecting candidates via chan/len sanity + CRC.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define MUX_CH_SAS      0x01  // raw SAS poll/response bytes
#define MUX_CH_CMS      0x02  // UTF-8 JSON
#define MUX_CH_LOG      0x7F  // firmware debug text
#define MUX_MAX_PAYLOAD 2048

// Runs on the mux_rx task: must not call LVGL; hand off via app events or
// module-internal queues only.
typedef void (*mux_rx_cb_t)(const uint8_t *payload, uint16_t len, void *ctx);

esp_err_t mux_init(int baud_rate);
esp_err_t mux_register_rx(uint8_t chan, mux_rx_cb_t cb, void *ctx);

// Thread-safe (mutex, 50 ms take timeout). Not ISR-safe.
esp_err_t mux_send(uint8_t chan, const uint8_t *payload, uint16_t len);

// Drops (ESP_ERR_NO_MEM) instead of blocking when the TX buffer or mutex is
// unavailable. Used only by the log bridge so a log storm can't stall SAS/CMS.
esp_err_t mux_send_nonblock(uint8_t chan, const uint8_t *payload, uint16_t len);

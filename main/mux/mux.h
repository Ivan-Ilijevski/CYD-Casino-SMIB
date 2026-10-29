// Framed multiplexer over UART0 (the USB serial link to the slot machine).
//
// Carries SAS and the firmware's logs. The CMS channel does NOT come through
// here any more: it rides a TCP socket over Wi-Fi (cms/cms_link.c), using the
// same frame format. MUX_CH_CMS is kept so the host-side splitter and this
// firmware still agree on the channel numbering.
//
// The frame format and its parser live in mux_frame.h, shared with the TCP leg.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "mux_frame.h"

#define MUX_CH_SAS      0x01  // raw SAS poll/response bytes
#define MUX_CH_CMS      0x02  // UTF-8 JSON (on the TCP leg, not this one)
#define MUX_CH_LOG      0x7F  // firmware debug text

esp_err_t mux_init(int baud_rate);
esp_err_t mux_register_rx(uint8_t chan, mux_rx_cb_t cb, void *ctx);

// Thread-safe (mutex, 50 ms take timeout). Not ISR-safe.
esp_err_t mux_send(uint8_t chan, const uint8_t *payload, uint16_t len);

// Drops (ESP_ERR_NO_MEM) instead of blocking when the TX buffer or mutex is
// unavailable. Used only by the log bridge so a log storm can't stall SAS.
esp_err_t mux_send_nonblock(uint8_t chan, const uint8_t *payload, uint16_t len);

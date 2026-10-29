// CMS transport: a TCP socket to the CMS host, carrying the same mux frames
// the serial link used to carry on channel 0x02.
//
// This replaced the UART mux leg for CMS. The frame format is unchanged
// (mux/mux_frame.h), so tools/host_sim/cms_tcp_server.py is the peer and the
// host-side parser is shared. Keeping the frame means cms.c's JSON layer, its
// pending table and every funds-transfer rule are untouched by the move.
//
// One task owns the socket: it connects, reconnects with backoff, and feeds
// received bytes to the parser. Sends come from the app task.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#include "mux/mux_frame.h"

#define CMS_HOST_MAX 64

// Starts the link task. The endpoint comes from cms_link_set_endpoint(); until
// one is set (or while it is empty) the task idles and sends fail.
esp_err_t cms_link_init(void);

// Registers the receive callback for CMS payloads. Same signature as the mux's,
// so cms.c's existing rx callback is reused verbatim. Runs on the link task.
esp_err_t cms_link_register_rx(mux_rx_cb_t cb, void *ctx);

// Points the link at a host (dotted IP or hostname) and port, and reconnects.
// An empty or NULL host parks the link. Safe from the app task; the link task
// picks the change up within its receive timeout (~200 ms).
void cms_link_set_endpoint(const char *host, uint16_t port);
void cms_link_get_endpoint(char *host_out, size_t host_out_size, uint16_t *port_out);

// True while the socket is up. cms.c's ping/pong still decides whether the CMS
// itself is answering; this only reports the transport.
bool cms_link_connected(void);

// Frames and sends one CMS payload. ESP_ERR_INVALID_STATE when the socket is
// down, which is what cms.c reports to its caller as a failed request.
esp_err_t cms_link_send(const uint8_t *payload, uint16_t len);

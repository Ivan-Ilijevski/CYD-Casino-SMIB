// Mux frame format and its incremental parser, shared by every transport that
// carries mux frames: the UART0 link to the slot machine (mux.c, SAS + logs)
// and the TCP link to the CMS (cms/cms_link.c).
//
// Frame: [0x7E][chan][len_lo][len_hi][payload...][crc_lo][crc_hi]
//   - CRC-16/KERMIT over chan+len+payload (sync byte excluded), LSB first.
//   - No byte-stuffing: length-driven parsing, resync by scanning for 0x7E
//     and rejecting candidates via chan/len sanity + CRC.
//
// This lives apart from mux.c so the framing has exactly one implementation.
// Two copies of a CRC-and-resync loop would drift, and the host-side parser
// (tools/host_sim/mux.py) is written against this one.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MUX_SYNC        0x7E
#define MUX_HDR_LEN     4                       // sync + chan + len16
#define MUX_OVERHEAD    (MUX_HDR_LEN + 2)       // + crc16
#define MUX_MAX_PAYLOAD 2048

// Receive callback. Runs on whichever task feeds the parser — never the app
// task — so it must not call LVGL; hand off via app events or module-internal
// queues only.
typedef void (*mux_rx_cb_t)(const uint8_t *payload, uint16_t len, void *ctx);

// Writes one complete frame into `out`, which must have room for
// MUX_OVERHEAD + len bytes. Returns the number of bytes written.
size_t mux_frame_write(uint8_t *out, uint8_t chan, const uint8_t *payload, uint16_t len);

// Tells the parser whether a channel byte is one this transport expects. A
// candidate with an unknown channel is rejected like a bad CRC, which is what
// lets the parser resync past garbage.
typedef bool (*mux_chan_valid_fn)(uint8_t chan);

// Called for each complete, CRC-checked frame.
typedef void (*mux_frame_cb_t)(uint8_t chan, const uint8_t *payload, uint16_t len, void *ctx);

typedef struct {
    uint8_t *acc;               // caller-owned accumulator
    size_t cap;
    size_t len;
    int64_t last_byte_us;
    int64_t stall_timeout_us;   // 0 = never abandon a partial frame (see below)
    mux_chan_valid_fn chan_valid;
    mux_frame_cb_t cb;
    void *ctx;
} mux_parser_t;

// `storage`/`cap` is the accumulator; it must hold at least one maximum-size
// frame. Size it with MUX_ACC_SIZE_FOR().
//
// `stall_timeout_us` is for lossy transports: on a UART a frame can be cut off
// mid-flight and never finish, so a partial frame whose stream has gone quiet
// for this long is abandoned and rescanned from the next byte. Pass 0 on a
// reliable stream like TCP, where a partial frame always completes and the only
// real failure is the socket closing — which resets the parser instead.
void mux_parser_init(mux_parser_t *p, uint8_t *storage, size_t cap,
                     int64_t stall_timeout_us, mux_chan_valid_fn chan_valid,
                     mux_frame_cb_t cb, void *ctx);

// Headroom so a maximum-size frame still fits after some leading garbage.
#define MUX_ACC_SIZE_FOR(max_payload) ((max_payload) + MUX_OVERHEAD + 256)

// Forgets any partial frame. Use when the transport reconnects: bytes from
// before the break can only be garbage.
void mux_parser_reset(mux_parser_t *p);

// Appends `n` bytes and dispatches every complete frame they produce. When the
// accumulator would overflow without yielding a frame, the oldest bytes are
// dropped — they cannot be part of a parsable frame any more.
void mux_parser_feed(mux_parser_t *p, const uint8_t *data, size_t n);

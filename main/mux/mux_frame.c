#include <string.h>
#include "esp_timer.h"

#include "crc16.h"
#include "mux_frame.h"

size_t mux_frame_write(uint8_t *out, uint8_t chan, const uint8_t *payload, uint16_t len)
{
    out[0] = MUX_SYNC;
    out[1] = chan;
    out[2] = (uint8_t)(len & 0xFF);
    out[3] = (uint8_t)(len >> 8);
    if (len > 0) {
        memcpy(out + MUX_HDR_LEN, payload, len);
    }
    uint16_t crc = crc16_kermit(out + 1, MUX_HDR_LEN - 1 + len);
    out[MUX_HDR_LEN + len] = (uint8_t)(crc & 0xFF);
    out[MUX_HDR_LEN + len + 1] = (uint8_t)(crc >> 8);
    return MUX_OVERHEAD + len;
}

void mux_parser_init(mux_parser_t *p, uint8_t *storage, size_t cap,
                     int64_t stall_timeout_us, mux_chan_valid_fn chan_valid,
                     mux_frame_cb_t cb, void *ctx)
{
    p->acc = storage;
    p->cap = cap;
    p->len = 0;
    p->last_byte_us = esp_timer_get_time();
    p->stall_timeout_us = stall_timeout_us;
    p->chan_valid = chan_valid;
    p->cb = cb;
    p->ctx = ctx;
}

void mux_parser_reset(mux_parser_t *p)
{
    p->len = 0;
    p->last_byte_us = esp_timer_get_time();
}

static void acc_drop(mux_parser_t *p, size_t n)
{
    memmove(p->acc, p->acc + n, p->len - n);
    p->len -= n;
}

// Consumes as much of the accumulator as possible. On any candidate-frame
// rejection, resumes scanning at the byte AFTER the candidate 0x7E so that a
// 0x7E embedded in garbage can't permanently desync the stream.
static void parse_acc(mux_parser_t *p)
{
    while (1) {
        // HUNT: align the buffer on a sync byte.
        size_t i = 0;
        while (i < p->len && p->acc[i] != MUX_SYNC) {
            i++;
        }
        if (i > 0) {
            acc_drop(p, i);
        }
        if (p->len < MUX_HDR_LEN) {
            return; // need more bytes for a header
        }

        uint8_t chan = p->acc[1];
        uint16_t len = (uint16_t)p->acc[2] | ((uint16_t)p->acc[3] << 8);
        if (!p->chan_valid(chan) || len > MUX_MAX_PAYLOAD ||
            (size_t)MUX_OVERHEAD + len > p->cap) {
            acc_drop(p, 1); // bogus candidate: rescan from the next byte
            continue;
        }

        size_t total = MUX_OVERHEAD + len;
        if (p->len < total) {
            // Mid-frame: on a lossy transport, give up on this candidate if
            // the stream stalled. On a reliable one the frame always finishes.
            if (p->stall_timeout_us > 0 &&
                esp_timer_get_time() - p->last_byte_us > p->stall_timeout_us) {
                acc_drop(p, 1);
                continue;
            }
            return; // wait for the rest
        }

        uint16_t crc = crc16_kermit(p->acc + 1, MUX_HDR_LEN - 1 + len);
        uint16_t rx_crc = (uint16_t)p->acc[MUX_HDR_LEN + len] |
                          ((uint16_t)p->acc[MUX_HDR_LEN + len + 1] << 8);
        if (crc != rx_crc) {
            acc_drop(p, 1);
            continue;
        }

        p->cb(chan, p->acc + MUX_HDR_LEN, len, p->ctx);
        acc_drop(p, total);
    }
}

void mux_parser_feed(mux_parser_t *p, const uint8_t *data, size_t n)
{
    if (n > 0) {
        p->last_byte_us = esp_timer_get_time();
        if (n >= p->cap) {
            // One read bigger than the whole accumulator: only its tail can
            // still start a parsable frame.
            data += n - p->cap;
            n = p->cap;
            p->len = 0;
        } else if (p->len + n > p->cap) {
            // Accumulator full without a parsable frame: keep the newest
            // bytes, the old ones can only be garbage.
            acc_drop(p, p->len + n - p->cap);
        }
        memcpy(p->acc + p->len, data, n);
        p->len += n;
    }
    if (p->len > 0) {
        parse_acc(p);
    }
}

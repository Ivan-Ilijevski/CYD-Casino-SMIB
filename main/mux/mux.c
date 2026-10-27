#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "esp_timer.h"

#include "board.h"
#include "crc16.h"
#include "mux.h"

#define MUX_UART            ((uart_port_t)MUX_UART_NUM)
#define MUX_HDR_LEN         4                        // sync + chan + len16
#define MUX_OVERHEAD        (MUX_HDR_LEN + 2)        // + crc16
#define MUX_SYNC            0x7E
#define MUX_ACC_SIZE        (MUX_MAX_PAYLOAD + MUX_OVERHEAD + 256)
#define MUX_RX_CHUNK        256
#define MUX_INTERBYTE_TIMEOUT_US (500 * 1000)
#define MUX_TX_MUTEX_TIMEOUT pdMS_TO_TICKS(50)

typedef struct {
    uint8_t chan;
    mux_rx_cb_t cb;
    void *ctx;
} mux_rx_slot_t;

static mux_rx_slot_t s_rx_slots[4];
static SemaphoreHandle_t s_tx_mutex;

static uint8_t s_acc[MUX_ACC_SIZE];
static size_t s_acc_len;
static int64_t s_last_byte_us;

static bool chan_valid(uint8_t chan)
{
    return chan == MUX_CH_SAS || chan == MUX_CH_CMS || chan == MUX_CH_LOG;
}

static void dispatch(uint8_t chan, const uint8_t *payload, uint16_t len)
{
    for (size_t i = 0; i < sizeof(s_rx_slots) / sizeof(s_rx_slots[0]); i++) {
        if (s_rx_slots[i].cb && s_rx_slots[i].chan == chan) {
            s_rx_slots[i].cb(payload, len, s_rx_slots[i].ctx);
            return;
        }
    }
}

static void acc_drop(size_t n)
{
    memmove(s_acc, s_acc + n, s_acc_len - n);
    s_acc_len -= n;
}

// Consumes as much of the accumulator as possible. On any candidate-frame
// rejection, resumes scanning at the byte AFTER the candidate 0x7E so that a
// 0x7E embedded in garbage can't permanently desync the stream.
static void parse_acc(void)
{
    while (1) {
        // HUNT: align the buffer on a sync byte.
        size_t i = 0;
        while (i < s_acc_len && s_acc[i] != MUX_SYNC) {
            i++;
        }
        if (i > 0) {
            acc_drop(i);
        }
        if (s_acc_len < MUX_HDR_LEN) {
            return; // need more bytes for a header
        }

        uint8_t chan = s_acc[1];
        uint16_t len = (uint16_t)s_acc[2] | ((uint16_t)s_acc[3] << 8);
        if (!chan_valid(chan) || len > MUX_MAX_PAYLOAD) {
            acc_drop(1); // bogus candidate: rescan from the next byte
            continue;
        }

        size_t total = MUX_OVERHEAD + len;
        if (s_acc_len < total) {
            // Mid-frame: give up on this candidate if the stream stalled.
            if (esp_timer_get_time() - s_last_byte_us > MUX_INTERBYTE_TIMEOUT_US) {
                acc_drop(1);
                continue;
            }
            return; // wait for the rest
        }

        uint16_t crc = crc16_kermit(s_acc + 1, MUX_HDR_LEN - 1 + len);
        uint16_t rx_crc = (uint16_t)s_acc[MUX_HDR_LEN + len] |
                          ((uint16_t)s_acc[MUX_HDR_LEN + len + 1] << 8);
        if (crc != rx_crc) {
            acc_drop(1);
            continue;
        }

        dispatch(chan, s_acc + MUX_HDR_LEN, len);
        acc_drop(total);
    }
}

static void mux_rx_task(void *arg)
{
    uint8_t chunk[MUX_RX_CHUNK];
    while (1) {
        int n = uart_read_bytes(MUX_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(50));
        if (n > 0) {
            s_last_byte_us = esp_timer_get_time();
            if (s_acc_len + (size_t)n > MUX_ACC_SIZE) {
                // Accumulator full without a parsable frame: keep the newest
                // bytes, the old ones can only be garbage.
                size_t need = s_acc_len + (size_t)n - MUX_ACC_SIZE;
                acc_drop(need);
            }
            memcpy(s_acc + s_acc_len, chunk, n);
            s_acc_len += n;
        }
        if (s_acc_len > 0) {
            parse_acc();
        }
    }
}

esp_err_t mux_init(int baud_rate)
{
    uart_config_t cfg = {
        .baud_rate = baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(MUX_UART, 4096, 4096, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(MUX_UART, &cfg));
    // With CONFIG_ESP_CONSOLE_NONE nothing routes the UART0 pins to the
    // peripheral for the app (a UART console would have), so do it explicitly.
    // The pins differ per target, so they come from the board header.
    ESP_ERROR_CHECK(uart_set_pin(MUX_UART, MUX_UART_TX, MUX_UART_RX,
                                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    s_tx_mutex = xSemaphoreCreateMutex();
    if (!s_tx_mutex) {
        return ESP_ERR_NO_MEM;
    }
    s_last_byte_us = esp_timer_get_time();

    BaseType_t ok = xTaskCreate(mux_rx_task, "mux_rx", 6144, NULL, 10, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t mux_register_rx(uint8_t chan, mux_rx_cb_t cb, void *ctx)
{
    for (size_t i = 0; i < sizeof(s_rx_slots) / sizeof(s_rx_slots[0]); i++) {
        if (!s_rx_slots[i].cb) {
            s_rx_slots[i] = (mux_rx_slot_t){ .chan = chan, .cb = cb, .ctx = ctx };
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

static void write_frame(uint8_t chan, const uint8_t *payload, uint16_t len)
{
    uint8_t hdr[MUX_HDR_LEN] = { MUX_SYNC, chan, (uint8_t)(len & 0xFF), (uint8_t)(len >> 8) };
    uint16_t crc = crc16_kermit_update(0x0000, hdr + 1, MUX_HDR_LEN - 1);
    crc = crc16_kermit_update(crc, payload, len);
    uint8_t crc_bytes[2] = { (uint8_t)(crc & 0xFF), (uint8_t)(crc >> 8) };

    uart_write_bytes(MUX_UART, hdr, sizeof(hdr));
    if (len > 0) {
        uart_write_bytes(MUX_UART, payload, len);
    }
    uart_write_bytes(MUX_UART, crc_bytes, sizeof(crc_bytes));
}

esp_err_t mux_send(uint8_t chan, const uint8_t *payload, uint16_t len)
{
    if (len > MUX_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    if (xSemaphoreTake(s_tx_mutex, MUX_TX_MUTEX_TIMEOUT) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    write_frame(chan, payload, len);
    xSemaphoreGive(s_tx_mutex);
    return ESP_OK;
}

esp_err_t mux_send_nonblock(uint8_t chan, const uint8_t *payload, uint16_t len)
{
    if (len > MUX_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t free_size = 0;
    if (uart_get_tx_buffer_free_size(MUX_UART, &free_size) != ESP_OK ||
        free_size < (size_t)len + MUX_OVERHEAD) {
        return ESP_ERR_NO_MEM; // drop, never block
    }
    if (xSemaphoreTake(s_tx_mutex, 0) != pdTRUE) {
        return ESP_ERR_NO_MEM;
    }
    write_frame(chan, payload, len);
    xSemaphoreGive(s_tx_mutex);
    return ESP_OK;
}

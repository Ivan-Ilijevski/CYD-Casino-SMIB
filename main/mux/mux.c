#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/uart.h"

#include "board.h"
#include "crc16.h"
#include "mux.h"
#include "mux_frame.h"

#define MUX_UART            ((uart_port_t)MUX_UART_NUM)
#define MUX_ACC_SIZE        MUX_ACC_SIZE_FOR(MUX_MAX_PAYLOAD)
#define MUX_RX_CHUNK        256
// A UART frame can be cut off mid-flight and never finish (noise, a reset at
// the other end), so a partial frame on a stream that has gone quiet this long
// is abandoned.
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
static mux_parser_t s_parser;

static bool chan_valid(uint8_t chan)
{
    return chan == MUX_CH_SAS || chan == MUX_CH_CMS || chan == MUX_CH_LOG;
}

static void dispatch(uint8_t chan, const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)ctx;
    for (size_t i = 0; i < sizeof(s_rx_slots) / sizeof(s_rx_slots[0]); i++) {
        if (s_rx_slots[i].cb && s_rx_slots[i].chan == chan) {
            s_rx_slots[i].cb(payload, len, s_rx_slots[i].ctx);
            return;
        }
    }
}

static void mux_rx_task(void *arg)
{
    uint8_t chunk[MUX_RX_CHUNK];
    while (1) {
        int n = uart_read_bytes(MUX_UART, chunk, sizeof(chunk), pdMS_TO_TICKS(50));
        mux_parser_feed(&s_parser, chunk, n > 0 ? (size_t)n : 0);
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
    mux_parser_init(&s_parser, s_acc, sizeof(s_acc), MUX_INTERBYTE_TIMEOUT_US,
                    chan_valid, dispatch, NULL);

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

    // Written in three pieces rather than through mux_frame_write() so a
    // maximum-size payload never needs a 2 KB staging buffer on the stack.
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

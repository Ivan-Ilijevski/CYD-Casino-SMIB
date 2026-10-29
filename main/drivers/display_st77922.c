// ES3C35P display: ST77922 TDDI panel over QSPI with its built-in touch on
// I2C, plus LVGL bring-up. Implements drivers/display.h like the E32R40T's
// display_st7796.c.
//
// The panel is 320x480 portrait and can't swap axes (no validated MADCTL.MV),
// so LVGL renders the 480x320 landscape UI and this driver rotates each
// flushed strip in software before sending it.
#include <assert.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77922.h"

#include "lvgl.h"

#include "board.h"
#include "display.h"
#include "i2c_bus.h"

static const char *TAG = "display";

// Native (portrait) panel size; LCD_H_RES x LCD_V_RES is the rotated UI.
#define PANEL_W         LCD_V_RES
#define PANEL_H         LCD_H_RES
// LV_DISPLAY_ROTATION_90 or _270 picks which landscape way up the UI is.
#define UI_ROTATION     LV_DISPLAY_ROTATION_90
// Draw strip height in UI rows; a multiple of 4 (see rounder_cb).
#define BUF_LINES       40
#define BUF_PX          (LCD_H_RES * BUF_LINES)

// Panel init from the xiaozhi-esp32 lcdwiki-es3c35p board (vendor sequence
// for this HMX035 module; the component's default targets a 532x300 panel).
static const st77922_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xF1, (uint8_t[]){0x00}, 1, 0},
    {0x60, (uint8_t[]){0x00, 0x00, 0x00}, 3, 0},
    {0x65, (uint8_t[]){0x80}, 1, 0},
    {0x79, (uint8_t[]){0x06}, 1, 0},
    {0x7B, (uint8_t[]){0x00, 0x08, 0x08}, 3, 0},
    {0x80, (uint8_t[]){0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, (uint8_t[]){0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, (uint8_t[]){0x92, 0x25}, 2, 0},
    {0x87, (uint8_t[]){0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, (uint8_t[]){0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, (uint8_t[]){0x00, 0x00, 0x00}, 3, 0},
    {0x8A, (uint8_t[]){0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, (uint8_t[]){0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, (uint8_t[]){0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, (uint8_t[]){0x0C}, 1, 0},
    {0x8E, (uint8_t[]){0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, (uint8_t[]){0x00, 0x30}, 2, 0},
    {0xF1, (uint8_t[]){0x00}, 1, 0},
    {0x71, (uint8_t[]){0xD0}, 1, 0},
    {0x66, (uint8_t[]){0x02, 0x3F}, 2, 0},
    {0xBE, (uint8_t[]){0x26, 0x00, 0x9D}, 3, 0},
    {0x70, (uint8_t[]){0x01, 0xA0, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x69, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, (uint8_t[]){0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x27, 0x27}, 9, 0},
    {0x91, (uint8_t[]){0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x27, 0x27}, 9, 0},
    {0x92, (uint8_t[]){0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x27}, 10, 0},
    {0x93, (uint8_t[]){0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x27}, 10, 0},
    {0x94, (uint8_t[]){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, (uint8_t[]){0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, (uint8_t[]){0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0x97, (uint8_t[]){0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x94, 0x27, 0x00, 0x44}, 12, 0},
    {0xBA, (uint8_t[]){0x55, 0x94, 0x2D, 0x94, 0x27}, 5, 0},
    {0x9A, (uint8_t[]){0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, (uint8_t[]){0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, (uint8_t[]){0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, (uint8_t[]){0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, (uint8_t[]){0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, (uint8_t[]){0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, (uint8_t[]){0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, (uint8_t[]){0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, (uint8_t[]){0xCD, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, (uint8_t[]){0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, (uint8_t[]){0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, (uint8_t[]){0x23, 0x23}, 2, 0},
    {0xBF, (uint8_t[]){0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, (uint8_t[]){0x00}, 1, 0},
    {0x73, (uint8_t[]){0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, (uint8_t[]){0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, (uint8_t[]){0x15, 0x27}, 2, 0},
    {0x7B, (uint8_t[]){0x04, 0x57}, 2, 0},
    {0x7E, (uint8_t[]){0x01, 0x0E}, 2, 0},
    {0xBF, (uint8_t[]){0x36}, 1, 0},
    {0xE3, (uint8_t[]){0x40, 0x40}, 2, 0},
    {0xF0, (uint8_t[]){0x00}, 1, 0},
    {0xD0, (uint8_t[]){0x00}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, (uint8_t[]){0x00}, 0, 0},
    {0x11, (uint8_t[]){0x00}, 0, 120},
    {0x29, (uint8_t[]){0x00}, 0, 0},
    {0x2C, (uint8_t[]){0x00}, 0, 0},
    {0x3A, (uint8_t[]){0x01}, 1, 0},
    {0x36, (uint8_t[]){0x00}, 1, 0},
    {0x35, (uint8_t[]){0x01}, 1, 20},
};

// --- Touch: the ST77922's integrated controller (FT6336G-like protocol,
// 16-bit register addresses) ---
#define TOUCH_I2C_ADDR          0x55
#define TOUCH_REG_MAX_TOUCHES   0x0009
#define TOUCH_REG_INFO          0x0010
#define TOUCH_REG_POINT0        0x0014
#define TOUCH_POINT_BYTES       7
#define TOUCH_MAX_POINTS        5

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static i2c_master_dev_handle_t tp_dev = NULL;
static uint8_t tp_max_points = 1;
static lv_disp_t *disp = NULL;
static lv_indev_t *indev = NULL;

static lv_color_t *buf1 = NULL;    // LVGL renders here (UI orientation)
static uint16_t *rot_buf = NULL;   // rotated copy the DMA sends

void display_backlight_on(void)
{
    gpio_config_t io = {
        .pin_bit_mask = 1ULL << PIN_NUM_BK_LIGHT,
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = 0,
        .pull_down_en = 0,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(PIN_NUM_BK_LIGHT, 1);
}

static void lv_tick_task(void *arg)
{
    (void)arg;
    lv_tick_inc(1);
}

static void lvgl_flush_cb(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    lv_color_format_t cf = lv_display_get_color_format(display);
    int32_t w = lv_area_get_width(area);
    int32_t h = lv_area_get_height(area);

    // Rotate the strip into panel orientation, then byte-swap for the
    // big-endian panel (the rotate doesn't take RGB565_SWAPPED).
    //
    // The lv_draw_sw_* spelling rather than LVGL 9.6's lv_draw_* wrappers:
    // these exist with identical signatures in both 9.5 and 9.6, so this
    // driver does not pin the whole project (and the desktop sim) to 9.6.
    // See "Dependencies" in ../../README.md.
    lv_area_t panel_area = *area;
    lv_display_rotate_area(display, &panel_area);
    lv_draw_sw_rotate(px_map, rot_buf, w, h,
                      lv_draw_buf_width_to_stride(w, cf), lv_draw_buf_width_to_stride(h, cf),
                      lv_display_get_rotation(display), cf);
    lv_draw_sw_rgb565_swap(rot_buf, w * h);

    // draw_bitmap only queues the QSPI/DMA transfer; flush_ready is signaled
    // from lcd_trans_done_cb once it completes, so neither buffer is reused
    // while DMA still reads rot_buf. End coords are exclusive.
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, panel_area.x1, panel_area.y1,
                                              panel_area.x2 + 1, panel_area.y2 + 1, rot_buf));
}

static bool lcd_trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

// The ST77922 needs column windows aligned to 4 pixels. Panel columns are UI
// rows after the 90/270 rotation, so round the UI area's rows. LVGL also
// sizes its render strips through this callback, keeping them aligned too.
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_invalidated_area(e);
    area->y1 &= ~3;
    area->y2 |= 3;
}

// The panel powers up with whatever was left in its GRAM, which reads as
// full-screen snow until LVGL's first flush ~a second later. Paint it black
// as soon as the panel is up; it also makes a dead pixel path obvious, since
// a panel that still shows snow after this never accepted a RAMWR.
static void clear_panel(void)
{
    const int rows = 16;
    uint16_t *black = heap_caps_calloc(PANEL_W * rows, sizeof(uint16_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!black) {
        ESP_LOGW(TAG, "no DMA buffer to clear the panel");
        return;
    }
    for (int y = 0; y < PANEL_H; y += rows) {
        ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, 0, y, PANEL_W, y + rows, black));
    }
    // draw_bitmap only queues the transfer, so let the DMA drain before the
    // buffer goes away. The whole panel is ~10 ms of QSPI at 80 MHz.
    vTaskDelay(pdMS_TO_TICKS(100));
    free(black);
}

static void init_lcd_st77922(void)
{
    // 1) Init QSPI bus
    spi_bus_config_t buscfg = ST77922_PANEL_BUS_QSPI_CONFIG(PIN_NUM_LCD_SCK,
        PIN_NUM_LCD_D0, PIN_NUM_LCD_D1, PIN_NUM_LCD_D2, PIN_NUM_LCD_D3,
        BUF_PX * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2) Create panel IO (QSPI); 80 MHz as in the vendor/xiaozhi setup
    esp_lcd_panel_io_spi_config_t io_config = ST77922_PANEL_IO_QSPI_CONFIG(PIN_NUM_LCD_CS, NULL, NULL);
    io_config.pclk_hz = 80 * 1000 * 1000;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    // 3) Create ST77922 panel
    const st77922_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77922(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    // This panel displays a photo-negative without inversion: the vendor
    // sequence turns it on (0x21) and it has to stay on, so pass true here
    // rather than letting the component send INVOFF.
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, true));
    // MADCTL ends at 0x00. The vendor sequence's own 0x36 overwrites both the
    // panel register and the component's cached madctl_val, so the BGR bit
    // from rgb_ele_order is already gone by here and mirror() cannot restore
    // it. 0x00 is what this module wants; the xiaozhi board behaves the same.
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, false, false));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
    clear_panel();
    ESP_LOGI(TAG, "ST77922 panel up (%dx%d native)", PANEL_W, PANEL_H);
}

static esp_err_t tp_read(uint16_t reg, uint8_t *data, size_t len)
{
    uint8_t addr[2] = { reg >> 8, reg & 0xFF };
    return i2c_master_transmit_receive(tp_dev, addr, sizeof(addr), data, len, 100);
}

static void init_touch(void)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = TOUCH_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_bus_get(), &dev_cfg, &tp_dev));

    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_NUM_TOUCH_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst));
    gpio_set_level(PIN_NUM_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(PIN_NUM_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    uint8_t max_pts = 0;
    if (tp_read(TOUCH_REG_MAX_TOUCHES, &max_pts, 1) == ESP_OK &&
        max_pts > 0 && max_pts <= TOUCH_MAX_POINTS) {
        tp_max_points = max_pts;
        ESP_LOGI(TAG, "ST77922 touch initialized (%d points)", tp_max_points);
    } else {
        ESP_LOGW(TAG, "ST77922 touch not answering at 0x%02X", TOUCH_I2C_ADDR);
    }
}

// Reports panel-native (portrait) coordinates; LVGL applies the display
// rotation to pointer input itself.
static void lvgl_touch_read_cb(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    static int32_t last_x, last_y;
    data->state = LV_INDEV_STATE_RELEASED;
    data->point.x = last_x;
    data->point.y = last_y;

    uint8_t info;
    if (tp_read(TOUCH_REG_INFO, &info, 1) != ESP_OK || !(info & 0x08)) {
        return;
    }
    uint8_t buf[TOUCH_POINT_BYTES * TOUCH_MAX_POINTS];
    if (tp_read(TOUCH_REG_POINT0, buf, TOUCH_POINT_BYTES * tp_max_points) != ESP_OK) {
        return;
    }
    for (int i = 0; i < tp_max_points; i++) {
        const uint8_t *p = &buf[i * TOUCH_POINT_BYTES];
        if (p[0] & 0x80) { // point valid
            last_x = ((p[0] & 0x3F) << 8) | p[1];
            last_y = ((p[2] & 0x3F) << 8) | p[3];
            data->state = LV_INDEV_STATE_PRESSED;
            data->point.x = last_x;
            data->point.y = last_y;
            return;
        }
    }
}

static void init_lvgl(void)
{
    lv_init();

    size_t buf_size = BUF_PX * sizeof(lv_color_t);
    buf1 = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    rot_buf = heap_caps_malloc(buf_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(buf1 && rot_buf);

    disp = lv_display_create(PANEL_W, PANEL_H);
    lv_display_set_rotation(disp, UI_ROTATION);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_add_event_cb(disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    // Signal flush_ready only once the QSPI/DMA transfer has actually
    // completed, instead of immediately after queueing it (see lvgl_flush_cb).
    const esp_lcd_panel_io_callbacks_t cbs = {
        .on_color_trans_done = lcd_trans_done_cb,
    };
    ESP_ERROR_CHECK(esp_lcd_panel_io_register_event_callbacks(io_handle, &cbs, disp));

    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, lvgl_touch_read_cb);
    lv_indev_set_display(indev, disp);

    // 1ms tick using esp_timer
    const esp_timer_create_args_t periodic_timer_args = {
        .callback = &lv_tick_task,
        .name = "lv_tick"
    };
    esp_timer_handle_t periodic_timer;
    ESP_ERROR_CHECK(esp_timer_create(&periodic_timer_args, &periodic_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(periodic_timer, 1000)); // 1000us = 1ms
}

void display_init(void)
{
    init_lcd_st77922();
    init_touch();
    init_lvgl();
}

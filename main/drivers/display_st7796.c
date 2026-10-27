#include <assert.h>
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
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_st7796.h"
#include "esp_lcd_touch_xpt2046.h"

#include "lvgl.h"

#include "board.h"
#include "display.h"

static const char *TAG = "display";

static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_touch_handle_t tp_handle = NULL;
static esp_lcd_panel_io_handle_t tp_io_handle = NULL;
static lv_disp_t *disp = NULL;
static lv_indev_t *indev = NULL;

// Small partial buffer (RAM-friendly)
static lv_color_t *buf1 = NULL;

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
    // LVGL provides RGB565 pixel data in px_map when LV_COLOR_DEPTH=16
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;

    // esp_lcd expects end coords are exclusive.
    // draw_bitmap only queues the SPI/DMA transfer; flush_ready is signaled
    // later from lcd_trans_done_cb once the transfer actually completes,
    // otherwise LVGL reuses buf1 while DMA is still reading it (tearing).
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel_handle, x1, y1, x2 + 1, y2 + 1, px_map));
}

static bool lcd_trans_done_cb(esp_lcd_panel_io_handle_t io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_display_flush_ready((lv_display_t *)user_ctx);
    return false;
}

static void init_lcd_st7796(void)
{
    // 1) Init SPI bus
    spi_bus_config_t buscfg = {
        .sclk_io_num = PIN_NUM_LCD_SCK,
        .mosi_io_num = PIN_NUM_LCD_MOSI,
        .miso_io_num = PIN_NUM_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * 60 * 2 + 8, // match our buffer height (60 lines)
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

    // 2) Create panel IO (SPI)
    esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = PIN_NUM_LCD_DC,
        .cs_gpio_num = PIN_NUM_LCD_CS,
        .pclk_hz = 40 * 1000 * 1000, // 40MHz - CYD supports up to 60MHz
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

    // 3) Create ST7796 panel
    esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_NUM_LCD_RST,
        .color_space = ESP_LCD_COLOR_SPACE_BGR,
        .bits_per_pixel = 16,
    };

    ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

    // Configure display orientation for CYD (landscape mode)
    ESP_ERROR_CHECK(esp_lcd_panel_invert_color(panel_handle, false));
    ESP_ERROR_CHECK(esp_lcd_panel_swap_xy(panel_handle, true));   // swap axes for landscape
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, true));  // 180 rotation

    // Set gap to fix display offset issue
    ESP_ERROR_CHECK(esp_lcd_panel_set_gap(panel_handle, 0, 0));

    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
}

static void init_touch_xpt2046(void)
{
    // XPT2046 shares the LCD's SPI bus (pins 14/13/12), with its own CS/IRQ.
    esp_lcd_panel_io_spi_config_t tp_io_config = ESP_LCD_TOUCH_IO_SPI_XPT2046_CONFIG(PIN_NUM_TOUCH_CS);
    tp_io_config.pclk_hz = 1 * 1000 * 1000; // XPT2046 max ~2MHz
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &tp_io_config, &tp_io_handle));

    // Same axis swap/mirror as the LCD panel so touch coords match landscape orientation.
    esp_lcd_touch_config_t tp_config = {
        .x_max = LCD_V_RES, // need to swap X/Y for landscape orientation
        .y_max = LCD_H_RES,
        .rst_gpio_num = -1,
        .int_gpio_num = PIN_NUM_TOUCH_IRQ,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 1,
            .mirror_x = 0,   // 180 rotation (flip both axes to match panel)
            .mirror_y = 0,
        },
    };
    ESP_ERROR_CHECK(esp_lcd_touch_new_spi_xpt2046(tp_io_handle, &tp_config, &tp_handle));
    ESP_LOGI(TAG, "XPT2046 touch initialized");
}

static void lvgl_touch_read_cb(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    esp_err_t err = esp_lcd_touch_read_data(tp_handle);
    if (err != ESP_OK) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    esp_lcd_touch_point_data_t point_data;
    uint8_t point_num = 0;
    // esp_lcd_touch_get_data returns esp_err_t (ESP_OK on success either way);
    // whether a touch was detected is signaled via point_num, not the return value.
    esp_lcd_touch_get_data(tp_handle, &point_data, &point_num, 1);

    if (point_num > 0) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = point_data.x;
        data->point.y = point_data.y;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

static void init_lvgl(void)
{
    lv_init();

    buf1 = heap_caps_malloc(LCD_H_RES * 60 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    assert(buf1);

    disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, lvgl_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
    lv_display_set_buffers(disp, buf1, NULL, LCD_H_RES * 60 * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);

    // Signal flush_ready only once the SPI/DMA transfer has actually
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
    init_lcd_st7796();
    init_touch_xpt2046();
    init_lvgl();
}

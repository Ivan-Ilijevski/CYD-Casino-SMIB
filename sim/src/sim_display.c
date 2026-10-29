// Display + touch: an SDL window stands in for the ST7796 and the mouse for
// the XPT2046. Replaces main/drivers/display.c; LVGL and everything above it
// are the firmware's own code, rendering into the same partial draw buffer
// as on the board. Also pumps SDL events, which is where the keyboard (NFC,
// reset, ...) and the control channel get serviced.

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <SDL.h>

#include "board.h"
#include "drivers/display.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sim.h"

static const char *TAG = "display";

static SDL_Window *s_window;
static SDL_Renderer *s_renderer;
static SDL_Texture *s_texture;
static uint16_t s_fb[LCD_V_RES][LCD_H_RES]; // RGB565: what the panel shows
static atomic_bool s_title_dirty = true;

static struct {
    bool down;    // left button held
    bool latched; // pressed since LVGL's last read, so a quick click isn't missed
    int x, y;     // screen pixels
    int dx, dy;   // fat-finger offset for the current press
    bool tap;     // ctl "tap" in progress (overrides the mouse)
    int tap_x, tap_y;
    uint32_t tap_until_ms;
} s_touch;

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void present(void)
{
    SDL_UpdateTexture(s_texture, NULL, s_fb, LCD_H_RES * sizeof(uint16_t));
    SDL_RenderClear(s_renderer);
    SDL_RenderCopy(s_renderer, s_texture, NULL, NULL);
    SDL_RenderPresent(s_renderer);
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    int32_t w = lv_area_get_width(area);
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_RGB565);
    for (int32_t y = area->y1; y <= area->y2; y++) {
        memcpy(&s_fb[y][area->x1], px_map + (size_t)(y - area->y1) * stride,
               (size_t)w * sizeof(uint16_t));
    }
    if (lv_display_flush_is_last(disp)) {
        present();
    }
    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev;
    if (s_touch.tap) {
        data->point.x = s_touch.tap_x;
        data->point.y = s_touch.tap_y;
        if ((int32_t)(s_touch.tap_until_ms - tick_ms()) > 0) {
            data->state = LV_INDEV_STATE_PRESSED;
        } else {
            data->state = LV_INDEV_STATE_RELEASED;
            s_touch.tap = false;
        }
        return;
    }
    bool pressed = s_touch.down || s_touch.latched;
    s_touch.latched = false;
    data->point.x = clampi(s_touch.x + s_touch.dx, 0, LCD_H_RES - 1);
    data->point.y = clampi(s_touch.y + s_touch.dy, 0, LCD_V_RES - 1);
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void mouse_at(int wx, int wy)
{
    s_touch.x = clampi(wx / g_sim.zoom, 0, LCD_H_RES - 1);
    s_touch.y = clampi(wy / g_sim.zoom, 0, LCD_V_RES - 1);
}

static void update_title(void)
{
    char title[160];
    snprintf(title, sizeof(title), "SMIB sim  —  %s  —  host %s", sim_nfc_status(),
             sim_uart_host_attached() ? "attached" : "not attached");
    SDL_SetWindowTitle(s_window, title);
}

static void screenshot_key(void)
{
    char path[64];
    time_t now = time(NULL);
    strftime(path, sizeof(path), "smib-sim-%Y%m%d-%H%M%S.bmp", localtime(&now));
    if (sim_display_screenshot(path)) {
        sim_printf("screenshot saved: %s", path);
    }
}

static void on_key(SDL_Keycode key, bool down)
{
    switch (key) {
    case SDLK_1: case SDLK_KP_1: sim_nfc_key(0, down); return;
    case SDLK_2: case SDLK_KP_2: sim_nfc_key(1, down); return;
    case SDLK_3: case SDLK_KP_3: sim_nfc_key(2, down); return;
    case SDLK_f: sim_nfc_flaky(down); return;
    case SDLK_d: sim_door_key(down); return;
    default: break;
    }
    if (!down) {
        return;
    }
    switch (key) {
    case SDLK_r: sim_reset(); break;
    case SDLK_s: screenshot_key(); break;
    case SDLK_m: sim_audio_toggle_mute(); break;
    case SDLK_h: sim_print_help(); break;
    default: break;
    }
}

static void sdl_poll_cb(lv_timer_t *timer)
{
    (void)timer;
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        switch (e.type) {
        case SDL_QUIT:
            sim_printf("window closed: exiting");
            sim_exit(0);
        case SDL_MOUSEBUTTONDOWN:
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouse_at(e.button.x, e.button.y);
                s_touch.down = s_touch.latched = true;
                int r = g_sim.fat_finger_px;
                s_touch.dx = r > 0 ? (int)(sim_rand() % (uint32_t)(2 * r + 1)) - r : 0;
                s_touch.dy = r > 0 ? (int)(sim_rand() % (uint32_t)(2 * r + 1)) - r : 0;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            if (e.button.button == SDL_BUTTON_LEFT) {
                mouse_at(e.button.x, e.button.y);
                s_touch.down = false;
            }
            break;
        case SDL_MOUSEMOTION:
            mouse_at(e.motion.x, e.motion.y);
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
            if (!e.key.repeat) {
                on_key(e.key.keysym.sym, e.type == SDL_KEYDOWN);
            }
            break;
        case SDL_WINDOWEVENT:
            if (e.window.event == SDL_WINDOWEVENT_EXPOSED) {
                present();
            } else if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                // Key/button releases outside the window never arrive.
                s_touch.down = false;
                sim_nfc_release_all();
            }
            break;
        default:
            break;
        }
    }
    if (atomic_exchange(&s_title_dirty, false)) {
        update_title();
    }
    sim_ctl_poll();
}

// ---------------------------------------------------------------- sim API

void sim_display_request_title_update(void)
{
    atomic_store(&s_title_dirty, true);
}

void sim_display_tap(int x, int y, int ms)
{
    s_touch.tap = true;
    s_touch.tap_x = clampi(x, 0, LCD_H_RES - 1);
    s_touch.tap_y = clampi(y, 0, LCD_V_RES - 1);
    // At least two of LVGL's input reads (one per refresh period).
    s_touch.tap_until_ms = tick_ms() + (uint32_t)(ms < 80 ? 80 : ms);
}

bool sim_display_screenshot(const char *path)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(
        s_fb, LCD_H_RES, LCD_V_RES, 16, LCD_H_RES * sizeof(uint16_t), SDL_PIXELFORMAT_RGB565);
    bool ok = surface && SDL_SaveBMP(surface, path) == 0;
    if (!ok) {
        sim_printf("screenshot %s failed: %s", path, SDL_GetError());
    }
    SDL_FreeSurface(surface);
    return ok;
}

bool sim_display_window_pos(int *x, int *y)
{
    if (!s_window) {
        return false;
    }
    SDL_GetWindowPosition(s_window, x, y);
    return true;
}

// ---------------------------------------------------------------- driver API

void display_backlight_on(void)
{
}

void display_init(void)
{
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        sim_die("SDL_Init: %s", SDL_GetError());
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); // crisp pixels when zoomed
    int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
    const char *pos = getenv(SIM_ENV_WINPOS); // same spot after a reset
    if (pos) {
        sscanf(pos, "%d,%d", &x, &y);
    }
    s_window = SDL_CreateWindow("SMIB sim", x, y, LCD_H_RES * g_sim.zoom,
                                LCD_V_RES * g_sim.zoom, 0);
    if (!s_window) {
        sim_die("SDL_CreateWindow: %s", SDL_GetError());
    }
    s_renderer = SDL_CreateRenderer(s_window, -1, 0);
    if (!s_renderer) {
        sim_die("SDL_CreateRenderer: %s", SDL_GetError());
    }
    s_texture = SDL_CreateTexture(s_renderer, SDL_PIXELFORMAT_RGB565,
                                  SDL_TEXTUREACCESS_STREAMING, LCD_H_RES, LCD_V_RES);
    if (!s_texture) {
        sim_die("SDL_CreateTexture: %s", SDL_GetError());
    }
    present();

    lv_init();
    lv_tick_set_cb(tick_ms);

    // Same partial draw buffer as main/drivers/display.c, so LVGL renders in
    // the same bands as on the board.
    size_t buf_size = LCD_H_RES * 60 * sizeof(lv_color_t);
    void *buf = malloc(buf_size);
    if (!buf) {
        sim_die("out of memory");
    }
    lv_display_t *disp = lv_display_create(LCD_H_RES, LCD_V_RES);
    lv_display_set_flush_cb(disp, flush_cb);
    // The board uses RGB565_SWAPPED (byte order for the SPI panel); SDL
    // takes native RGB565. Same pixels either way.
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf, NULL, buf_size, LV_DISPLAY_RENDER_MODE_PARTIAL);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    lv_timer_create(sdl_poll_cb, 5, NULL);
    ESP_LOGI(TAG, "SDL window %dx%d (zoom %d) stands in for ST7796 + XPT2046", LCD_H_RES,
             LCD_V_RES, g_sim.zoom);
}

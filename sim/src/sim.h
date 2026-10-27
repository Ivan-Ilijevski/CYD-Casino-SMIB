// SMIB desktop simulator: the pieces that stand in for the board's hardware.
#pragma once

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    const char *tty_link;  // symlink to the virtual serial port
    const char *ctl_path;  // control FIFO
    int zoom;              // window scale
    bool fast_uart;        // don't pace TX to the baud rate
    double noise;          // per-byte bit-flip probability on the link
    int fat_finger_px;     // max random touch offset
    bool mute;
    bool logs;             // echo firmware logs to the sim's terminal
} sim_options_t;

extern sim_options_t g_sim;

// Environment carried across a simulated reset (re-exec).
#define SIM_ENV_PTY    "SMIB_SIM_PTY"    // "<master fd>:<slave path>"
#define SIM_ENV_RESET  "SMIB_SIM_RESET"
#define SIM_ENV_WINPOS "SMIB_SIM_WINPOS" // "<x>,<y>"

// sim_main.c
void sim_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void sim_vprint_fw_log(const char *fmt, va_list args);
void sim_die(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
uint32_t sim_rand(void);
void sim_print_help(void);
void sim_reset(void);                     // simulated board reset; keeps the pty
void sim_exit(int code) __attribute__((noreturn));

// port/esp.c
void sim_log_echo(bool on);

// port/uart_pty.c
void sim_uart_open(const char *link_path);
void sim_uart_boot_banner(bool after_reset);
const char *sim_uart_pty_path(void);
int sim_uart_master_fd(void);
bool sim_uart_host_attached(void);
void sim_uart_remove_link(void);          // async-signal-safe

// sim_display.c (app/LVGL thread unless noted)
void sim_display_request_title_update(void); // any thread
void sim_display_tap(int x, int y, int ms);
bool sim_display_screenshot(const char *path);
bool sim_display_window_pos(int *x, int *y);

// sim_nfc.c (app/LVGL thread)
void sim_nfc_key(int card, bool down);    // card = 0..2 for keys 1..3
void sim_nfc_flaky(bool down);
bool sim_nfc_command(const char *arg);    // ctl "card ..."
void sim_nfc_release_all(void);           // window lost focus
const char *sim_nfc_status(void);

// sim_audio.c
void sim_audio_toggle_mute(void);

// sim_ctl.c
void sim_ctl_start(const char *fifo_path);
void sim_ctl_poll(void);                  // app/LVGL thread: runs queued commands
void sim_ctl_remove_fifo(void);           // async-signal-safe

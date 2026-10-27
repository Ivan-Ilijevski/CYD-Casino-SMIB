#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "esp_log.h"

#include "mux.h"
#include "log_bridge.h"

// Re-entrancy guard: if formatting/sending a log ever triggers another log
// (e.g. from inside the UART driver), drop it instead of recursing. The flag
// is deliberately global, not per-task — the rare concurrent log from another
// task gets dropped too, which is an acceptable trade for never deadlocking.
static volatile bool s_in_log;

static int log_bridge_vprintf(const char *fmt, va_list args)
{
    if (s_in_log) {
        return 0;
    }
    s_in_log = true;

#if !CONFIG_ESP_CONSOLE_NONE
    // Boards with a console of their own (ES3C35P: USB-Serial-JTAG, which is
    // not UART0) also get the line there, so boot can be watched without the
    // host sim. va_list is consumed, so copy before formatting below.
    va_list console_args;
    va_copy(console_args, args);
    vprintf(fmt, console_args);
    va_end(console_args);
#endif

    char buf[256];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n < 0) {
        s_in_log = false;
        return 0;
    }
    if (n > (int)sizeof(buf) - 1) {
        n = (int)sizeof(buf) - 1; // truncated
    }
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) {
        n--;
    }
    if (n > 0) {
        mux_send_nonblock(MUX_CH_LOG, (const uint8_t *)buf, (uint16_t)n);
    }

    s_in_log = false;
    return n;
}

void log_bridge_install(void)
{
    esp_log_set_vprintf(log_bridge_vprintf);
}

// SMIB desktop simulator entry point. Sets up the virtual serial port and
// the control channel, then runs the firmware's own app_main() on the main
// thread: SDL needs its window and events there, and app_main's loop is
// the LVGL thread on the board too.

#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#include "board.h"
#include "sim.h"

void app_main(void);

sim_options_t g_sim = {
    .tty_link = "/tmp/smib-sim.tty",
    .ctl_path = "/tmp/smib-sim.ctl",
    .zoom = 2,
};

static char **s_argv;
static char s_exe[PATH_MAX];
static pthread_mutex_t s_print_mu = PTHREAD_MUTEX_INITIALIZER;

// ---------------------------------------------------------------- output

void sim_printf(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    pthread_mutex_lock(&s_print_mu);
    fputs("\033[36m[sim]\033[0m ", stdout);
    vprintf(fmt, args);
    fputc('\n', stdout);
    fflush(stdout);
    pthread_mutex_unlock(&s_print_mu);
    va_end(args);
}

void sim_vprint_fw_log(const char *fmt, va_list args)
{
    pthread_mutex_lock(&s_print_mu);
    fputs("\033[90m[fw]\033[0m  ", stdout);
    vprintf(fmt, args);
    fflush(stdout);
    pthread_mutex_unlock(&s_print_mu);
}

void sim_die(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fputs("smib_sim: ", stderr);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
    va_end(args);
    sim_exit(1);
}

uint32_t sim_rand(void)
{
    // xorshift64, per thread; plenty for noise and jitter.
    static _Thread_local uint64_t state;
    if (state == 0) {
        state = (uint64_t)time(NULL) ^ (uint64_t)(uintptr_t)&state ^ 0x9E3779B97F4A7C15ull;
    }
    state ^= state << 13;
    state ^= state >> 7;
    state ^= state << 17;
    return (uint32_t)(state >> 32);
}

void sim_print_help(void)
{
    sim_printf("keys (sim window focused):");
    sim_printf("  hold 1 / 2 / 3   card on the reader: player 9C 76 5A F4 / unknown DE AD BE EF /"
               " 2nd player 04 A2 3B 1C");
    sim_printf("  hold F           flaky player card (flickers like one at the edge of range)");
    sim_printf("  mouse            touch      R reset board   S screenshot   M mute   H help");
    sim_printf("control FIFO %s: card 1|2|3|flaky|<uid>|off, tap X Y [ms], shot FILE.bmp, mem,"
               " reset, quit", g_sim.ctl_path);
}

// ---------------------------------------------------------------- lifecycle

static void remove_links(void)
{
    sim_uart_remove_link();
    sim_ctl_remove_fifo();
}

void sim_exit(int code)
{
    remove_links();
    _exit(code);
}

static void on_fatal_signal(int sig)
{
    // Don't leave a symlink to a dead pty: its /dev/ttys* name gets reused.
    remove_links();
    signal(sig, SIG_DFL);
    raise(sig);
}

void sim_reset(void)
{
    sim_printf("reset: rebooting the firmware (the serial port stays open)");
    char pty[PATH_MAX + 16];
    snprintf(pty, sizeof(pty), "%d:%s", sim_uart_master_fd(), sim_uart_pty_path());
    setenv(SIM_ENV_PTY, pty, 1);
    setenv(SIM_ENV_RESET, "1", 1);
    int x, y;
    if (sim_display_window_pos(&x, &y)) {
        char pos[32];
        snprintf(pos, sizeof(pos), "%d,%d", x, y);
        setenv(SIM_ENV_WINPOS, pos, 1);
    }
    fflush(stdout);
    fflush(stderr);
    execv(s_exe, s_argv);
    sim_printf("reset failed: exec %s: %s", s_exe, strerror(errno));
}

static void find_self(const char *argv0)
{
#ifdef __APPLE__
    uint32_t size = sizeof(s_exe);
    if (_NSGetExecutablePath(s_exe, &size) == 0) {
        return;
    }
#else
    ssize_t n = readlink("/proc/self/exe", s_exe, sizeof(s_exe) - 1);
    if (n > 0) {
        s_exe[n] = '\0';
        return;
    }
#endif
    snprintf(s_exe, sizeof(s_exe), "%s", argv0);
}

// ---------------------------------------------------------------- options

static void usage(FILE *out)
{
    fprintf(out,
            "usage: smib_sim [options]\n"
            "  --tty PATH        symlink to the virtual serial port (default %s)\n"
            "  --ctl PATH        control FIFO for scripts (default %s)\n"
            "  --zoom N          window scale (default %d)\n"
            "  --fast-uart       don't pace the link to 115200 baud\n"
            "  --noise P         flip a random bit in each link byte with probability P\n"
            "  --fat-finger PX   offset each touch by up to PX pixels, like a finger on\n"
            "                    the resistive panel (try 8)\n"
            "  --mute            no sound (cues are still printed)\n"
            "  --logs            also print firmware logs here (they always go over the mux)\n",
            g_sim.tty_link, g_sim.ctl_path, g_sim.zoom);
}

static void parse_args(int argc, char **argv)
{
    enum { OPT_TTY = 1, OPT_CTL, OPT_ZOOM, OPT_FAST, OPT_NOISE, OPT_FAT, OPT_MUTE, OPT_LOGS };
    static const struct option opts[] = {
        { "tty", required_argument, NULL, OPT_TTY },
        { "ctl", required_argument, NULL, OPT_CTL },
        { "zoom", required_argument, NULL, OPT_ZOOM },
        { "fast-uart", no_argument, NULL, OPT_FAST },
        { "noise", required_argument, NULL, OPT_NOISE },
        { "fat-finger", required_argument, NULL, OPT_FAT },
        { "mute", no_argument, NULL, OPT_MUTE },
        { "logs", no_argument, NULL, OPT_LOGS },
        { "help", no_argument, NULL, 'h' },
        { NULL, 0, NULL, 0 },
    };
    int c;
    while ((c = getopt_long(argc, argv, "h", opts, NULL)) != -1) {
        switch (c) {
        case OPT_TTY:   g_sim.tty_link = optarg; break;
        case OPT_CTL:   g_sim.ctl_path = optarg; break;
        case OPT_ZOOM:  g_sim.zoom = atoi(optarg); break;
        case OPT_FAST:  g_sim.fast_uart = true; break;
        case OPT_NOISE: g_sim.noise = atof(optarg); break;
        case OPT_FAT:   g_sim.fat_finger_px = atoi(optarg); break;
        case OPT_MUTE:  g_sim.mute = true; break;
        case OPT_LOGS:  g_sim.logs = true; break;
        case 'h':       usage(stdout); exit(0);
        default:        usage(stderr); exit(2);
        }
    }
    if (g_sim.zoom < 1 || g_sim.zoom > 6) {
        g_sim.zoom = 2;
    }
    if (g_sim.noise < 0 || g_sim.noise > 1) {
        sim_die("--noise must be between 0 and 1");
    }
}

int main(int argc, char **argv)
{
    s_argv = argv;
    find_self(argv[0]);
    parse_args(argc, argv);

    signal(SIGPIPE, SIG_IGN);
    signal(SIGINT, on_fatal_signal);
    signal(SIGTERM, on_fatal_signal);
    signal(SIGHUP, on_fatal_signal);
    signal(SIGABRT, on_fatal_signal); // ESP_ERROR_CHECK / configASSERT failures
    // SIGSEGV is left to AddressSanitizer, whose report is worth more.

    bool after_reset = getenv(SIM_ENV_RESET) != NULL;
    sim_log_echo(g_sim.logs);
    sim_uart_open(g_sim.tty_link);
    sim_ctl_start(g_sim.ctl_path);

    if (!after_reset) {
        sim_printf("SMIB firmware %s on the desktop", SMIB_FW_VERSION);
        sim_printf("serial port %s -> %s", g_sim.tty_link, sim_uart_pty_path());
        sim_printf("  host:  tools/host_sim/venv/bin/python tools/host_sim/host_sim.py --port %s",
                   g_sim.tty_link);
        sim_print_help();
    }
    sim_uart_boot_banner(after_reset);

    app_main(); // never returns
    return 0;
}

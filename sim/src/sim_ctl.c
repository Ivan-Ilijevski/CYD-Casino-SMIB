// Control channel for scripts and tests: newline-separated commands on a
// FIFO (default /tmp/smib-sim.ctl) and, when run interactively, on stdin.
//
//   card 1|2|3|flaky|<UID>|off put a card on / take it off the reader
//   tap X Y [MS]               touch the screen at (X, Y) for MS ms (default 100)
//   shot FILE.bmp              save what the screen shows
//   mem                        LVGL heap usage
//   reset                      simulated board reset (like the R key)
//   quit
//
// Readers only queue lines; they run on the app/LVGL thread (sim_ctl_poll),
// like touch input.

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "lvgl.h"
#include "sim.h"

#define QUEUE_LEN 32
#define LINE_MAX_LEN 256

static pthread_mutex_t s_mu = PTHREAD_MUTEX_INITIALIZER;
static char s_lines[QUEUE_LEN][LINE_MAX_LEN];
static int s_head, s_count;
static char s_fifo[1024];

static void enqueue(const char *line)
{
    pthread_mutex_lock(&s_mu);
    if (s_count < QUEUE_LEN) {
        snprintf(s_lines[(s_head + s_count) % QUEUE_LEN], LINE_MAX_LEN, "%s", line);
        s_count++;
    }
    pthread_mutex_unlock(&s_mu);
}

static void *reader_thread(void *arg)
{
    FILE *in = arg;
    char line[LINE_MAX_LEN];
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\r\n")] = '\0';
        if (line[0]) {
            enqueue(line);
        }
    }
    return NULL;
}

static void start_reader(FILE *in)
{
    pthread_t thread;
    if (pthread_create(&thread, NULL, reader_thread, in) == 0) {
        pthread_detach(thread);
    }
}

static void run(char *line)
{
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    char *rest = save; // remainder of the line (for UIDs with spaces)
    if (!cmd) {
        return;
    }
    if (strcmp(cmd, "card") == 0) {
        while (rest && (*rest == ' ' || *rest == '\t')) {
            rest++;
        }
        if (!rest || !*rest) {
            sim_printf("usage: card 1|2|3|flaky|<uid>|off");
            return;
        }
        sim_nfc_command(rest);
    } else if (strcmp(cmd, "tap") == 0) {
        int x, y, ms = 100;
        if (!rest || sscanf(rest, "%d %d %d", &x, &y, &ms) < 2) {
            sim_printf("usage: tap X Y [ms]");
            return;
        }
        sim_display_tap(x, y, ms);
    } else if (strcmp(cmd, "shot") == 0) {
        char *path = strtok_r(NULL, " \t", &save);
        if (!path) {
            sim_printf("usage: shot FILE.bmp");
            return;
        }
        if (sim_display_screenshot(path)) {
            sim_printf("screenshot saved: %s", path);
        }
    } else if (strcmp(cmd, "mem") == 0) {
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        sim_printf("LVGL heap: %d%% used (%u of %u bytes), peak %u, fragmentation %d%%",
                   mon.used_pct, (unsigned)(mon.total_size - mon.free_size),
                   (unsigned)mon.total_size, (unsigned)mon.max_used, mon.frag_pct);
    } else if (strcmp(cmd, "reset") == 0) {
        sim_reset();
    } else if (strcmp(cmd, "quit") == 0) {
        sim_exit(0);
    } else if (strcmp(cmd, "help") == 0) {
        sim_print_help();
    } else {
        sim_printf("unknown command '%s' (try help)", cmd);
    }
}

// ---------------------------------------------------------------- sim API

void sim_ctl_start(const char *fifo_path)
{
    struct stat st;
    if (lstat(fifo_path, &st) == 0) {
        if (!S_ISFIFO(st.st_mode)) {
            sim_die("%s exists and is not a FIFO; pick another --ctl path", fifo_path);
        }
        // Reused as is (e.g. after a reset), so a writer holding it open
        // keeps working.
    } else if (mkfifo(fifo_path, 0600) != 0) {
        sim_die("mkfifo %s: %s", fifo_path, strerror(errno));
    }
    snprintf(s_fifo, sizeof(s_fifo), "%s", fifo_path);
    // O_RDWR keeps a writer end open, so the reader never sees EOF between
    // separate `echo ... > fifo` commands.
    int fd = open(fifo_path, O_RDWR);
    FILE *in = fd >= 0 ? fdopen(fd, "r") : NULL;
    if (!in) {
        sim_die("open %s: %s", fifo_path, strerror(errno));
    }
    start_reader(in);
    if (isatty(STDIN_FILENO)) {
        start_reader(stdin);
    }
}

void sim_ctl_poll(void)
{
    for (;;) {
        char line[LINE_MAX_LEN];
        pthread_mutex_lock(&s_mu);
        if (s_count == 0) {
            pthread_mutex_unlock(&s_mu);
            return;
        }
        memcpy(line, s_lines[s_head], LINE_MAX_LEN);
        s_head = (s_head + 1) % QUEUE_LEN;
        s_count--;
        pthread_mutex_unlock(&s_mu);
        run(line);
    }
}

void sim_ctl_remove_fifo(void)
{
    if (s_fifo[0]) {
        unlink(s_fifo);
    }
}

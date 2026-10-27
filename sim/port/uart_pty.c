// UART0 of the board -> a pseudo-terminal on the Mac. The firmware's mux
// runs unchanged on top; host_sim.py / listen.py open the pty (through the
// --tty symlink) like the CH340's /dev/cu.* port.
//
// Like the ESP-IDF driver, writes go into a TX ring buffer (the size given
// to uart_driver_install) that a thread drains at the configured baud rate,
// so a burst fills the buffer as it does on the board. With no host
// attached the bytes are dropped, as on a UART with nothing on the wire.

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <util.h>
#else
#include <pty.h>
#endif

#include "driver/uart.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "sim.h"

#define SIM_UART_PORT   UART_NUM_0 // MUX_UART_NUM: the only UART the board uses
#define TX_RING_MAX     (64 * 1024)
#define TX_HW_FIFO      128        // what the chip buffers with no driver ring
#define TX_CHUNK        32         // bytes per paced write (~2.8 ms at 115200)

static int s_master = -1;
static char s_pty_path[128];
static char s_link[1024]; // symlink we created, "" if none
static atomic_bool s_host_attached;

static struct {
    pthread_mutex_t mu;
    pthread_cond_t changed;
    uint8_t buf[TX_RING_MAX];
    size_t cap; // driver ring size; the storage is always TX_RING_MAX
    size_t head, len;
} s_tx = {
    .mu = PTHREAD_MUTEX_INITIALIZER,
    .changed = PTHREAD_COND_INITIALIZER,
    .cap = 4096,
};

static bool s_installed;
static int s_baud = 115200;
static int s_bits_per_byte = 10; // 8N1

// ---------------------------------------------------------------- wire

static void apply_noise(uint8_t *p, size_t n)
{
    if (g_sim.noise <= 0) {
        return;
    }
    for (size_t i = 0; i < n; i++) {
        if (sim_rand() / 4294967296.0 < g_sim.noise) {
            p[i] ^= (uint8_t)(1u << (sim_rand() % 8));
        }
    }
}

// macOS resets a pty to cooked mode (with echo) whenever a process opens
// it, so raw mode is re-applied on every attach before anything is sent:
// echo would bounce the firmware's own frames back into its RX.
static void make_raw(void)
{
    int fd = open(s_pty_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        return;
    }
    struct termios tio;
    if (tcgetattr(fd, &tio) == 0) {
        cfmakeraw(&tio);
        tcsetattr(fd, TCSANOW, &tio);
    }
    close(fd);
}

// With no process holding the slave end, the master reports POLLHUP and
// writes fail with EIO: that's "no host attached".
static bool host_hung_up(void)
{
    struct pollfd p = { .fd = s_master, .events = POLLOUT };
    return poll(&p, 1, 0) >= 0 && (p.revents & POLLHUP);
}

static void set_attached(bool on)
{
    if (atomic_exchange(&s_host_attached, on) != on) {
        sim_printf("host %s %s", on ? "attached to" : "detached from",
                   s_link[0] ? s_link : s_pty_path);
        sim_display_request_title_update();
    }
}

static void wire_write(uint8_t *p, size_t n)
{
    apply_noise(p, n);
    while (n > 0) {
        if (!atomic_load(&s_host_attached)) {
            if (host_hung_up()) {
                return; // nobody listening: the bytes are lost on the wire
            }
            make_raw();
            set_attached(true);
        }
        ssize_t w = write(s_master, p, n);
        if (w > 0) {
            p += w;
            n -= (size_t)w;
            continue;
        }
        if (w < 0 && errno == EINTR) {
            continue;
        }
        if (w < 0 && errno == EAGAIN) {
            // Host attached but behind on reading (the pty holds ~1 KB).
            struct pollfd pfd = { .fd = s_master, .events = POLLOUT };
            poll(&pfd, 1, 50);
            continue;
        }
        set_attached(false); // EIO: the host closed the port
        return;
    }
}

static void sleep_until_us(int64_t t)
{
    for (;;) {
        int64_t left = t - esp_timer_get_time();
        if (left <= 0) {
            return;
        }
        struct timespec ts = { .tv_sec = (time_t)(left / 1000000), .tv_nsec = (long)(left % 1000000) * 1000 };
        nanosleep(&ts, NULL);
    }
}

static void *tx_thread(void *arg)
{
    (void)arg;
#ifdef __APPLE__
    pthread_setname_np("uart_tx");
#endif
    uint8_t chunk[TX_CHUNK];
    int64_t next_us = 0;
    for (;;) {
        pthread_mutex_lock(&s_tx.mu);
        while (s_tx.len == 0) {
            pthread_cond_wait(&s_tx.changed, &s_tx.mu);
        }
        size_t n = s_tx.len < TX_CHUNK ? s_tx.len : TX_CHUNK;
        for (size_t i = 0; i < n; i++) {
            chunk[i] = s_tx.buf[(s_tx.head + i) % TX_RING_MAX];
        }
        pthread_mutex_unlock(&s_tx.mu);

        if (!g_sim.fast_uart) {
            // Bytes leave the ring only once they'd have been clocked out
            // on the line, as with the UART's hardware FIFO.
            int64_t now = esp_timer_get_time();
            if (next_us < now - 20000) {
                next_us = now; // line was idle
            }
            next_us += (int64_t)n * s_bits_per_byte * 1000000 / s_baud;
            sleep_until_us(next_us);
        }
        wire_write(chunk, n);

        pthread_mutex_lock(&s_tx.mu);
        s_tx.head = (s_tx.head + n) % TX_RING_MAX;
        s_tx.len -= n;
        pthread_cond_broadcast(&s_tx.changed);
        pthread_mutex_unlock(&s_tx.mu);
    }
    return NULL;
}

// Blocks while the ring is full, like the IDF driver.
static void tx_push(const uint8_t *p, size_t size)
{
    pthread_mutex_lock(&s_tx.mu);
    while (size > 0) {
        while (s_tx.len >= s_tx.cap) {
            pthread_cond_wait(&s_tx.changed, &s_tx.mu);
        }
        size_t room = s_tx.cap - s_tx.len;
        size_t n = size < room ? size : room;
        for (size_t i = 0; i < n; i++) {
            s_tx.buf[(s_tx.head + s_tx.len + i) % TX_RING_MAX] = p[i];
        }
        s_tx.len += n;
        p += n;
        size -= n;
        pthread_cond_broadcast(&s_tx.changed);
    }
    pthread_mutex_unlock(&s_tx.mu);
}

// ---------------------------------------------------------------- sim API

void sim_uart_open(const char *link_path)
{
    const char *inherited = getenv(SIM_ENV_PTY);
    if (inherited) {
        // Simulated reset: the pty survived the re-exec, the host never
        // noticed. Format "<fd>:<slave path>".
        char *colon = strchr(inherited, ':');
        s_master = atoi(inherited);
        if (!colon || s_master < 0) {
            sim_die("bad %s=%s", SIM_ENV_PTY, inherited);
        }
        snprintf(s_pty_path, sizeof(s_pty_path), "%s", colon + 1);
    } else {
        int slave;
        if (openpty(&s_master, &slave, NULL, NULL, NULL) != 0) {
            sim_die("openpty: %s", strerror(errno));
        }
        snprintf(s_pty_path, sizeof(s_pty_path), "%s", ttyname(slave));
        // Nothing of ours holds the slave end, so a missing host shows up
        // as EIO/POLLHUP on the master.
        close(slave);
    }
    fcntl(s_master, F_SETFL, fcntl(s_master, F_GETFL) | O_NONBLOCK);
    fcntl(s_master, F_SETFD, 0); // keep it across the reset re-exec

    struct stat st;
    if (lstat(link_path, &st) == 0 && !S_ISLNK(st.st_mode)) {
        sim_die("%s exists and is not a symlink; pick another --tty path", link_path);
    }
    unlink(link_path);
    if (symlink(s_pty_path, link_path) != 0) {
        sim_die("symlink %s -> %s: %s", link_path, s_pty_path, strerror(errno));
    }
    snprintf(s_link, sizeof(s_link), "%s", link_path);

    pthread_t thread;
    if (pthread_create(&thread, NULL, tx_thread, NULL) != 0) {
        sim_die("uart tx thread");
    }
    pthread_detach(thread);
}

const char *sim_uart_pty_path(void)
{
    return s_pty_path;
}

int sim_uart_master_fd(void)
{
    return s_master;
}

bool sim_uart_host_attached(void)
{
    return atomic_load(&s_host_attached);
}

// Async-signal-safe. Leaves the link alone if another sim has taken it over.
void sim_uart_remove_link(void)
{
    if (!s_link[0]) {
        return;
    }
    char target[sizeof(s_pty_path)];
    ssize_t n = readlink(s_link, target, sizeof(target) - 1);
    if (n > 0) {
        target[n] = '\0';
        if (strcmp(target, s_pty_path) == 0) {
            unlink(s_link);
        }
    }
}

// What an ESP32 (rev3) ROM prints on UART0 at boot, before the firmware
// runs. The host's mux parser has to skip it, as with a real reset.
void sim_uart_boot_banner(bool after_reset)
{
    char banner[512];
    int n = snprintf(banner, sizeof(banner),
                     "ets Jul 29 2019 12:21:46\r\n\r\n"
                     "rst:%s,boot:0x13 (SPI_FAST_FLASH_BOOT)\r\n"
                     "configsip: 0, SPIWP:0xee\r\n"
                     "clk_drv:0x00,q_drv:0x00,d_drv:0x00,cs0_drv:0x00,hd_drv:0x00,wp_drv:0x00\r\n"
                     "mode:DIO, clock div:2\r\n"
                     "load:0x3fff0030,len:6276\r\n"
                     "load:0x40078000,len:15716\r\n"
                     "load:0x40080400,len:4\r\n"
                     "entry 0x4008063c\r\n",
                     after_reset ? "0xc (SW_CPU_RESET)" : "0x1 (POWERON_RESET)");
    tx_push((const uint8_t *)banner, (size_t)n);
}

// ---------------------------------------------------------------- IDF API

esp_err_t uart_driver_install(uart_port_t uart_num, int rx_buffer_size, int tx_buffer_size,
                              int queue_size, QueueHandle_t *uart_queue, int intr_alloc_flags)
{
    (void)rx_buffer_size; // the pty buffers RX
    (void)queue_size;
    (void)intr_alloc_flags;
    if (uart_num != SIM_UART_PORT || s_master < 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_installed) {
        return ESP_FAIL; // same as IDF: already installed
    }
    if (uart_queue) {
        *uart_queue = NULL; // event queue not simulated
    }
    pthread_mutex_lock(&s_tx.mu);
    s_tx.cap = tx_buffer_size > 0 ? (size_t)tx_buffer_size : TX_HW_FIFO;
    if (s_tx.cap > TX_RING_MAX) {
        s_tx.cap = TX_RING_MAX;
    }
    pthread_mutex_unlock(&s_tx.mu);
    s_installed = true;
    return ESP_OK;
}

esp_err_t uart_param_config(uart_port_t uart_num, const uart_config_t *cfg)
{
    if (uart_num != SIM_UART_PORT || !cfg || cfg->baud_rate <= 0) {
        return ESP_ERR_INVALID_ARG;
    }
    int stop = cfg->stop_bits == UART_STOP_BITS_1 ? 1 : 2; // 1.5 rounds up
    s_bits_per_byte = 1 + (5 + (int)cfg->data_bits) +
                      (cfg->parity != UART_PARITY_DISABLE ? 1 : 0) + stop;
    s_baud = cfg->baud_rate;
    return ESP_OK;
}

esp_err_t uart_set_pin(uart_port_t uart_num, int tx_io_num, int rx_io_num, int rts_io_num,
                       int cts_io_num)
{
    (void)tx_io_num;
    (void)rx_io_num;
    (void)rts_io_num;
    (void)cts_io_num;
    return uart_num == SIM_UART_PORT ? ESP_OK : ESP_ERR_INVALID_ARG;
}

int uart_write_bytes(uart_port_t uart_num, const void *src, size_t size)
{
    if (uart_num != SIM_UART_PORT || !s_installed || !src) {
        return -1;
    }
    tx_push(src, size);
    return (int)size;
}

esp_err_t uart_get_tx_buffer_free_size(uart_port_t uart_num, size_t *size)
{
    if (uart_num != SIM_UART_PORT || !s_installed || !size) {
        return ESP_ERR_INVALID_ARG;
    }
    pthread_mutex_lock(&s_tx.mu);
    *size = s_tx.cap - s_tx.len;
    pthread_mutex_unlock(&s_tx.mu);
    return ESP_OK;
}

// Same contract as the IDF driver: collect until `length` bytes are in, or
// until the line has been quiet for ticks_to_wait.
int uart_read_bytes(uart_port_t uart_num, void *buf, uint32_t length, TickType_t ticks_to_wait)
{
    if (uart_num != SIM_UART_PORT || !s_installed || !buf) {
        return -1;
    }
    uint8_t *out = buf;
    uint32_t got = 0;
    int timeout_ms = ticks_to_wait == portMAX_DELAY ? -1 : (int)pdTICKS_TO_MS(ticks_to_wait);
    while (got < length) {
        struct pollfd p = { .fd = s_master, .events = POLLIN };
        int rc = poll(&p, 1, timeout_ms);
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc <= 0) {
            break; // quiet for the whole timeout
        }
        ssize_t n = read(s_master, out + got, length - got);
        if (n > 0) {
            apply_noise(out + got, (size_t)n);
            got += (uint32_t)n;
            continue;
        }
        if (n < 0 && (errno == EAGAIN || errno == EINTR)) {
            continue;
        }
        // EOF/EIO: no host has the port open. Behave like a silent line.
        set_attached(false);
        if (ticks_to_wait == portMAX_DELAY) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        vTaskDelay(ticks_to_wait);
        break;
    }
    return (int)got;
}

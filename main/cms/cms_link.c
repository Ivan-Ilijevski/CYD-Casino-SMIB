#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

// lwip's socket API is POSIX, so this file is the same on the board and in the
// desktop simulator; only the header location differs.
#ifdef ESP_PLATFORM
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include "mux/mux.h"         // MUX_CH_CMS
#include "mux/mux_frame.h"
#include "cms_link.h"

static const char *TAG = "cms_link";

#define LINK_ACC_SIZE       MUX_ACC_SIZE_FOR(MUX_MAX_PAYLOAD)
#define LINK_RX_CHUNK       512
#define LINK_RECV_TIMEOUT_MS 200        // also the reconnect-request latency
#define LINK_CONNECT_TIMEOUT_MS 5000
#define LINK_BACKOFF_MIN_MS 500
#define LINK_BACKOFF_MAX_MS 8000
#define LINK_TX_MUTEX_TIMEOUT pdMS_TO_TICKS(500)

// Guards s_fd, the endpoint and the TX staging buffer. The link task is the
// only thread that creates or closes s_fd, and it only ever does so while
// holding this, so a send can never write into a reused descriptor.
static SemaphoreHandle_t s_lock;
static int s_fd = -1;
static bool s_connected;

static char s_host[CMS_HOST_MAX];
static uint16_t s_port;
static uint32_t s_endpoint_gen;          // bumped on every endpoint change

static mux_rx_cb_t s_rx_cb;
static void *s_rx_ctx;

static uint8_t s_acc[LINK_ACC_SIZE];
static mux_parser_t s_parser;
// Staging for one outgoing frame. Static rather than on the stack: a
// maximum-size CMS payload is 2 KB, which the app task's stack should not
// have to carry. Guarded by s_lock like the descriptor.
static uint8_t s_tx[MUX_OVERHEAD + MUX_MAX_PAYLOAD];

bool cms_link_connected(void)
{
    return s_connected;
}

esp_err_t cms_link_register_rx(mux_rx_cb_t cb, void *ctx)
{
    s_rx_cb = cb;
    s_rx_ctx = ctx;
    return ESP_OK;
}

void cms_link_set_endpoint(const char *host, uint16_t port)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    snprintf(s_host, sizeof(s_host), "%s", host ? host : "");
    s_port = port;
    s_endpoint_gen++;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "endpoint set to %s:%u", host ? host : "(none)", (unsigned)port);
}

void cms_link_get_endpoint(char *host_out, size_t host_out_size, uint16_t *port_out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (host_out && host_out_size > 0) {
        snprintf(host_out, host_out_size, "%s", s_host);
    }
    if (port_out) {
        *port_out = s_port;
    }
    xSemaphoreGive(s_lock);
}

// ---------------------------------------------------------------- TX

esp_err_t cms_link_send(const uint8_t *payload, uint16_t len)
{
    if (len > MUX_MAX_PAYLOAD) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (xSemaphoreTake(s_lock, LINK_TX_MUTEX_TIMEOUT) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_fd >= 0) {
        size_t total = mux_frame_write(s_tx, MUX_CH_CMS, payload, len);
        size_t sent = 0;
        err = ESP_OK;
        while (sent < total) {
            ssize_t n = send(s_fd, s_tx + sent, total - sent, 0);
            if (n > 0) {
                sent += (size_t)n;
                continue;
            }
            if (n < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
                continue;
            }
            // A half-written frame is unparsable at the far end, but the far
            // end is gone: the reconnect resets both parsers, and cms.c's
            // request timeout resolves whatever was in flight.
            ESP_LOGW(TAG, "send failed after %u/%u bytes: errno %d",
                     (unsigned)sent, (unsigned)total, errno);
            err = ESP_FAIL;
            break;
        }
    }
    xSemaphoreGive(s_lock);
    return err;
}

// ---------------------------------------------------------------- RX task

static void frame_cb(uint8_t chan, const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)ctx;
    if (chan == MUX_CH_CMS && s_rx_cb) {
        s_rx_cb(payload, len, s_rx_ctx);
    }
}

static bool chan_valid(uint8_t chan)
{
    return chan == MUX_CH_CMS; // this leg carries nothing else
}

static void link_close(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_fd >= 0) {
        close(s_fd);
        s_fd = -1;
    }
    s_connected = false;
    xSemaphoreGive(s_lock);
}

// Connects with a bounded timeout: a blocking connect() to an unreachable host
// takes the full TCP SYN retry budget (tens of seconds), which would make a
// mistyped address in the admin panel look like a hang rather than a failure.
static int connect_with_timeout(const struct addrinfo *ai, int timeout_ms)
{
    int fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
    if (fd < 0) {
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
    if (rc != 0 && errno == EINPROGRESS) {
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(fd, &wset);
        struct timeval tv = {
            .tv_sec = timeout_ms / 1000,
            .tv_usec = (timeout_ms % 1000) * 1000,
        };
        rc = select(fd + 1, NULL, &wset, NULL, &tv);
        if (rc > 0) {
            int soerr = 0;
            socklen_t slen = sizeof(soerr);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &slen) != 0 || soerr != 0) {
                rc = -1;
                errno = soerr ? soerr : errno;
            } else {
                rc = 0;
            }
        } else {
            rc = -1;
            if (errno == 0) {
                errno = ETIMEDOUT;
            }
        }
    }
    if (rc != 0) {
        close(fd);
        return -1;
    }

    fcntl(fd, F_SETFL, flags); // back to blocking, bounded by SO_RCVTIMEO
    struct timeval rcvto = {
        .tv_sec = LINK_RECV_TIMEOUT_MS / 1000,
        .tv_usec = (LINK_RECV_TIMEOUT_MS % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvto, sizeof(rcvto));
    // Requests are small and latency-sensitive (cms.c times out after 3 s), so
    // never let Nagle hold one back waiting for more bytes.
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    return fd;
}

// Resolves and connects. Returns the generation the connection was made for,
// so the task can tell when the endpoint changed under it.
static bool link_connect(const char *host, uint16_t port)
{
    char portstr[8];
    snprintf(portstr, sizeof(portstr), "%u", (unsigned)port);
    struct addrinfo hints = {
        .ai_family = AF_INET,       // lwip here is IPv4-only in this build
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res = NULL;
    int rc = getaddrinfo(host, portstr, &hints, &res);
    if (rc != 0 || !res) {
        ESP_LOGW(TAG, "cannot resolve '%s' (rc %d)", host, rc);
        return false;
    }

    int fd = -1;
    for (struct addrinfo *ai = res; ai && fd < 0; ai = ai->ai_next) {
        fd = connect_with_timeout(ai, LINK_CONNECT_TIMEOUT_MS);
    }
    freeaddrinfo(res);
    if (fd < 0) {
        ESP_LOGW(TAG, "connect to %s:%u failed: errno %d", host, (unsigned)port, errno);
        return false;
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_fd = fd;
    s_connected = true;
    xSemaphoreGive(s_lock);
    mux_parser_reset(&s_parser);
    ESP_LOGI(TAG, "connected to %s:%u", host, (unsigned)port);
    return true;
}

static void cms_link_task(void *arg)
{
    (void)arg;
    uint8_t chunk[LINK_RX_CHUNK];
    int backoff_ms = LINK_BACKOFF_MIN_MS;

    while (1) {
        char host[CMS_HOST_MAX];
        uint16_t port;
        uint32_t gen;
        xSemaphoreTake(s_lock, portMAX_DELAY);
        snprintf(host, sizeof(host), "%s", s_host);
        port = s_port;
        gen = s_endpoint_gen;
        xSemaphoreGive(s_lock);

        if (host[0] == '\0' || port == 0) {
            vTaskDelay(pdMS_TO_TICKS(LINK_RECV_TIMEOUT_MS));
            continue;
        }

        if (!link_connect(host, port)) {
            // Don't sit out a backoff for an endpoint nobody wants any more.
            for (int waited = 0; waited < backoff_ms; waited += LINK_RECV_TIMEOUT_MS) {
                vTaskDelay(pdMS_TO_TICKS(LINK_RECV_TIMEOUT_MS));
                xSemaphoreTake(s_lock, portMAX_DELAY);
                bool changed = (s_endpoint_gen != gen);
                xSemaphoreGive(s_lock);
                if (changed) {
                    break;
                }
            }
            backoff_ms = backoff_ms * 2 > LINK_BACKOFF_MAX_MS
                             ? LINK_BACKOFF_MAX_MS
                             : backoff_ms * 2;
            continue;
        }
        backoff_ms = LINK_BACKOFF_MIN_MS;

        // Receive until the peer goes away or the endpoint changes. recv's
        // timeout is what makes an endpoint change take effect without another
        // thread having to touch this socket.
        while (1) {
            ssize_t n = recv(s_fd, chunk, sizeof(chunk), 0);
            if (n > 0) {
                mux_parser_feed(&s_parser, chunk, (size_t)n);
            } else if (n == 0) {
                ESP_LOGI(TAG, "CMS closed the connection");
                break;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR &&
                       errno != ETIMEDOUT) {
                ESP_LOGW(TAG, "recv failed: errno %d", errno);
                break;
            }
            xSemaphoreTake(s_lock, portMAX_DELAY);
            bool changed = (s_endpoint_gen != gen);
            xSemaphoreGive(s_lock);
            if (changed) {
                ESP_LOGI(TAG, "endpoint changed, dropping the connection");
                break;
            }
        }
        link_close();
    }
}

esp_err_t cms_link_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        return ESP_ERR_NO_MEM;
    }
    mux_parser_init(&s_parser, s_acc, sizeof(s_acc),
                    0 /* TCP never loses a partial frame; reconnect resets it */,
                    chan_valid, frame_cb, NULL);

    BaseType_t ok = xTaskCreate(cms_link_task, "cms_link", 5120, NULL, 9, NULL);
    return ok == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}

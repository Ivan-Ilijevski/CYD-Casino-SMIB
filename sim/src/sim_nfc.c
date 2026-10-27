// RC522 stand-in: keys (or ctl commands) put a card on the reader. Posts the
// same APP_EVT_CARD_PRESENT/REMOVED events, with the same UID string format
// ("9C 76 5A F4"), as main/drivers/nfc.c.

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "app/app_events.h"
#include "drivers/nfc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"
#include "sim.h"

static const char *TAG = "nfc";

// The RC522 library polls the field every 120 ms, so a card at the edge of
// range comes and goes on that grid.
#define RC522_POLL_US (120 * 1000)

static const struct {
    const char *uid;
    const char *label;
} k_cards[] = {
    { "9C 76 5A F4", "player card" },
    { "DE AD BE EF", "unknown card" },
    { "04 A2 3B 1C", "2nd player card" },
};
#define CARD_COUNT ((int)(sizeof(k_cards) / sizeof(k_cards[0])))

// Who is holding a card to the reader. One card at a time, like the real one.
static enum { SRC_NONE, SRC_KEY, SRC_FLAKY, SRC_CTL } s_src;
static int s_key_card;
static char s_uid[32];
static bool s_present; // what the app was last told
static int64_t s_flaky_next_us;
static char s_status[64];

static void post_present(void)
{
    app_event_t e = { .type = APP_EVT_CARD_PRESENT };
    snprintf(e.u.uid, sizeof(e.u.uid), "%s", s_uid);
    ESP_LOGI(TAG, "Card detected, UID: %s", e.u.uid);
    app_post_event(&e);
    s_present = true;
}

static void post_removed(void)
{
    if (!s_present) {
        return;
    }
    app_event_t e = { .type = APP_EVT_CARD_REMOVED };
    ESP_LOGI(TAG, "Card has been removed");
    app_post_event(&e);
    s_present = false;
}

static bool reader_busy(void)
{
    if (s_src == SRC_NONE) {
        return false;
    }
    sim_printf("the reader already has a card (%s)", s_uid);
    return true;
}

static void place(int src, const char *uid, const char *label)
{
    s_src = src;
    snprintf(s_uid, sizeof(s_uid), "%s", uid);
    post_present();
    sim_printf("card on the reader: %s (%s)", uid, label);
    sim_display_request_title_update();
}

static void take_off(void)
{
    s_src = SRC_NONE;
    post_removed();
    sim_printf("card off the reader");
    sim_display_request_title_update();
}

static void flaky_cb(lv_timer_t *timer)
{
    (void)timer;
    int64_t now = esp_timer_get_time();
    if (s_src != SRC_FLAKY || now < s_flaky_next_us) {
        return;
    }
    int polls;
    if (s_present) {
        post_removed();
        polls = 1 + (int)(sim_rand() % 2); // missed for 1-2 polls
    } else {
        post_present();
        polls = 1 + (int)(sim_rand() % 4); // read for 1-4 polls
    }
    s_flaky_next_us = now + (int64_t)polls * RC522_POLL_US;
}

// "9C 76 5A F4", "9c765af4" or "9C:76:5A:F4" -> "9C 76 5A F4" (1-10 bytes).
static bool normalize_uid(const char *in, char *out, size_t size)
{
    size_t n = 0;
    int nibbles = 0;
    for (; *in; in++) {
        if (*in == ' ' || *in == ':') {
            continue;
        }
        if (!isxdigit((unsigned char)*in) || n + 3 > size) {
            return false;
        }
        if (nibbles > 0 && nibbles % 2 == 0) {
            out[n++] = ' ';
        }
        out[n++] = (char)toupper((unsigned char)*in);
        nibbles++;
    }
    out[n] = '\0';
    return nibbles >= 2 && nibbles % 2 == 0 && nibbles <= 20;
}

// ---------------------------------------------------------------- sim API

void sim_nfc_key(int card, bool down)
{
    if (card < 0 || card >= CARD_COUNT) {
        return;
    }
    if (down) {
        if (!reader_busy()) {
            s_key_card = card;
            place(SRC_KEY, k_cards[card].uid, k_cards[card].label);
        }
    } else if (s_src == SRC_KEY && s_key_card == card) {
        take_off();
    }
}

void sim_nfc_flaky(bool down)
{
    if (down) {
        if (!reader_busy()) {
            place(SRC_FLAKY, k_cards[0].uid, "player card, flaky");
            s_flaky_next_us = esp_timer_get_time() + (1 + (int)(sim_rand() % 4)) * RC522_POLL_US;
        }
    } else if (s_src == SRC_FLAKY) {
        take_off();
    }
}

bool sim_nfc_command(const char *arg)
{
    if (strcmp(arg, "off") == 0) {
        if (s_src != SRC_NONE) {
            take_off();
        }
        return true;
    }
    if (strcmp(arg, "flaky") == 0) {
        sim_nfc_flaky(true);
        return s_src == SRC_FLAKY;
    }
    if (arg[0] >= '1' && arg[0] < '1' + CARD_COUNT && arg[1] == '\0') {
        int i = arg[0] - '1';
        if (reader_busy()) {
            return false;
        }
        place(SRC_CTL, k_cards[i].uid, k_cards[i].label);
        return true;
    }
    char uid[sizeof(s_uid)];
    if (!normalize_uid(arg, uid, sizeof(uid))) {
        sim_printf("bad card UID '%s' (want hex bytes, e.g. 9C 76 5A F4)", arg);
        return false;
    }
    if (reader_busy()) {
        return false;
    }
    place(SRC_CTL, uid, "custom");
    return true;
}

void sim_nfc_release_all(void)
{
    if (s_src == SRC_KEY || s_src == SRC_FLAKY) {
        take_off();
    }
}

const char *sim_nfc_status(void)
{
    if (s_src == SRC_NONE) {
        return "no card";
    }
    snprintf(s_status, sizeof(s_status), "%scard %s", s_src == SRC_FLAKY ? "flaky " : "", s_uid);
    return s_status;
}

// ---------------------------------------------------------------- driver API

void nfc_init(void)
{
    lv_timer_create(flaky_cb, 20, NULL);
    ESP_LOGI(TAG, "RC522 initialized (sim: keys 1-3 hold a card to the reader)");
}

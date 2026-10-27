// CMS client: JSON request/response over mux channel 0x02, relayed by the
// slot-machine host app to the CMS backend.
//
// All requests are async; results arrive as APP_EVT_CMS events (including
// timeouts, which surface as ok=false with err_code "timeout").
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "cms_types.h"

esp_err_t cms_init(void);

// Periodic work: keepalive ping, request timeout scan, link up/down edges.
// Call from the app task every loop pass.
void cms_tick(void);

bool cms_link_up(void);

esp_err_t cms_auth(const char *uid);
esp_err_t cms_logout(const char *sid, const char *reason); // fire-and-forget
esp_err_t cms_debit(const char *sid, uint64_t cents, char txn_out[24]);
esp_err_t cms_commit(const char *txn);
esp_err_t cms_rollback(const char *txn, const char *reason);

// Generates a fresh transaction id (app task only, like cms_debit).
void cms_make_txn(char out[24]);
// Cashout deposit: credits cents onto the card. Idempotent by txn — safe to
// re-send until CMS_EVT_CREDIT_RES confirms.
esp_err_t cms_credit(const char *sid, uint64_t cents, const char *txn);
esp_err_t cms_fetch_menu(void);
esp_err_t cms_order(const char *sid, const cms_order_item_t *items, size_t n,
                    bool pay_points);

// Copies the most recent menu_res (mutex-protected).
esp_err_t cms_get_menu(cms_menu_t *out);

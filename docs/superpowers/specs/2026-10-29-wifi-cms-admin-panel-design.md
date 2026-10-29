# WiFi CMS transport + door-gated admin panel

Date: 2026-10-29
Status: approved

## Problem

The CMS channel rides the UART0 mux today, so every terminal needs a host PC on
the slot machine running a splitter that relays channel `0x02` to the network.
That makes the PC a required part of the money path and a single point of
failure for a terminal that is otherwise self-contained.

Move the CMS channel onto WiFi, and give staff a way to set the network up from
the terminal itself — behind the cabinet door and a passcode, since the panel
can redirect where money messages go.

## Decisions

| Decision | Choice | Why |
|---|---|---|
| CMS transport | TCP carrying the existing mux frame | `tools/host_sim/cms_tcp_server.py` already speaks this wire; `cms.c`'s parser, pending table and money chains need no change |
| Serial CMS fallback | None — WiFi only | One transport means one set of failure modes; a transport that can switch mid-commit violates the funds-transfer rules |
| Admin entry | Cabinet door switch on a spare GPIO | Needs the cabinet key, so it is staff-only by construction; no player-visible affordance |
| Door pin | IO32 (E32R40T) / IO46 (ES3C35P) | Both free and header-accessible; both support an internal pull-up, so a bare microswitch needs no external parts |
| Banner scope | Top layer, over every screen | Reuses the offline-badge pattern; staff reach the panel from any state and an open cabinet is never invisible |
| `.env` scope | Passcode + WiFi + CMS endpoint defaults | A freshly flashed board joins the network without anyone typing on a 480x320 resistive panel |

## Architecture

```
UART0 mux:   SAS 0x01  +  LOG 0x7F          (unchanged)
WiFi TCP:    CMS JSON, mux-framed           (new)

main/cms/cms.c          unchanged JSON logic, pending table, money chains
   └─ cms_link.c        NEW  socket + RX task; frames with the mux format
                             └─ TCP -> cms_tcp_server.py :9020

main/mux/mux_frame.c    NEW  framing + incremental parser, extracted from mux.c
   ├─ mux.c                  UART0 leg (SAS + LOG)
   └─ cms_link.c             TCP leg (CMS)
```

`mux_frame.c` exists so the sync/length/CRC/resync logic has one
implementation instead of two that drift. The parser takes caller-provided
storage, so the two legs size their accumulators independently.

### Module boundaries

`main/drivers/` is the desktop simulator's exclusion boundary, so
platform-backed modules live there and the sim supplies its own stand-ins.

| Module | Purpose | Firmware | Sim |
|---|---|---|---|
| `drivers/net.h` | STA connect, scan, IP/RSSI | `net_wifi.c` | `sim_net.c` (fake APs, `wifi up/down` ctl) |
| `drivers/settings.h` | Persisted WiFi + CMS endpoint | `settings_nvs.c` | `sim_settings.c` (file beside the binary) |
| `drivers/door.h` | Debounced door-switch read | `door.c` | `sim_door.c` (`D` key, `door open/closed` ctl) |

`cms_link.c` is deliberately *not* split: lwip's socket API is POSIX, so one
file serves both behind a single conditional include, and the sim's FreeRTOS
shim already provides every primitive it uses (`xTaskCreate`, `vTaskDelay`,
mutexes).

### Contracts

```c
// drivers/net.h
esp_err_t   net_init(void);
esp_err_t   net_connect(const char *ssid, const char *pass);  // async
net_state_t net_state(void);                 // DOWN | CONNECTING | UP
bool        net_ip(char *out, size_t n);
int         net_rssi(void);
esp_err_t   net_scan_start(void);
size_t      net_scan_results(net_ap_t *out, size_t max);

// drivers/settings.h
void        settings_load(smib_settings_t *out);   // NVS, else .env defaults
esp_err_t   settings_save(const smib_settings_t *s);

// drivers/door.h
esp_err_t   door_init(void);
bool        door_is_open(void);              // debounced; polled from app_tick

// cms/cms_link.h
esp_err_t   cms_link_init(const char *host, uint16_t port);
esp_err_t   cms_link_send(const uint8_t *payload, uint16_t len);
esp_err_t   cms_link_register_rx(mux_rx_cb_t cb, void *ctx);  // same signature as mux
void        cms_link_set_endpoint(const char *host, uint16_t port);  // reconnects
bool        cms_link_connected(void);
```

`cms_link_register_rx` keeps the `mux_rx_cb_t` signature so `cms.c`'s existing
`cms_rx_cb` is reused verbatim.

## Door switch

Each board header gains `PIN_NUM_DOOR`. Configured as input with the internal
pull-up on; the microswitch shorts to GND when the door is shut.

```
IO32/IO46 --[ door microswitch ]-- GND

door SHUT  -> switch closed -> LOW
door OPEN  -> released      -> HIGH
wire cut   -> HIGH = "open"
```

A cut wire reads as "door open" on purpose. The passcode is the actual security
boundary, so failing that way keeps the terminal administrable and shows a loud
permanent banner, whereas failing to "shut" would make the panel unreachable
without a reflash.

Polled with debounce from `app_tick()` — no extra task, and the edge is handled
on the LVGL thread where the UI call is legal.

Door-open is **not** reported to the machine as a SAS exception. That is a
protocol change nobody asked for.

## Admin panel

A top-layer modal, following `ui_transfer`'s shell (`ui_modal_create`).

1. **Passcode keypad** — masked entry. Three wrong codes -> 30 s lockout.
2. **Panel**
   - *Status*: WiFi state, IP, RSSI, CMS link, firmware version, device id
   - *WiFi*: SSID from a scan dropdown or typed manually; password
   - *CMS*: host — dotted IP **or** hostname — and port, validated 1-65535
   - *Actions*: Save and reconnect / Rescan / Reboot / Close

Saving the endpoint reconnects the socket in place; no reboot needed.

The keyboard is styled with `lv_font_montserrat_14`, not `Sf_Compact_14`: that
font was generated with `--range 32-127,1024-1119` and has no `LV_SYMBOL`
glyphs, so its backspace key would draw as tofu.

Closing the cabinet door closes the panel.

### Safety rule

Saving network settings is **refused while a money chain is in flight**
(`s_xfer != XFER_NONE`), with a toast saying so. Dropping the CMS socket during
a commit or a cashout credit is exactly what the funds-transfer rules forbid.
Status remains readable throughout.

## Configuration

`.env` at the project root, gitignored, with `.env.example` committed:

```
SMIB_ADMIN_PASSCODE=4271
SMIB_WIFI_SSID=club-ap
SMIB_WIFI_PASS=...
SMIB_CMS_HOST=192.168.1.50
SMIB_CMS_PORT=9020
```

Parsed by `cmake/smib_env.cmake`, included by both `main/CMakeLists.txt` and
`sim/CMakeLists.txt` so firmware and simulator agree on the passcode. Values
become compile definitions and act as factory defaults; NVS wins once staff
save from the panel. A missing `.env` produces a CMake warning and harmless
defaults, so a fresh clone still builds.

## Error handling

- **Socket down**: `cms_link` reconnects with backoff. `cms_tick`'s existing
  ping/pong liveness still drives the link edges, and a dropped socket forces
  the link down immediately so the offline badge is prompt.
- **WiFi down**: same path — the CMS link reports down, the offline badge shows,
  and auth is blocked, exactly as a dead serial CMS behaves today.
- **Money chain across an outage**: unchanged. `XFER_COMMIT` and `XFER_CREDIT`
  retry until the CMS confirms and are never rolled back.
- **Bad endpoint typed in**: rejected at entry (port range, non-empty host);
  a host that does not resolve surfaces as a down link, recoverable from the
  panel.

## Testing

- `cms_tcp_server.py` becomes the CMS peer, unchanged, with its existing
  `--fail-auth` / `--fail-debit` / `--drop-commit` / `--drop-credit` flags.
- `host_sim.py` keeps SAS + logs on the serial leg and warns if a CMS frame
  arrives (it should no longer see one).
- The desktop simulator exercises the panel end to end through `sim_net.c`,
  `sim_settings.c` and `sim_door.c`, including a door-open banner and a failed
  WiFi connect.
- `selftest.py` gains a case for `mux_frame`'s parser over a TCP-shaped byte
  stream.
- Both IDF targets and the sim must build.

## Known risk

The E32R40T is a plain ESP32 with no PSRAM, and WiFi wants roughly 50 KB of
SRAM on top of LVGL's buffers. If it does not fit, trim the WiFi buffer counts
in `sdkconfig.defaults` (static RX 4, dynamic TX 16) rather than shrink LVGL.
The S3 has 8 MB of PSRAM and is unaffected. Report measured free heap for both.

## Out of scope

- Reporting door-open to the machine over SAS
- Keeping the serial CMS channel as a fallback transport
- TLS on the CMS socket (the terminal and CMS share a club LAN; revisit if that
  stops being true)
- Over-the-air firmware update, even though WiFi now makes it possible

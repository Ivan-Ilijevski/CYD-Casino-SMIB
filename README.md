# SMIB — Slot Machine Interface Board

Firmware for the **4.0" ESP32-32E CYD** (model E32R40T) or the **3.5" ESP32-S3
display** (LCDWiki ES3C35P) that acts as a cashless
payment terminal attached to a slot machine. The device authenticates a player
via NFC card, talks **SAS** (Slot Accounting System) to the machine over the USB
serial link (UART0) and JSON to the **CMS** (Cash Management System) over
**Wi-Fi**.

The two links carry the same frame format but different wires:

| Link | Transport | Channels |
|---|---|---|
| Slot machine | UART0, 115200 baud | SAS `0x01`, firmware logs `0x7F` |
| CMS | TCP socket over Wi-Fi | CMS JSON `0x02` |

The CMS used to ride the serial mux too, which meant every terminal needed a
host app on the slot machine PC relaying channel `0x02` to the network. It
doesn't any more: the terminal reaches the CMS itself, and staff configure the
network from an on-screen **admin panel** behind the cabinet door switch and a
passcode (see "Admin panel" below).

## Hardware

Two boards are supported; the board follows the IDF target (Kconfig
"SMIB board", `main/Kconfig.projbuild`). Pins live in `main/boards/`, and the
display/audio drivers are picked per board in `main/CMakeLists.txt`.

### E32R40T (esp32)

| Peripheral | Interface | Key pins |
|---|---|---|
| ST7796 LCD (480×320) | SPI2 (HSPI) | SCK=14, MOSI=13, MISO=12, CS=15, DC=2, BL=27 |
| XPT2046 touch | SPI2 (shared) | CS=33, IRQ=36 |
| RC522 NFC reader | SPI3 (VSPI) | SCK=18, MOSI=23, MISO=19, CS=21, RST=25 |
| Speaker amp | DAC / GPIO | EN=4 (active-low), DAC=26 |
| Cabinet door switch | GPIO | IO32 (I2C connector SDA pin, internal pull-up) |
| Slot machine (SAS) | UART0 | RX/TX via CH340, 115200 baud |

The LCD and touch share SPI2 at 40 MHz and 1 MHz respectively. The RC522 is on
its own SPI3 bus (IO21 is the board's documented external-SPI CS pin) to avoid
contention with the display DMA feed.

### ES3C35P (esp32s3)

| Peripheral | Interface | Key pins |
|---|---|---|
| ST77922 LCD (320×480, used as 480×320) | QSPI on SPI2 | SCK=12, D0=11, D1=13, D2=14, D3=9, CS=10, BL=41 |
| ST77922 integrated touch | I2C 0x55 | SDA=38, SCL=39, RST=48 |
| ES8311 codec + amp | I2S + I2C 0x18 | MCLK=17, BCLK=18, LRCK=21, DOUT=15, amp EN=1 (active-low) |
| RC522 NFC reader | SPI3 | microSD pins: SCK=5, MOSI=4, MISO=6, CS=3, RST=2 |
| Cabinet door switch | GPIO | IO46 (free-GPIO header, internal pull-up) |
| Slot machine (SAS) | UART0 | RX/TX via CH340 (USB-C), 115200 baud |

The panel can't swap axes, so LVGL renders landscape and the driver rotates each
strip in software; flip `UI_ROTATION` in `drivers/display_st77922.c` if the UI
comes out upside down. The panel needs 4-pixel-aligned column windows (handled
by an LVGL rounder). The board only exposes IO45/46 (strapping pins) and the
shared I2C bus, so the RC522 sits on the microSD slot's pins (SD is unused).
Connect it through a microSD breakout adapter or the slot pads.

## Project structure

```
main/
├── smib_main.c          Entry point; boot sequence and main loop
├── board.h              Picks the board header (CONFIG_SMIB_BOARD_*)
├── boards/              Per-board pins and resolution
├── Kconfig.projbuild    "SMIB board" choice
├── idf_component.yml    Component Manager dependencies
├── app/
│   ├── app.c/.h         State machine; sole LVGL caller; event queue consumer
│   └── app_events.h     Event types shared across tasks (CMS, SAS, NFC, audio)
├── cms/
│   ├── cms.c/.h          CMS JSON client (auth, menu, orders, debit/commit/rollback)
│   └── cms_link.c/.h     TCP socket to the CMS; frames with the mux format
├── sas/                 SAS host engine (polls, exceptions, credits, AFT)
├── mux/
│   ├── crc16.c/.h       CRC-16/KERMIT (mux frames + SAS messages)
│   ├── mux_frame.c/.h   Frame format + incremental parser, shared by both legs
│   ├── mux.c/.h         UART0 multiplexer (SAS 0x01, logs 0x7F)
│   └── log_bridge.c/.h  Forwards ESP_LOG output upstream over the mux
├── drivers/
│   ├── display.h        Display + touch + LVGL bring-up (one .c per board)
│   ├── display_st7796.c   E32R40T: ST7796 + XPT2046
│   ├── display_st77922.c  ES3C35P: ST77922 QSPI + integrated touch, sw rotation
│   ├── audio.h          Speaker cues (one .c per board)
│   ├── audio_ledc.c       E32R40T: LEDC square wave into the amp
│   ├── audio_i2s.c        ES3C35P: generated tones via ES8311 over I2S
│   ├── i2c_bus.c/.h     ES3C35P shared I2C bus (touch + codec)
│   ├── net.h            Wi-Fi station: connect, scan, IP/RSSI
│   ├── net_wifi.c         esp_wifi implementation
│   ├── settings.h       Persisted Wi-Fi + CMS endpoint
│   ├── settings_nvs.c     NVS implementation, falling back to .env defaults
│   ├── door.h/.c        Cabinet door switch (gates the admin panel)
│   └── nfc.c/.h         RC522 init, card-present/removed events
├── ui/
│   ├── ui.c/.h          Screen router, overlays, door banner
│   ├── ui_main.c        Main/idle screen layout
│   ├── ui_idle.c        Idle state rendering
│   ├── ui_order.c       Order/purchase flow screen
│   ├── ui_transfer.c    AFT credit transfer screen
│   └── ui_admin.c       Passcode keypad + network admin panel
└── assets/              LVGL image descriptors and font glyphs
```

Everything in `drivers/` is platform-backed and is what the desktop simulator
replaces; nothing else in `main/` knows whether it is on a board or a Mac.

## Dependencies

Managed via the ESP-IDF Component Manager (`main/idf_component.yml`):

- `lvgl/lvgl ~9.5.0`
- `abobija/rc522 ^3.4.3`
- esp32 only: `espressif/esp_lcd_st7796`, `atanisoft/esp_lcd_touch_xpt2046`
- esp32s3 only: `espressif/esp_lcd_st77922`, `espressif/esp_codec_dev`

Wi-Fi, lwip and NVS are IDF built-ins, so the move to Wi-Fi added no new
managed component — only `PRIV_REQUIRES` entries in `main/CMakeLists.txt`.

Each target has its own lock file (`dependencies.lock.<target>`).

**LVGL is pinned on purpose.** Both targets share one `managed_components/`
directory, so an unpinned `'*'` let whichever target built last decide the
version for the other *and* for the desktop sim, which reads
`managed_components/` directly. That is how the esp32 lock once said 9.5.0
while the esp32s3 lock said 9.6.0~1, with a 9.6 install silently breaking the
sim build. Bump the pin deliberately, and rebuild all three.

## Build & flash

Requires **ESP-IDF v5.5+**.

First, create `.env` from the template — it carries the admin passcode and the
factory network defaults, and the build reads it:

```sh
cp .env.example .env    # then edit: passcode, Wi-Fi, CMS host and port
```

`.env` is gitignored. A missing one still builds (with a CMake warning and
passcode `0000`), so CI and fresh clones work, but never deploy that.

```sh
# E32R40T (esp32): the default build/ and sdkconfig
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash

# ES3C35P (esp32s3): separate build dir and sdkconfig so both coexist
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 set-target esp32s3   # once
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 build
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 -p /dev/cu.usbserial-XXXX flash
```

`sdkconfig.defaults` holds the shared settings (including the custom partition
table); `sdkconfig.defaults.<target>` adds the board choice and its flash size
(4 MB on the E32R40T, 16 MB plus octal PSRAM on the S3). Both builds share
`managed_components/`, so switching targets re-fetches the board-specific
components.

`partitions.csv` gives the app a **2 MB** slot. The stock 1 MB layout no longer
fits: `esp_wifi` + lwip + mbedtls add roughly 470 KB, taking the image past
1.4 MB. At 2 MB there is about 27% headroom, and everything above `0x210000`
is unallocated — which is where an OTA layout would go if the Wi-Fi link is
ever used for firmware updates.

Or use the ESP-IDF VS Code extension (set the target before the first build).

**Do not use `idf.py monitor`.** The console is off (`CONFIG_ESP_CONSOLE_NONE`)
and UART0 carries protocol frames; firmware logs arrive as mux LOG frames.
Use the host simulator below as your log console.

## Admin panel

Staff configure the network on the terminal itself. Two gates stand in front of
it: the **cabinet door switch** and the **passcode**.

Wire a microswitch between the door pin and GND — IO32 on the E32R40T, IO46 on
the ES3C35P — with the internal pull-up doing the rest:

```
IO32/IO46 --[ door microswitch ]-- GND

door SHUT  -> switch closed -> LOW
door OPEN  -> released      -> HIGH
wire cut   -> HIGH = "open"
```

A cut or absent wire reads as "door open" deliberately. The passcode is the
real security boundary, so failing that way keeps the terminal administrable
and leaves a loud permanent banner; failing to "shut" would make the panel
unreachable without a reflash. A board with no switch at all therefore shows
the banner permanently — set `PIN_NUM_DOOR` to `-1` in the board header to opt
out instead.

Opening the door puts a gold banner across the top of whatever is on screen,
with an **АДМИН** button. That asks for `SMIB_ADMIN_PASSCODE`; three wrong
codes lock the keypad for 30 seconds. Only then is the panel built, so a player
at an open cabinet never sees the configuration.

The panel holds:

- **Status** — Wi-Fi state, IP, RSSI, CMS link, firmware version, device id
- **Wi-Fi** — SSID from a scan list or typed in (hidden networks), password
- **CMS** — host as a dotted IP *or* a hostname, and port, validated 1-65535
- **Зачувај и поврзи** — saves to NVS, then re-points the radio and the CMS
  socket at the new values. No reboot.
- **Фабрички** — drops the saved values and shows the `.env` defaults, without
  applying them; Save or Restart still has to commit
- **Рестарт**, **Затвори**

Closing the cabinet door closes the panel.

Two rules worth knowing:

- **Saving is refused while a funds transfer is in flight.** Dropping the CMS
  socket during a commit or a cashout credit is exactly what "Funds-transfer
  safety" below forbids, so the panel says "Трансакција во тек" and changes
  nothing. Status stays readable throughout.
- **The passcode is build-time only**, never editable from the terminal. A
  stolen terminal cannot be re-pointed at another CMS by whoever holds it.

Precedence for every other setting is NVS first, `.env` second: a freshly
flashed board joins the network from `.env` with nobody touching the screen,
and once staff save, their values win.

## Testing with the host simulator

`tools/host_sim/` is a Python test bench. Because the two links are now on
separate wires, it runs as **two processes**:

| Process | Plays | Transport |
|---|---|---|
| `host_sim.py` | mux splitter + fake EGM, and the log console | serial |
| `cms_tcp_server.py` | fake CMS (canned player, drink menu) + bar console | TCP :9020 |

`mux.py` doubles as the reference parser for the production splitter app, and
serves both legs.

```sh
python3 -m venv tools/host_sim/.venv
tools/host_sim/.venv/bin/pip install -r tools/host_sim/requirements.txt

# terminal 1 — the machine side and the firmware's logs
tools/host_sim/.venv/bin/python tools/host_sim/host_sim.py \
    --port /dev/cu.usbserial-XXXX

# terminal 2 — the CMS; point the board here (.env, or the admin panel)
tools/host_sim/.venv/bin/python tools/host_sim/cms_tcp_server.py \
    --port 9020 --balance 1250000 --points 340
```

The board has to be able to reach the machine running `cms_tcp_server.py`, so
set `SMIB_CMS_HOST` to that machine's LAN address — not `127.0.0.1`, which on
the board means the board itself. The desktop simulator is the exception:
loopback is correct there.

Fault-injection flags, by process:

| Flag | Process | Tests |
|---|---|---|
| `--fail-auth` / `--uid "AA BB…"` | cms_tcp | rejected card toast + fail cue |
| `--fail-debit` | cms_tcp | transfer refused, nothing moved |
| `--drop-commit` | cms_tcp | commit retry path (machine keeps its credits) |
| `--drop-credit` | cms_tcp | cashout credit retry path (card still gets the funds) |
| *don't start it* | cms_tcp | offline badge, auth blocked (replaces `--no-cms`) |
| `--aft-status 0x80` | host_sim | AFT failure → CMS rollback, balance restored (also fails cashout, machine keeps credits) |
| `--aft-pending 3` | host_sim | AFT pending → interrogate loop |
| `--sas-verbose` | host_sim | dump every SAS exchange |

`host_sim.py` warns if a CMS frame ever arrives on the serial link — that means
firmware older than this port, and nothing there will answer it.

`python3 tools/host_sim/selftest.py` runs the offline protocol tests (framing
resync, CMS-over-TCP segmentation, EGM AFT byte layout, CMS idempotency).

### Manual acceptance script

1. Boot with both processes running → offline badge clears once the SAS link
   and the CMS socket are up. `[TCP] bridge connected` appears in terminal 2.
2. Hold a card to the reader for 1.5 s → three rising tones (one every
   0.5 s) + "Држете ја картичката...", the auth request goes out only
   after the window elapses → long beep + main screen (name/points
   top-right, ИСПЛАТИ top-left, logo bottom-right) and a "може да ја
   тргнете картичката" toast. Remove the card → the session stays open.
   A quick tap (<1.5 s) → no auth at all, "држете ја картичката подолго"
   toast; removing it mid-auth → "тргната предвреме".
3. Tap any card while a session is open → warning toast, nothing changes.
4. НАРАЧАЈ → menu loads from the sim → toggle Кафе + Вода buttons (one of
   each max; total shown at the top, КОРИСТИ ПОЕНИ switches it to points)
   → НАРАЧАЈ → success toast + cue, balance drops (`[CMS] ORDER …`).
5. ПРЕФРЛИ → 500 → progress steps → success toast + cue, sim prints
   `[EGM] +500.00 credits`.
6. ИСПЛАТИ → sim prints `[EGM] -500.00 credits` and `[CMS] CREDIT 500.00`,
   toast "Исплатени 500 МКД", balance restored, back to the idle screen.
   With an empty machine ИСПЛАТИ just logs out.
7. Leave an authed session untouched (no touch, no game activity) for
   5 minutes → automatic logout ("Одјавени сте").
8. Open the cabinet door (`door open` in the desktop sim, or the real switch)
   → gold banner over whatever is on screen + АДМИН button. Wrong code three
   times → "Блокирано (30s)". Right code → the panel. Скенирај lists networks;
   pick one and the SSID field fills in. Change the CMS port to something
   nothing listens on, Зачувај → offline badge within a few seconds; change it
   back → the link returns without a reboot. Close the door → the panel closes.
9. Start a transfer and open the panel mid-chain → Зачувај refuses with
   "Трансакција во тек" and the transfer completes untouched.
10. Kill `cms_tcp_server.py` mid-session → offline badge, session ends; restart
   it → the terminal reconnects on its own (backoff caps at 8 s) and an open
   order comes back via `orders_req`.
11. Re-run with each fault flag and confirm the documented behavior; reset
   the board mid-run → the sim resyncs past the boot-ROM garbage.

## Testing without hardware (desktop simulator)

`sim/` builds the firmware as a macOS (or Linux) desktop app: the real
`app/`, `cms/`, `sas/`, `mux/` and `ui/` code with LVGL, and only
`drivers/` swapped out. The host simulator connects to it unchanged, so the
acceptance script above and every fault flag work without the board.
The sim stands in for either board: both run the same 480×320 landscape UI,
and it uses the E32R40T pin header (its `sdkconfig.h` has no board symbol).

Note that `cms/cms_link.c` is **not** stubbed out: lwip's socket API is POSIX,
so the sim opens a real TCP connection to `cms_tcp_server.py`. Only the Wi-Fi
radio around it is faked.

| Board | Simulator |
|---|---|
| LCD + touch | 480×320 SDL window (2× zoom); the mouse is the finger |
| RC522 | hold `1` / `2` / `3` = player / unknown / second-player card on the reader; hold `F` = flaky card at the edge of range |
| Cabinet door | hold `D`, or latch it with `door open` / `door closed` |
| Wi-Fi | fake radio with a canned scan list; `wifi down` parks the CMS socket too, so the offline badge actually appears |
| Settings (NVS) | `smib-sim-settings.txt` beside the binary, so a save survives `R` |
| UART0 (CH340) | pseudo-terminal, linked at `/tmp/smib-sim.tty`, paced to 115200 baud |
| Speaker | the firmware's cue patterns on the Mac's audio output |
| Reset | `R`: reboots the firmware, ROM banner and all; the serial port stays open |

Needs SDL2 and CMake (`brew install sdl2 cmake`), plus one ESP-IDF build or
`idf.py reconfigure` here: the sim reads this project's `sdkconfig` (the
esp32 one, not `sdkconfig.s3`) and `managed_components/` (only LVGL, which
both targets fetch).

```sh
cmake -S sim -B sim/build && cmake --build sim/build -j
./sim/build/smib_sim                                      # terminal 1

tools/host_sim/.venv/bin/python tools/host_sim/host_sim.py \
    --port /tmp/smib-sim.tty                              # terminal 2

tools/host_sim/.venv/bin/python tools/host_sim/cms_tcp_server.py \
    --port 9020                                           # terminal 3
```

Set `SMIB_CMS_HOST=127.0.0.1` in `.env` for the sim — loopback is right here,
unlike on a board.

Keys act when the sim window has focus: `S` screenshot, `M` mute, `H` help.
After a rebuild, `R` loads the new binary without restarting the host sim.
Options: `--fat-finger 8` (offset each touch like a finger on the resistive
panel), `--noise 0.001` (flip random bits on the link), `--fast-uart` (no
baud pacing), `--logs` (firmware logs in the sim's terminal too), `--mute`,
`--zoom N`.

Scripts drive it through the FIFO `/tmp/smib-sim.ctl` (the same commands
can be typed into the sim's terminal):

```sh
echo "card 1"          > /tmp/smib-sim.ctl   # 1|2|3|flaky|<uid>|off
sleep 2
echo "card off"        > /tmp/smib-sim.ctl
echo "tap 267 278"     > /tmp/smib-sim.ctl   # ПРЕФРЛИ
echo "door open"       > /tmp/smib-sim.ctl   # admin banner; "closed" to shut it
echo "wifi down"       > /tmp/smib-sim.ctl   # offline badge; "up" to restore
echo "shot screen.bmp" > /tmp/smib-sim.ctl
echo "mem"             > /tmp/smib-sim.ctl   # LVGL heap usage
```

The sim is built with AddressSanitizer + UBSan (`-DSMIB_SIM_SANITIZE=thread`
for ThreadSanitizer, `none` for neither). LVGL's settings, heap size
included, come from `sdkconfig`; with 8-byte pointers the sim needs more
heap than the board, so its `mem` numbers are an upper bound. Its clock
stops while the Mac sleeps, so a closed lid doesn't time out the links.

What it can't show: SPI/panel timing and orientation, touch calibration,
RC522 read range, task stack sizes (every task gets 1 MB here), real Wi-Fi
behaviour (association failures, roaming, RSSI), free heap on the board, and
the CH340/USB link. A firmware abort stops the sim with a stack trace instead
of rebooting. macOS prints a harmless `Task policy set failed` line after each
reset.

## Funds-transfer safety

Deposits (ПРЕФРЛИ) run CMS debit hold → SAS AFT → CMS commit:

- Debit fails/times out → nothing moved.
- AFT fails → rollback the hold (retried 3×, then "контактирајте персонал").
- AFT succeeded but commit unanswered → **never** rollback; the commit is
  retried until the CMS confirms (`debit_commit`/`debit_rollback` are
  idempotent by `txn` — a hard requirement on the real CMS).

Cashouts (ИСПЛАТИ) run the reverse: SAS AFT from the machine → CMS credit →
logout:

- The amount is the machine's credit meter, read fresh (0x1A) right before
  the transfer; an empty machine skips straight to logout.
- AFT-from fails → nothing moved, the session stays open.
- AFT-from succeeded → the credits are off the machine; the `credit_req`
  (idempotent by `txn`, like commit) is retried until the CMS confirms.

Card removal never interrupts any of this — sessions survive card removal
and end via ИСПЛАТИ, a CMS `logout_push`, or 5 minutes of inactivity (no
touch and no machine activity).

The admin panel is bound by the same rules: **saving network settings is
refused while `s_xfer != XFER_NONE`**. A commit or a cashout credit that has
already left the machine is being retried until the CMS confirms it, and
re-pointing the socket mid-retry is exactly the failure those rules exist to
prevent. `app_can_change_network()` is the single check, in `app/app.c` with
the rest of the money state.

## Architecture notes

- **LVGL is single-threaded**: only `app_task` (same thread as `lv_timer_handler`) calls LVGL APIs. Every other task — mux RX, SAS, CMS, RC522 event handler, audio — communicates exclusively via `app_post_event()`.
- **Event queue**: `app_post_event()` is safe to call from any task; it has zero wait and drops + logs on overflow.
- **One frame format, two transports**: `[0x7E][chan][len16][payload][crc16]`,
  CRC-16/KERMIT, no byte stuffing. SAS (`0x01`) and logs (`0x7F`) share UART0
  so the host PC can demultiplex them; CMS (`0x02`) rides a TCP socket.
  `mux/mux_frame.c` holds the single implementation of the framing and its
  incremental parser, and both legs feed it with their own accumulator.
  Parsers resync on `0x7E` + CRC, which also skips the boot-ROM banner after a
  reset. The parser's stall timeout is on for the UART (where a frame really
  can be cut off mid-flight) and off for TCP (where a partial frame always
  completes, and the only failure is the socket closing — which resets it).
- **The CMS socket reconnects itself**, backing off 0.5 s → 8 s. `cms_tick`'s
  existing ping/pong still decides whether the CMS is *answering*; the socket
  state short-circuits it so a dropped connection shows the offline badge at
  once rather than six seconds later. Changing the endpoint from the admin
  panel takes effect within one receive timeout (~200 ms), with no other
  thread touching the socket.
- **Money chain state** lives in `app/app.c` (`XFER_*`); see
  "Funds-transfer safety" above before changing it.

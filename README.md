# SMIB — Slot Machine Interface Board

Firmware for the **4.0" ESP32-32E CYD** (model E32R40T) or the **3.5" ESP32-S3
display** (LCDWiki ES3C35P) that acts as a cashless
payment terminal attached to a slot machine. The device authenticates a player
via NFC card, talks **SAS** (Slot Accounting System) to the machine and JSON to
the **CMS** (Cash Management System) — both multiplexed over the single USB
serial link (UART0). A host-side app on the slot machine PC splits the stream
and relays the CMS channel to the network; the SMIB itself needs no Wi-Fi.

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
├── cms/                 CMS JSON client (auth, menu, orders, debit/commit/rollback)
├── sas/                 SAS host engine (polls, exceptions, credits, AFT)
├── mux/
│   ├── crc16.c/.h       CRC-16/KERMIT (mux frames + SAS messages)
│   ├── mux.c/.h         UART0 multiplexer (SAS 0x01, CMS 0x02, logs 0x7F)
│   └── log_bridge.c/.h  Forwards ESP_LOG output upstream over the mux
├── drivers/
│   ├── display.h        Display + touch + LVGL bring-up (one .c per board)
│   ├── display_st7796.c   E32R40T: ST7796 + XPT2046
│   ├── display_st77922.c  ES3C35P: ST77922 QSPI + integrated touch, sw rotation
│   ├── audio.h          Speaker cues (one .c per board)
│   ├── audio_ledc.c       E32R40T: LEDC square wave into the amp
│   ├── audio_i2s.c        ES3C35P: generated tones via ES8311 over I2S
│   ├── i2c_bus.c/.h     ES3C35P shared I2C bus (touch + codec)
│   └── nfc.c/.h         RC522 init, card-present/removed events
├── ui/
│   ├── ui.c/.h          Screen router
│   ├── ui_main.c        Main/idle screen layout
│   ├── ui_idle.c        Idle state rendering
│   ├── ui_order.c       Order/purchase flow screen
│   └── ui_transfer.c    AFT credit transfer screen
└── assets/              LVGL image descriptors and font glyphs
```

## Dependencies

Managed via the ESP-IDF Component Manager (`main/idf_component.yml`):

- `lvgl/lvgl`
- `abobija/rc522 ^3.4.3`
- esp32 only: `espressif/esp_lcd_st7796`, `atanisoft/esp_lcd_touch_xpt2046`
- esp32s3 only: `espressif/esp_lcd_st77922`, `espressif/esp_codec_dev`

Each target has its own lock file (`dependencies.lock.<target>`).

## Build & flash

Requires **ESP-IDF v5.5+**.

```sh
# E32R40T (esp32): the default build/ and sdkconfig
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash

# ES3C35P (esp32s3): separate build dir and sdkconfig so both coexist
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 set-target esp32s3   # once
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 build
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 -p /dev/cu.usbserial-XXXX flash
```

`sdkconfig.defaults` holds the shared settings; `sdkconfig.defaults.<target>`
adds the board choice (and for the S3: 16 MB flash, octal PSRAM, 1.5 MB app
partition). Both builds share `managed_components/`, so switching targets
re-fetches the board-specific components.

Or use the ESP-IDF VS Code extension (set the target before the first build).

**Do not use `idf.py monitor`.** The console is off (`CONFIG_ESP_CONSOLE_NONE`)
and UART0 carries protocol frames; firmware logs arrive as mux LOG frames.
Use the host simulator below as your log console.

## Testing with the host simulator

`tools/host_sim/` is a Python test bench that plays both peers at once: the
mux splitter, a fake EGM answering SAS, and a fake CMS with a canned player
and drink menu. Its `mux.py` doubles as the reference parser for the future
production splitter app.

```sh
python3 -m venv tools/host_sim/.venv
tools/host_sim/.venv/bin/pip install -r tools/host_sim/requirements.txt

tools/host_sim/.venv/bin/python tools/host_sim/host_sim.py \
    --port /dev/cu.usbserial-XXXX --balance 1250000 --points 340
```

Fault-injection flags:

| Flag | Tests |
|---|---|
| `--fail-auth` / `--uid "AA BB…"` | rejected card toast + fail cue |
| `--fail-debit` | transfer refused, nothing moved |
| `--aft-status 0x80` | AFT failure → CMS rollback, balance restored (also fails cashout, machine keeps credits) |
| `--aft-pending 3` | AFT pending → interrogate loop |
| `--drop-commit` | commit retry path (machine keeps its credits) |
| `--drop-credit` | cashout credit retry path (card still gets the funds) |
| `--no-cms` | offline badge, auth blocked |
| `--sas-verbose` | dump every SAS exchange |

`python3 tools/host_sim/selftest.py` runs the sim's offline protocol tests
(framing resync, EGM AFT byte layout, CMS idempotency).

### Manual acceptance script

1. Boot with the sim running → offline badge clears once both links are up.
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
8. Re-run with each fault flag and confirm the documented behavior; reset
   the board mid-run → the sim resyncs past the boot-ROM garbage.

## Testing without hardware (desktop simulator)

`sim/` builds the firmware as a macOS (or Linux) desktop app: the real
`app/`, `cms/`, `sas/`, `mux/` and `ui/` code with LVGL, and only
`drivers/` swapped out. The host simulator connects to it unchanged, so the
acceptance script above and every fault flag work without the board.
The sim stands in for either board: both run the same 480×320 landscape UI,
and it uses the E32R40T pin header (its `sdkconfig.h` has no board symbol).

| Board | Simulator |
|---|---|
| LCD + touch | 480×320 SDL window (2× zoom); the mouse is the finger |
| RC522 | hold `1` / `2` / `3` = player / unknown / second-player card on the reader; hold `F` = flaky card at the edge of range |
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

tools/host_sim/venv/bin/python tools/host_sim/host_sim.py \
    --port /tmp/smib-sim.tty                              # terminal 2
```

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
echo "shot screen.bmp" > /tmp/smib-sim.ctl
echo "mem"             > /tmp/smib-sim.ctl   # LVGL heap usage
```

The sim is built with AddressSanitizer + UBSan (`-DSMIB_SIM_SANITIZE=thread`
for ThreadSanitizer, `none` for neither). LVGL's settings, heap size
included, come from `sdkconfig`; with 8-byte pointers the sim needs more
heap than the board, so its `mem` numbers are an upper bound. Its clock
stops while the Mac sleeps, so a closed lid doesn't time out the links.

What it can't show: SPI/panel timing and orientation, touch calibration,
RC522 read range, task stack sizes (every task gets 1 MB here), and the
CH340/USB link. A firmware abort stops the sim with a stack trace instead of
rebooting. macOS prints a harmless `Task policy set failed` line after each
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

## Architecture notes

- **LVGL is single-threaded**: only `app_task` (same thread as `lv_timer_handler`) calls LVGL APIs. Every other task — mux RX, SAS, CMS, RC522 event handler, audio — communicates exclusively via `app_post_event()`.
- **Event queue**: `app_post_event()` is safe to call from any task; it has zero wait and drops + logs on overflow.
- **SAS, CMS and logs share UART0**: the mux layer frames all three streams
  (`[0x7E][chan][len16][payload][crc16]`, CRC-16/KERMIT, no byte stuffing) so
  the host PC can demultiplex them; parsers resync on `0x7E` + CRC, which also
  skips the boot-ROM banner after a reset.
- **Money chain state** lives in `app/app.c` (`XFER_*`); see
  "Funds-transfer safety" above before changing it.

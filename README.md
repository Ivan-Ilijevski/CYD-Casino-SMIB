# SMIB — Slot Machine Interface Board

Firmware for the **4.0" ESP32-32E CYD** (model E32R40T) that acts as a cashless
payment terminal attached to a slot machine. The device authenticates a player
via NFC card, talks **SAS** (Slot Accounting System) to the machine and JSON to
the **CMS** (Cash Management System) — both multiplexed over the single USB
serial link (UART0). A host-side app on the slot machine PC splits the stream
and relays the CMS channel to the network; the SMIB itself needs no Wi-Fi.

## Hardware

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

## Project structure

```
main/
├── smib_main.c          Entry point; boot sequence and main loop
├── board.h              All pin and resolution constants
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
│   ├── display.c/.h     ST7796 + XPT2046 init, LVGL flush/touch callbacks
│   ├── nfc.c/.h         RC522 init, card-present/removed events
│   └── audio.c/.h       Speaker amp enable and tone output
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

- `espressif/esp_lcd_st7796`
- `atanisoft/esp_lcd_touch_xpt2046`
- `lvgl/lvgl`
- `abobija/rc522 ^3.4.3`

## Build & flash

Requires **ESP-IDF v5.5+**.

```sh
idf.py build
idf.py -p /dev/cu.usbserial-XXXX flash
```

Or use the ESP-IDF VS Code extension (set target to `esp32` before the first build).

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
| `--aft-status 0x80` | AFT failure → CMS rollback, balance restored |
| `--aft-pending 3` | AFT pending → interrogate loop |
| `--drop-commit` | commit retry path (machine keeps its credits) |
| `--no-cms` | offline badge, auth blocked |
| `--sas-verbose` | dump every SAS exchange |

`python3 tools/host_sim/selftest.py` runs the sim's offline protocol tests
(framing resync, EGM AFT byte layout, CMS idempotency).

### Manual acceptance script

1. Boot with the sim running → offline badge clears once both links are up.
2. Tap a card → scan beep → spinner → main screen with name/balance/points.
   Remove the card → idle screen; the sim logs the logout.
3. НАРАЧАЈ → menu loads from the sim → 2× Кафе → success toast + cue,
   balance drops (`[CMS] ORDER …`).
4. ПРЕФРЛИ → 500 → progress steps → success toast + cue, sim prints
   `[EGM] +500.00 credits`.
5. Re-run with each fault flag and confirm the documented behavior; reset
   the board mid-run → the sim resyncs past the boot-ROM garbage.

## Funds-transfer safety

The chain is CMS debit hold → SAS AFT → CMS commit:

- Debit fails/times out → nothing moved.
- AFT fails → rollback the hold (retried 3×, then "контактирајте персонал").
- AFT succeeded but commit unanswered → **never** rollback; the commit is
  retried until the CMS confirms (`debit_commit`/`debit_rollback` are
  idempotent by `txn` — a hard requirement on the real CMS).
- Card removed mid-chain → the chain resolves first, then the logout runs.

## Architecture notes

- **LVGL is single-threaded**: only `app_task` (same thread as `lv_timer_handler`) calls LVGL APIs. Every other task — mux RX, SAS, CMS, RC522 event handler, audio — communicates exclusively via `app_post_event()`.
- **Event queue**: `app_post_event()` is safe to call from any task; it has zero wait and drops + logs on overflow.
- **SAS, CMS and logs share UART0**: the mux layer frames all three streams
  (`[0x7E][chan][len16][payload][crc16]`, CRC-16/KERMIT, no byte stuffing) so
  the host PC can demultiplex them; parsers resync on `0x7E` + CRC, which also
  skips the boot-ROM banner after a reset.
- **Money chain state** lives in `app/app.c` (`XFER_*`); see
  "Funds-transfer safety" above before changing it.

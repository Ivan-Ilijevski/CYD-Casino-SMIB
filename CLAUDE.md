# SMIB firmware: notes for Claude

Cashless terminal for slot machines: NFC card auth, SAS to the machine and CMS
JSON, all multiplexed over UART0. README.md is the full reference. This file
lists the rules that are easy to break.

## Two boards, one codebase

| Board | IDF target | Display | Touch | Audio |
|---|---|---|---|---|
| E32R40T (4.0" ESP32 CYD) | `esp32` | ST7796 SPI | XPT2046 (SPI) | LEDC square wave into the amp |
| ES3C35P (3.5" LCDWiki ESP32-S3) | `esp32s3` | ST77922 QSPI | built into ST77922 (I2C 0x55) | ES8311 codec over I2S |

- The board follows the target: `main/Kconfig.projbuild` (choice `SMIB_BOARD`),
  with defaults in `sdkconfig.defaults.<target>`.
- `main/board.h` includes `main/boards/board_<name>.h`. Every board header defines
  the same macro names (`PIN_NUM_*`, `LCD_H_RES/V_RES`, `MUX_*`), so shared code
  never needs `#ifdef CONFIG_SMIB_BOARD_*`. Keep it that way.
- Board-specific code lives in per-board files that implement the unchanged
  `drivers/display.h` and `drivers/audio.h`. `main/CMakeLists.txt` picks them.
  `drivers/nfc.c` is shared because the RC522 is on SPI3 on both boards.
- Board-only components are gated with `rules: - if: "target == …"` in
  `main/idf_component.yml`. Each target has its own lock file,
  `dependencies.lock.<target>`, and both lock files are committed.
- To add a board: create a board header, add a Kconfig choice entry
  (`depends on IDF_TARGET_…`), add `sdkconfig.defaults.<target>`, write the
  display/audio `.c` files, add the CMake branch, and add the component rules.

## Build

```sh
idf.py build                                                   # esp32  → build/,    sdkconfig
idf.py -B build_s3 -D SDKCONFIG=sdkconfig.s3 build             # esp32s3 → build_s3/, sdkconfig.s3
cmake -S sim -B sim/build && cmake --build sim/build -j        # desktop sim
```

- Build both targets after any change outside `drivers/`, and build the sim too.
- The first S3 build needs `set-target esp32s3` with the same `-B`/`-D` flags.
  `sdkconfig.s3` is generated and gitignored like `sdkconfig`. Put lasting
  settings in `sdkconfig.defaults*`.
- Switching targets re-fetches components into the shared `managed_components/`.
  That is expected.
- The sim reads the esp32 `sdkconfig`, uses the E32R40T pin header, and
  excludes all of `main/drivers/`.

## Rules

- **LVGL is single-threaded.** Only the app task (the `lv_timer_handler` loop
  in `smib_main.c`) may call LVGL. Every other task and callback (mux RX,
  SAS, CMS, RC522 events, audio) goes through `app_post_event()`. The one
  exception is `lv_display_flush_ready` from the panel IO trans-done callback.
- **UART0 is protocol-only.** Do not run `idf.py monitor`. Logs travel as mux
  LOG frames, and the host sim is the log console.
- **Funds transfers**: never roll back after a successful AFT. Commit and credit
  are retried until the CMS confirms. Read "Funds-transfer safety" in the
  README before touching `app/`, `cms/` or `sas/`.
- **Display flush**: signal `lv_display_flush_ready` from the panel IO's
  trans-done callback, not right after `draw_bitmap`. Signalling early tears the image.
- **ES3C35P specifics**:
  - The panel can't swap axes (no validated MADCTL.MV), so the UI is rotated
    in software in the flush callback (`UI_ROTATION`).
  - Column windows must be 4-px aligned, which the LVGL rounder handles. It
    rounds UI *rows*, because rows become panel columns after rotation.
  - The panel init sequence comes from xiaozhi-esp32's `lcdwiki-es3c35p`
    board. The component's default sequence is for a different panel.
  - The RC522 uses the microSD pins, so SD card support is unavailable on this board.

## Reference

- `skills/temu_esp_cyd/`: E32R40T hardware docs (vendor extraction).
- `skills/lcdwiki_es3c35p/`: ES3C35P hardware notes.
- `docs/SAS_CMS_PLAN.md`: protocol design.

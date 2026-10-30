# SMIB firmware: notes for Claude

Cashless terminal for slot machines: NFC card auth, SAS to the machine over
UART0, CMS JSON over Wi-Fi. README.md is the full reference. This file lists
the rules that are easy to break.

## Two boards, one codebase

| Board | IDF target | Display | Touch | Audio | Door pin |
|---|---|---|---|---|---|
| E32R40T (4.0" ESP32 CYD) | `esp32` | ST7796 SPI | XPT2046 (SPI) | LEDC square wave into the amp | IO32 |
| ES3C35P (3.5" LCDWiki ESP32-S3) | `esp32s3` | ST77922 QSPI | built into ST77922 (I2C 0x55) | ES8311 codec over I2S | IO46 |

- The board follows the target: `main/Kconfig.projbuild` (choice `SMIB_BOARD`),
  with defaults in `sdkconfig.defaults.<target>`.
- `main/board.h` includes `main/boards/board_<name>.h`. Every board header defines
  the same macro names (`PIN_NUM_*`, `LCD_H_RES/V_RES`, `MUX_*`), so shared code
  never needs `#ifdef CONFIG_SMIB_BOARD_*`. Keep it that way.
- Board-specific code lives in per-board files that implement the unchanged
  `drivers/display.h` and `drivers/audio.h`. `main/CMakeLists.txt` picks them.
  `drivers/nfc.c` is shared because the RC522 is on SPI3 on both boards.
- `main/drivers/` is also the **desktop sim's exclusion boundary**, so anything
  platform-backed goes there and gets a `sim/src/sim_*.c` counterpart:
  `net.h` (Wi-Fi), `settings.h` (NVS), `door.h` (door switch). Put a new
  platform dependency behind a `drivers/` header or the sim stops building.
  `cms/cms_link.c` is the deliberate exception — lwip's socket API is POSIX, so
  one file serves both.
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
- `.env` (gitignored, template in `.env.example`) carries the admin passcode and
  the factory Wi-Fi/CMS defaults. `cmake/smib_env.cmake` parses it for both the
  firmware and the sim, so they always agree on the passcode. Editing `.env`
  re-runs CMake.
- **LVGL is pinned** (`~9.5.0`). Both targets share one `managed_components/`,
  so an unpinned `'*'` let the last target built decide the version for the
  other *and* for the sim — which is how the two lock files once disagreed
  (9.5.0 vs 9.6.0~1) and a 9.6 install broke the sim. Prefer APIs that exist in
  both 9.x minors: `display_st77922.c` uses `lv_draw_sw_rotate`, not 9.6's
  `lv_draw_rotate` wrapper, for exactly this reason.
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
- **UART0 carries SAS and logs only.** The CMS went to Wi-Fi
  (`cms/cms_link.c`); `MUX_CH_CMS` still exists so both ends agree on the
  channel numbering, and `host_sim.py` warns if a CMS frame ever shows up on
  the wire. Don't route CMS back through the mux "as a fallback" — a transport
  that can switch mid-commit breaks the funds-transfer rules below.
- **Funds transfers**: never roll back after a successful AFT. Commit and credit
  are retried until the CMS confirms. Read "Funds-transfer safety" in the
  README before touching `app/`, `cms/` or `sas/`.
- **Nothing may re-point the network mid-transfer.** The admin panel's save
  goes through `app_on_ui_admin_save()`, which refuses while
  `s_xfer != XFER_NONE`. Any new way to change the Wi-Fi or CMS endpoint must
  go through `app_can_change_network()` too.
- **The admin passcode is build-time only** (`SMIB_ADMIN_PASSCODE`), never
  settable from the terminal: a stolen unit must not be re-pointable at another
  CMS by whoever holds it. Don't add a "change passcode" screen.
- **The door switch fails open.** A cut wire reads "door open" so the terminal
  stays administrable and shows a banner; the passcode is the real boundary.
  Don't "fix" this into failing shut — that bricks admin access short of a
  reflash.
- **`Sf_Compact_14` only covers ASCII + Cyrillic** (generated with
  `--range 32-127,1024-1119`). It is the theme's default font, so anything
  outside those ranges draws as a tofu box. That includes glyphs **LVGL
  supplies itself**, not just strings in this repo:
  - `lv_keyboard` — its backspace/enter/OK keys are `LV_SYMBOL_*`; give the
    whole widget `lv_font_montserrat_14`.
  - `lv_dropdown` — its arrow is `LV_SYMBOL_DOWN`; put Montserrat on
    `LV_PART_INDICATOR` only, so the list keeps Cyrillic.
  - `lv_textarea` password mode — masks with U+2022 BULLET; call
    `lv_textarea_set_password_bullet(ta, "*")`.
  - Any `LV_SYMBOL_*` in a label (see the order badge in `ui_main.c`).

  In this repo's own strings use ASCII `...`, never `…` (U+2026). A quick
  audit: flag characters in a UI file that are neither `< 128` nor in
  `0x400-0x45F`.
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

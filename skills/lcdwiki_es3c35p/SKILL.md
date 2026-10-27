---
name: lcdwiki_es3c35p
description: Hardware reference for the LCDWiki 3.5" ESP32-S3 display board (ES3C35P / ES3C35P-NS) with the ST77922 QSPI panel, integrated touch and ES8311 audio
---

# LCDWiki 3.5" ESP32-S3 Display (ES3C35P)

This is the second board the SMIB firmware supports (IDF target `esp32s3`). The
pin map is in `main/boards/board_es3c35p.h` and the drivers are
`main/drivers/display_st77922.c` and `audio_i2s.c`. The 4.0" ESP32 board is
covered by the `temu_esp_cyd` skill.

Sources:
- Product page: https://www.lcdwiki.com/3.5inch_ESP32-S3_Display
- Schematic and IO allocation table, both linked from the product page.
- xiaozhi-esp32 `main/boards/lcdwiki-es3c35p/`, the only open-source reference
  for the panel init sequence and the touch protocol. LCDWiki's quick-start zip
  contains only binaries.

## Board

- **MCU**: ESP32-S3 N16R8 (16 MB QSPI flash, 8 MB octal PSRAM)
- **USB**: a single USB-C, wired to the S3's native USB-Serial-JTAG. There is no
  CH340 on this unit, so UART0 is reachable only on the 4-pin header. Flashing
  and the dev console both go over USB-Serial-JTAG, which is a separate
  peripheral from UART0 and so does not break the protocol-only rule
  (`sdkconfig.defaults.esp32s3` sets `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG`).
- **Display**: 3.5" IPS module (HMX035CTFT-001), 320×480 portrait, RGB565
- **Models**: ES3C35P has a speaker; ES3C35P-NS has none

## Pins

| Function | GPIO | Notes |
|---|---|---|
| LCD QSPI CLK / D0 / D1 / D2 / D3 | 12 / 11 / 13 / 14 / 9 | SPI2 |
| LCD CS | 10 | |
| LCD reset | EN | shared with the chip reset, so no GPIO |
| LCD TE | 42 | unused |
| Backlight | 41 | high = on |
| I2C SDA / SCL | 38 / 39 | shared by touch, ES8311 and the expansion I2C header |
| Touch RST / INT | 48 / 47 | RST active low; INT low on touch |
| I2S MCLK / BCLK / LRCK | 17 / 18 / 21 | |
| I2S DOUT (to codec) / DIN (mic) | 15 / 16 | |
| Amp enable | 1 | active low |
| microSD CLK / CMD / D0 / D1 / D2 / D3 | 5 / 4 / 6 / 7 / 2 / 3 | SDIO; **SMIB uses these for the RC522** |
| Battery ADC | 8 | |
| RGB LED (WS2812B) | 40 | |
| BOOT button | 0 | |
| UART0 RX / TX | 44 / 43 | 4-pin header only (no CH340 on board) |
| Free GPIO | 45, 46 | strapping pins, avoid them as outputs at boot |

The LCDWiki IO table lists touch RST/INT as 47/48, the reverse of the
product page. xiaozhi uses RST=48 and INT=47, and so does SMIB.

## Display: ST77922 over QSPI

- Panel IO: `ST77922_PANEL_IO_QSPI_CONFIG`, 32-bit commands, 80 MHz (the
  rate xiaozhi validated), `use_qspi_interface = 1`.
- **A custom init sequence is required.** `esp_lcd_st77922`'s default sequence
  targets a 532×300 panel. Use the sequence in `display_st77922.c`.
- Color: **inversion must stay on**. The vendor sequence sends `0x21` (INVON) and
  the panel shows a photo-negative without it, so call
  `esp_lcd_panel_invert_color(panel, true)` — passing `false` undoes the vendor
  sequence. (xiaozhi passes `false`; this module disagrees with it.)
- MADCTL ends at `0x00`. The vendor sequence's own `0x36` overwrites the
  component's cached `madctl_val`, so the BGR bit from `rgb_ele_order` is gone
  before `esp_lcd_panel_mirror()` runs and cannot be restored by it. `0x00` is
  what this module wants.
- **No axis swap**: `swap_xy` needs `supports_madctl_mv`, which is not validated
  for this module. For landscape, render with `lv_display_set_rotation` and
  rotate in the flush callback with `lv_draw_rotate`. `lv_draw_rotate` does not
  accept `RGB565_SWAPPED`, so render plain `RGB565`, rotate, then call
  `lv_draw_rgb565_swap`.
- **4-pixel column alignment**: CASET start and end must be multiples of 4 (x1 % 4 == 0 and
  (x2 + 1) % 4 == 0). Enforce it with an `LV_EVENT_INVALIDATE_AREA` rounder. When the UI is
  rotated 90° or 270°, round the *rows* instead. LVGL also sizes its render
  strips through the rounder.

## Touch: integrated in the ST77922 (TDDI)

The touch controller is not a separate FT6336, and `esp_lcd_touch_ft5x06` does not apply.
- I2C address 0x55, 400 kHz, **16-bit big-endian register addresses**.
- Reset: pulse RST low for 10 ms, then wait 100 ms.
- Reg `0x0009`: maximum number of touch points (1–5).
- Reg `0x0010`: status; bit 3 is set when touch data is ready.
- Reg `0x0014`: points, 7 bytes each. Byte 0 bit 7 = valid.
  x = `(b0 & 0x3F) << 8 | b1`, y = `(b2 & 0x3F) << 8 | b3`, in panel-native
  portrait coordinates. LVGL applies the display rotation to pointer input.

## Audio: ES8311 + FM8002E/SC8002B amp

- `esp_codec_dev` with `es8311_codec_new`, I2C address `ES8311_CODEC_DEFAULT_ADDR`
  (0x30 in 8-bit form, 0x18 in 7-bit form), `use_mclk = true`, DAC mode.
- I2S std Philips, 16-bit mono. Use `auto_clear` so underruns play silence.
- The amp has no PA pin in the codec config. SMIB drives IO1 itself so it can
  apply its warmup and linger timing.
- There is no DAC on the S3, so the E32R40T's LEDC tone approach does not carry over.

## Memory and flash

- Draw and rotate buffers must be internal DMA RAM (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`), not PSRAM.
- The 1 MB default app partition is about 90% used by SMIB, so
  `sdkconfig.defaults.esp32s3` selects `PARTITION_TABLE_SINGLE_APP_LARGE`.

## Troubleshooting

| Symptom | Check |
|---|---|
| UI upside down | `UI_ROTATION` `_90` ↔ `_270` in `display_st77922.c` |
| Whole image is a photo-negative | `esp_lcd_panel_invert_color()` passed `false`; this panel needs `true` |
| Red and blue swapped | MADCTL BGR bit; note the vendor `0x36` wins over `rgb_ele_order` |
| Screen stuck on power-on noise | nothing was ever flushed — check for a boot loop aborting before the `lv_timer_handler` loop (e.g. `nfc_init`) |
| Vertical garbage or shifted columns | rounder not applied, or rounding the wrong axis for the rotation |
| Touch not responding | boot log "touch not answering at 0x55": check the RST pulse and the I2C bus |
| Touch offset or mirrored | touch must report native portrait coordinates; do not rotate them yourself |
| No sound | I2C NACK from ES8311 at boot; amp EN (IO1) must be low while playing |

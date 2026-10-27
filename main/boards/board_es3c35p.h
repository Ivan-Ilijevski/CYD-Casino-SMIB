// Pins for the LCDWiki 3.5" ESP32-S3 board (ES3C35P): ST77922 QSPI LCD,
// touch + ES8311 codec on one I2C bus, I2S audio.
// Sources: lcdwiki.com/3.5inch_ESP32-S3_Display (schematic, IO table) and
// the xiaozhi-esp32 lcdwiki-es3c35p board (panel init, touch protocol).
#pragma once

// --- Display (ST77922, QSPI on SPI2/FSPI) ---
#define PIN_NUM_BK_LIGHT    41   // high = on
#define PIN_NUM_LCD_CS      10
#define PIN_NUM_LCD_SCK     12
#define PIN_NUM_LCD_D0      11
#define PIN_NUM_LCD_D1      13
#define PIN_NUM_LCD_D2      14
#define PIN_NUM_LCD_D3      9
// LCD reset is shared with EN, so no reset GPIO is driven
#define PIN_NUM_LCD_RST     -1

// --- Shared I2C bus: touch controller, ES8311 codec, expansion header ---
#define PIN_NUM_I2C_SDA     38
#define PIN_NUM_I2C_SCL     39

// --- Touch (built into the ST77922 TDDI; I2C address 0x55) ---
#define PIN_NUM_TOUCH_RST   48   // active low
#define PIN_NUM_TOUCH_INT   47   // unused: the driver polls

// --- NFC (RC522 on SPI3, wired to the microSD slot pins; SD is unused) ---
#define PIN_NUM_RC522_SCK   5    // SD CLK
#define PIN_NUM_RC522_MOSI  4    // SD CMD
#define PIN_NUM_RC522_MISO  6    // SD D0
#define PIN_NUM_RC522_CS    3    // SD D3
#define PIN_NUM_RC522_RST   2    // SD D2

// --- Audio (ES8311 codec over I2S, SC8002B amp) ---
#define PIN_NUM_AUDIO_EN    1    // amp enable, active LOW
#define PIN_NUM_I2S_MCLK    17
#define PIN_NUM_I2S_BCLK    18
#define PIN_NUM_I2S_LRCK    21
#define PIN_NUM_I2S_DOUT    15   // ESP -> codec DSDIN
#define PIN_NUM_I2S_DIN     16   // codec ASDOUT -> ESP (mic, unused)

// --- Display params (landscape; the panel is 320x480 portrait natively) ---
#define LCD_H_RES           480
#define LCD_V_RES           320

// --- USB serial link to the slot machine (UART0 through the CH340) ---
#define MUX_UART_NUM        0
#define MUX_UART_TX         43   // ESP32-S3 UART0 default pins; GPIO1/3 are
#define MUX_UART_RX         44   // the amp enable and the RC522 CS here
#define MUX_BAUD            115200

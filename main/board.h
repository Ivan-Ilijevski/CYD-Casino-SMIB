// Board definitions for the 4.0" ESP32-32E CYD (E32R40T) running as a SMIB.
#pragma once

#define SMIB_FW_VERSION     "0.1.0"
#define SMIB_DEVICE_ID      "smib-1"

// --- Display (ST7796, SPI2/HSPI) ---
#define PIN_NUM_BK_LIGHT    27
#define PIN_NUM_LCD_CS      15
#define PIN_NUM_LCD_DC      2
#define PIN_NUM_LCD_SCK     14
#define PIN_NUM_LCD_MOSI    13
#define PIN_NUM_LCD_MISO    12
// LCD reset is shared with EN per the board spec, so no reset GPIO is driven
#define PIN_NUM_LCD_RST     -1

// --- Touch (XPT2046, shares SPI2 with the LCD) ---
#define PIN_NUM_TOUCH_CS    33
#define PIN_NUM_TOUCH_IRQ   36

// --- NFC (RC522 on its own VSPI/SPI3 bus; IO21 is the documented external
// SPI device CS pin; IO25 is the RST line wired on the module) ---
#define PIN_NUM_RC522_SCK   18
#define PIN_NUM_RC522_MISO  19
#define PIN_NUM_RC522_MOSI  23
#define PIN_NUM_RC522_CS    21
#define PIN_NUM_RC522_RST   25

// --- Audio (onboard speaker amp) ---
#define PIN_NUM_AUDIO_EN    4    // amp enable, active LOW
#define PIN_NUM_AUDIO_DAC   26   // DAC channel 1 output

// --- Display params (landscape) ---
#define LCD_H_RES           480
#define LCD_V_RES           320

// --- USB serial link to the slot machine (UART0 through the CH340) ---
#define MUX_UART_NUM        0
#define MUX_BAUD            115200

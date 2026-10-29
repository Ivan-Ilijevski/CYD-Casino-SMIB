// Pins for the 4.0" ESP32-32E CYD (E32R40T): ST7796 SPI LCD, XPT2046
// resistive touch, LEDC tone into the speaker amp.
#pragma once

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

// --- Cabinet door switch (gates the admin panel; see drivers/door.h) ---
// IO32 is the I2C peripheral connector's SDA pin, unused here (the RC522 took
// IO25, that connector's SCL). It is on a 1.25mm connector with GND beside it
// and supports an internal pull-up, so a bare microswitch needs no resistor.
// -1 on a board with no door switch.
#define PIN_NUM_DOOR        32

// --- Display params (landscape) ---
#define LCD_H_RES           480
#define LCD_V_RES           320

// --- USB serial link to the slot machine (UART0 through the CH340) ---
#define MUX_UART_NUM        0
#define MUX_UART_TX         1    // ESP32 UART0 default pins
#define MUX_UART_RX         3
#define MUX_BAUD            115200

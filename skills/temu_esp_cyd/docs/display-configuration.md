# Display Configuration - ST7796 on ESP32 CYD

## Display Hardware Overview

### ST7796 Display Controller Specifications

- **Driver IC**: ST7796U (Sitronix)
- **Resolution**: 320 x 480 pixels
- **Color Depth**:
  - Maximum: 262K colors (18-bit RGB666)
  - Common: 65K colors (16-bit RGB565)
- **Interface**: 4-wire SPI
- **Panel Type**: TN TFT
- **View Direction**: 12 O'CLOCK

### Physical Display Properties

- **Active Area**: 55.68mm (W) x 83.52mm (H)
- **Pixel Pitch**: 0.174mm x 0.174mm
- **Aspect Ratio**: 2:3 (portrait orientation native)
- **Viewing Angle**: 12 O'CLOCK position

### Backlight System

- **Type**: 8x White LEDs
- **Typical Brightness**: 450 cd/m²
- **Actual Brightness**:
  - With touch screen: 370 cd/m²
  - Without touch screen: 472 cd/m²
- **Current Draw**: 142 mA
- **Control Pin**: IO27
  - HIGH = Backlight ON
  - LOW = Backlight OFF
  - PWM capable for brightness control

## SPI Connection Configuration

### Pin Assignments

```
ESP32 Pin    Function        Description
---------    --------        -----------
IO15         LCD_CS          Chip Select (active LOW)
IO2          LCD_DC          Data/Command select (HIGH=data, LOW=command)
IO14         LCD_SCK         SPI Clock
IO13         LCD_MOSI        Master Out Slave In (Data to LCD)
IO12         LCD_MISO        Master In Slave Out (Data from LCD)
EN           LCD_RST         Reset (active LOW, shared with ESP32 reset)
IO27         LCD_BL          Backlight Control (HIGH=on, LOW=off)
```

### SPI Bus Sharing

The LCD shares SPI bus lines with the resistive touch screen (XPT2046):

**Shared Lines:**
- SCK (IO14)
- MOSI (IO13)
- MISO (IO12)

**Separate Lines:**
- LCD CS: IO15
- Touch CS: IO33

**Important**: When accessing the LCD or touch, ensure correct chip select is asserted (LOW) and the other is de-asserted (HIGH).

## SPI Communication Settings

### Recommended SPI Configuration

```c
// SPI Speed
#define LCD_SPI_FREQUENCY   40000000  // 40 MHz (typical)
// Can go up to 80 MHz for faster updates

// SPI Mode
#define LCD_SPI_MODE        SPI_MODE0  // CPOL=0, CPHA=0

// Bit Order
#define LCD_BIT_ORDER       SPI_MSBFIRST

// Data Width
#define LCD_DATA_WIDTH      8  // 8-bit transfers
```

### SPI Transfer Characteristics

- **Maximum SPI Speed**: 80 MHz (per specification settings)
- **Recommended Speed**: 40 MHz (stable operation)
- **SPI Mode**: Mode 0 (CPOL=0, CPHA=0)
- **Bit Order**: MSB first
- **CS Active**: LOW
- **Data/Command Control**: Via separate DC pin (IO2)

## Display Initialization Sequence

### Power-On Sequence

1. **Apply Power** (5V via Type-C)
2. **Wait**: 5-10ms for power stabilization
3. **Hardware Reset**:
   - Pull EN (reset) LOW for 10ms
   - Pull EN HIGH
   - Wait 120ms for ST7796 initialization
4. **Configure Backlight**: Set IO27 HIGH to enable backlight
5. **Send Initialization Commands** via SPI

### ST7796 Initialization Commands

Based on 4-line SPI mode, 320x480 resolution:

```c
// Basic initialization sequence
// Note: Actual values should be confirmed with working demo code

// Sleep Out
Command: 0x11
Wait: 120ms

// Interface Pixel Format
Command: 0x3A
Data: 0x55  // 16-bit/pixel (RGB565)
// or
Data: 0x66  // 18-bit/pixel (RGB666)

// Memory Access Control (rotation/mirroring)
Command: 0x36
Data: 0x48  // Example: Portrait mode
// Bit layout:
//   MY (bit 7): Row address order
//   MX (bit 6): Column address order
//   MV (bit 5): Row/Column exchange
//   ML (bit 4): Vertical refresh order
//   BGR (bit 3): RGB/BGR order
//   MH (bit 2): Horizontal refresh order

// Display Inversion
Command: 0x21  // Display Inversion ON
// or
Command: 0x20  // Display Inversion OFF

// Display ON
Command: 0x29
Wait: 10ms
```

### Common Memory Access Control (0x36) Values

Different orientations can be achieved by setting register 0x36:

```
Portrait (0°):       0x48 or 0x40
Landscape (90°):     0x28 or 0x20
Portrait (180°):     0x88 or 0x80
Landscape (270°):    0xE8 or 0xE0

Note: Exact values depend on panel mounting and BGR/RGB order
Common variations:
- Add 0x08 for BGR color order
- Without 0x08 for RGB color order
```

## Pixel Format Configuration

### RGB565 Format (16-bit, 65K colors)

Most common format for ESP32 displays:

```c
// Set pixel format to RGB565
Command: 0x3A (Interface Pixel Format)
Data: 0x55

// Pixel structure (16 bits):
// [15:11] Red (5 bits)
// [10:5]  Green (6 bits)
// [4:0]   Blue (5 bits)

// Example color values:
#define COLOR_BLACK   0x0000
#define COLOR_WHITE   0xFFFF
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
```

### RGB666 Format (18-bit, 262K colors)

Higher color depth but requires more memory:

```c
// Set pixel format to RGB666
Command: 0x3A (Interface Pixel Format)
Data: 0x66

// Pixel structure (18 bits in 3 bytes):
// Byte 1: [7:2] Red (6 bits)
// Byte 2: [7:2] Green (6 bits)
// Byte 3: [7:2] Blue (6 bits)
// Lower 2 bits of each byte are ignored
```

## Display Window/Region Setting

### Column and Page Address Set

To draw to a specific region:

```c
// Set Column Address (X coordinates)
Command: 0x2A (CASET)
Data[0]: X_start >> 8     (high byte)
Data[1]: X_start & 0xFF   (low byte)
Data[2]: X_end >> 8       (high byte)
Data[3]: X_end & 0xFF     (low byte)

// Set Page Address (Y coordinates)
Command: 0x2B (PASET)
Data[0]: Y_start >> 8     (high byte)
Data[1]: Y_start & 0xFF   (low byte)
Data[2]: Y_end >> 8       (high byte)
Data[3]: Y_end & 0xFF     (low byte)

// Write to Memory
Command: 0x2C (RAMWR)
// Then send pixel data
```

### Coordinate System

```
Default Portrait Orientation (0x48):
  (0,0) -------- (319,0)
    |              |
    |    Display   |
    |   320x480    |
    |              |
  (0,479) ------ (319,479)
```

## Backlight Control

### Simple On/Off Control

```c
// Turn backlight ON
digitalWrite(IO27, HIGH);

// Turn backlight OFF
digitalWrite(IO27, LOW);
```

### PWM Brightness Control

```c
// Using ESP32 LEDC (PWM)
#define BL_PWM_CHANNEL  0
#define BL_PWM_FREQ     5000  // 5 kHz
#define BL_PWM_RES      8     // 8-bit resolution (0-255)

void setup_backlight_pwm() {
    ledcSetup(BL_PWM_CHANNEL, BL_PWM_FREQ, BL_PWM_RES);
    ledcAttachPin(IO27, BL_PWM_CHANNEL);
}

// Set brightness (0-255)
void set_brightness(uint8_t brightness) {
    ledcWrite(BL_PWM_CHANNEL, brightness);
}

// Example usage:
set_brightness(255);  // Full brightness
set_brightness(128);  // 50% brightness
set_brightness(0);    // Backlight off
```

## Performance Optimization

### SPI Speed Optimization

```c
// For maximum performance with stable operation:
#define LCD_SPI_SPEED   80000000  // 80 MHz

// For reliable operation:
#define LCD_SPI_SPEED   40000000  // 40 MHz

// For debugging or problematic setups:
#define LCD_SPI_SPEED   20000000  // 20 MHz
```

### DMA Transfer

Use DMA for large pixel transfers to reduce CPU load:

```c
// ESP32 SPI DMA configuration
#define SPI_DMA_CHANNEL  1  // or 2

// When initializing SPI, enable DMA:
spi_bus_config_t buscfg = {
    .mosi_io_num = 13,
    .miso_io_num = 12,
    .sclk_io_num = 14,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 320*480*2  // Full screen buffer
};

spi_bus_initialize(HSPI_HOST, &buscfg, SPI_DMA_CHANNEL);
```

### Frame Buffer Strategies

**Full Frame Buffer** (320x480x2 = 307,200 bytes):
- Requires significant RAM
- Smoothest updates
- Not practical on ESP32 without external RAM

**Partial Frame Buffer** (e.g., 320x40x2 = 25,600 bytes):
- Update screen in horizontal strips
- Good balance of speed and memory
- Recommended approach

**Direct Drawing** (no frame buffer):
- Minimal RAM usage
- Slower for complex graphics
- Good for simple displays

## Common Display Functions

### Fill Screen with Color

```c
void fillScreen(uint16_t color) {
    setAddrWindow(0, 0, 319, 479);  // Full screen

    // Send color for all pixels
    for (uint32_t i = 0; i < 320*480; i++) {
        writePixel(color);  // RGB565 format
    }
}
```

### Draw Rectangle

```c
void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    setAddrWindow(x, y, x+w-1, y+h-1);

    for (uint32_t i = 0; i < w*h; i++) {
        writePixel(color);
    }
}
```

### Draw Pixel

```c
void drawPixel(int16_t x, int16_t y, uint16_t color) {
    setAddrWindow(x, y, x, y);
    writePixel(color);
}
```

## Display Rotation

### Setting Display Orientation

The 0x36 command controls rotation and mirroring:

```c
void setRotation(uint8_t rotation) {
    writeCommand(0x36);  // Memory Access Control

    switch (rotation % 4) {
        case 0:  // Portrait
            writeData(0x48);
            width = 320;
            height = 480;
            break;
        case 1:  // Landscape
            writeData(0x28);
            width = 480;
            height = 320;
            break;
        case 2:  // Portrait (inverted)
            writeData(0x88);
            width = 320;
            height = 480;
            break;
        case 3:  // Landscape (inverted)
            writeData(0xE8);
            width = 480;
            height = 320;
            break;
    }
}
```

## Troubleshooting Display Issues

### Display Not Showing Anything

1. **Check power**: Ensure 5V is supplied via Type-C
2. **Check backlight**: Set IO27 HIGH
3. **Verify SPI connections**: Ensure pins IO15, IO2, IO14, IO13 are connected
4. **Check initialization**: Ensure proper reset sequence
5. **Verify SPI speed**: Try reducing to 20MHz for testing

### Display Shows Garbage/Random Pixels

1. **Check SPI mode**: Should be SPI_MODE0
2. **Verify bit order**: Should be MSB first
3. **Check pixel format**: Ensure 0x3A command sent with correct value
4. **Reduce SPI speed**: Try 20-40 MHz
5. **Check DMA buffer size**: Ensure adequate allocation

### Display Colors Wrong

1. **BGR vs RGB**: Toggle bit 3 in 0x36 command (add or remove 0x08)
2. **Check pixel format**: Verify RGB565 vs RGB666 setting
3. **Verify color encoding**: Ensure color values match format

### Display Flickering

1. **Check backlight PWM frequency**: Should be 5kHz or higher
2. **Verify power supply**: Ensure stable 5V input
3. **Check SPI speed**: May be too fast, reduce to 40MHz
4. **Reduce update rate**: Don't refresh entire screen too frequently

### Backlight Won't Turn On

1. **Check IO27 control**: Set HIGH to enable
2. **Verify power**: Backlight draws 142mA, ensure adequate power
3. **Check initialization**: Backlight should be enabled in setup
4. **Measure voltage**: Should be ~5V at backlight circuit

## Recommended Arduino/ESP-IDF Libraries

### Arduino Libraries

1. **TFT_eSPI** - Highly optimized, supports ST7796
   - Configure User_Setup.h for this board

2. **Adafruit_GFX** + **Adafruit_ST7796**
   - More portable, easier to use

3. **LovyanGFX** - Very fast, good LVGL integration

### ESP-IDF Components

1. **ESP_LCD** - Official ESP-IDF LCD driver
2. **LVGL** - Graphics library with ST7796 support

## Power Consumption

- **Display Only** (backlight on): ~230 mA @ 5V
- **Backlight Current**: 142 mA
- **Display Logic**: ~88 mA
- **Total Module** (display + ESP32): Up to 580 mA with all features active

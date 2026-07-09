---
name: temu_esp_cyd
description: Expert assistant for the 4.0" ESP32 CYD (Cheap Yellow Display) board with ST7796 driver
---

# ESP32 CYD (Cheap Yellow Display) Expert Skill

You are an expert assistant for the **4.0" ESP32-32E CYD board** (models E32R40T with touch, E32N40T without touch) featuring the ST7796S display driver.

## Hardware Specifications

### Display Module
- **Size**: 4.0 inch TFT LCD
- **Resolution**: 320×480 pixels (portrait orientation)
- **Driver IC**: ST7796S
- **Color Support**: 262K max (RGB666), 65K common (RGB565)
- **Brightness**: 450 cd/m² (typical)
- **Backlight**: 8× White LEDs, controlled via GPIO27 (active high)

### ESP32-32E Controller
- **CPU**: ESP32-D0WD-V3 (Xtensa dual-core 32-bit LX6)
- **Max Clock**: 240 MHz
- **Memory**: 448KB ROM, 520KB SRAM, 16KB RTC SRAM, 4MB Flash
- **Connectivity**: 2.4GHz WiFi (802.11b/g/n), Bluetooth V4.2 BR/EDR + BLE
- **Operating Voltage**: 3.0-3.6V (5V via USB-C)

### Touch Screen (E32R40T only)
- **Type**: Resistive touch screen
- **Driver IC**: XPT2046
- **Interface**: SPI (shared with LCD)

## Pin Definitions (Critical)

### LCD Display Pins
```c
#define PIN_NUM_LCD_CS      15    // Chip select (active low)
#define PIN_NUM_LCD_DC      2     // Data/Command (high=data, low=command)
#define PIN_NUM_LCD_SCK     14    // SPI clock (shared with touch & SD)
#define PIN_NUM_LCD_MOSI    13    // SPI MOSI (shared with touch & SD)
#define PIN_NUM_LCD_MISO    12    // SPI MISO (shared with touch & SD)
#define PIN_NUM_LCD_RST     -1    // Reset shared with ESP32 EN pin
#define PIN_NUM_BK_LIGHT    27    // Backlight control (high=on)
```

### Touch Screen Pins (E32R40T)
```c
#define PIN_NUM_TOUCH_CS    33    // Touch chip select (active low)
#define PIN_NUM_TOUCH_IRQ   36    // Touch interrupt (low when touched)
// SPI pins shared with LCD (14, 13, 12)
```

### Other Peripherals
- **RGB LED**: IO22 (Red), IO16 (Green), IO17 (Blue) - common anode, active low
- **MicroSD Card**: IO5 (CS), IO23 (MOSI), IO18 (SCK), IO19 (MISO)
- **Audio**: IO4 (Enable, active low), IO26 (DAC output)
- **Battery ADC**: IO34 (voltage monitoring)
- **I2C**: IO25 (SCL), IO32 (SDA)

## Display Configuration

### ST7796 Initialization
- **SPI Speed**: Up to 80MHz (recommend starting at 40MHz)
- **SPI Mode**: Mode 0 (CPOL=0, CPHA=0)
- **Color Format**: RGB565 (16-bit) is most common
- **Byte Order**: May require RGB565_SWAPPED format in LVGL

### Display Orientation
- **Native Resolution**: 320×480 (portrait)
- **Rotation**: Configurable 0°, 90°, 180°, 270°
- **Mirror Settings**: X and Y mirroring available
- **Swap XY**: Can swap axes for landscape mode

### Common Display Issues & Solutions

#### Issue: Garbled or Corrupted Text
**Cause**: Byte order mismatch in RGB565 format
**Solution**: Use `LV_COLOR_FORMAT_RGB565_SWAPPED` instead of `LV_COLOR_FORMAT_RGB565`

#### Issue: Mirrored Display
**Cause**: Incorrect mirror settings
**Solution**: For standard CYD orientation, use:
```c
esp_lcd_panel_mirror(panel_handle, true, false);  // mirror X, not Y
```

#### Issue: Horizontal Gap/Split in Display
**Possible Causes**:
1. MADCTL register initialization issue in ST7796 driver
2. Partial buffer rendering alignment problem
3. SPI timing issues

**Solutions to Try**:
1. Increase buffer size (try 60+ lines instead of 40)
2. Adjust SPI clock speed (try 20MHz, 40MHz, 60MHz)
3. Check if custom ST7796 initialization sequence is needed
4. Verify swap_xy settings
5. Try different gap offset values using `esp_lcd_panel_set_gap()`

#### Issue: Colors Incorrect
**Cause**: RGB vs BGR byte order
**Solution**: Toggle color space between `ESP_LCD_COLOR_SPACE_RGB` and `ESP_LCD_COLOR_SPACE_BGR`

## Power Specifications

### Current Consumption
- **ESP32 reset**: 40mA
- **Display only**: 230mA
- **Backlight**: 142mA
- **Display + peripherals**: Up to 580mA
- **Total Power**: ~1.15W (display only)

### Battery Charging (if connected)
- **Charging IC**: TP4054
- **Max Current**: 500mA (actual ~290mA)
- **Battery Type**: 3.7V polymer lithium
- **Max Temperature**: 62°C during charging

## Software Development

### ESP-IDF with LVGL Setup
**Required Libraries**:
- `esp_lcd` (ESP-IDF LCD driver)
- `lvgl` (v8.x or v9.x, v9.x preferred)
- `esp_lcd_st7796` component (from ESP Component Registry)

**Recommended Configuration**:
```c
// Display buffer size
#define BUFFER_LINES 40-60  // Start with 40, increase if gaps appear

// SPI configuration
.pclk_hz = 40 * 1000 * 1000,  // 40MHz, can try up to 60MHz
.spi_mode = 0,
.trans_queue_depth = 10,

// Panel configuration
.color_space = ESP_LCD_COLOR_SPACE_BGR,  // Try BGR first
.bits_per_pixel = 16,  // RGB565

// LVGL display configuration
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
```

### Arduino IDE Setup (Alternative)
**Libraries Needed**:
- TFT_eSPI (with custom User_Setup.h)
- LVGL v8.x (NOT v9.x for Arduino)
- XPT2046 (for touch)

**Important**: Use modified ST7796_Init.h file provided in documentation package.

### Touch Screen Calibration (E32R40T)
Touch screen requires calibration. After running calibration example, copy calibration data:
```c
uint16_t calData[5] = { 252, 3653, 243, 3485, 7 };  // Example values
tft.setTouch(calData);
```

## Common Patterns for CYD Development

### Minimal Display Initialization
```c
// 1. Enable backlight
gpio_set_level(PIN_NUM_BK_LIGHT, 1);

// 2. Initialize SPI bus
spi_bus_config_t buscfg = {
    .sclk_io_num = PIN_NUM_LCD_SCK,
    .mosi_io_num = PIN_NUM_LCD_MOSI,
    .miso_io_num = PIN_NUM_LCD_MISO,
    .quadwp_io_num = -1,
    .quadhd_io_num = -1,
    .max_transfer_sz = 320 * 40 * 2 + 8,
};
ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO));

// 3. Create panel IO
esp_lcd_panel_io_spi_config_t io_config = {
    .dc_gpio_num = PIN_NUM_LCD_DC,
    .cs_gpio_num = PIN_NUM_LCD_CS,
    .pclk_hz = 40 * 1000 * 1000,
    .lcd_cmd_bits = 8,
    .lcd_param_bits = 8,
    .spi_mode = 0,
    .trans_queue_depth = 10,
};
ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io_handle));

// 4. Create ST7796 panel
esp_lcd_panel_dev_config_t panel_config = {
    .reset_gpio_num = -1,  // Shared with EN
    .color_space = ESP_LCD_COLOR_SPACE_BGR,
    .bits_per_pixel = 16,
};
ESP_ERROR_CHECK(esp_lcd_new_panel_st7796(io_handle, &panel_config, &panel_handle));

// 5. Reset and initialize
ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));

// 6. Configure orientation
ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel_handle, true, false));

// 7. Turn on display
ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel_handle, true));
```

### LVGL Integration
```c
lv_init();

// Allocate DMA-capable buffer
lv_color_t *buf = heap_caps_malloc(320 * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);

lv_disp_t *disp = lv_display_create(320, 480);
lv_display_set_flush_cb(disp, lvgl_flush_cb);
lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565_SWAPPED);
lv_display_set_buffers(disp, buf, NULL, 320 * 40 * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
```

## Debugging Tips

### Display Shows Nothing
1. Check backlight is enabled (GPIO27 = HIGH)
2. Verify SPI pins are correct
3. Check power supply can provide 300mA+
4. Verify ST7796 component is properly included

### Text Appears Garbled
1. Try RGB565_SWAPPED color format
2. Check byte order in color_space setting
3. Verify SPI clock speed isn't too high
4. Ensure DMA buffer is properly allocated

### Display Has Horizontal Gap
1. Increase buffer size from 40 to 60+ lines
2. Try different SPI speeds (20MHz, 40MHz, 60MHz)
3. Experiment with swap_xy and mirror settings
4. Check if gap offset settings are needed

### Touch Not Working (E32R40T)
1. Verify touch CS pin is GPIO33
2. Check IRQ pin is GPIO36 (input only)
3. Ensure SPI bus is shared correctly with LCD
4. Run calibration example first

## Safety Warnings

1. **Hot Components**: LDO voltage regulator and battery charge IC can reach high temperatures during operation
2. **Battery Charging**: Ensure proper ventilation, max temperature 62°C
3. **Current Draw**: Total current can exceed 500mA with all peripherals active
4. **Static Discharge**: Handle board with proper ESD precautions

## Skill Behavior

When the user asks for help with their CYD board:

1. **Always verify pin definitions** against the correct specifications above
2. **Check display configuration** matches the ST7796 requirements
3. **Suggest troubleshooting steps** based on common issues listed
4. **Provide code examples** that follow the patterns documented here
5. **Reference power requirements** if issues may be power-related
6. **Consider byte order issues** as the primary cause of display problems

When troubleshooting display issues, work through solutions systematically:
- First check byte order/color format
- Then verify orientation settings
- Then adjust buffer sizes
- Finally try SPI speed adjustments

Always provide complete, working code examples based on the patterns documented above.

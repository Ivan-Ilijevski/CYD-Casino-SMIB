# ESP32 CYD (Cheap Yellow Display) Documentation

Complete technical documentation for the ESP32-32E E32R40T and E32N40T 4.0" display modules.

## Product Information

**Models:**
- **E32R40T**: 4.0" display with resistive touch screen
- **E32N40T**: 4.0" display without touch screen

**Manufacturer:** ShenZhen QDtech electronic technology Co., LTD.
**Website:** www.lcdwiki.com
**Product Code:** CR2025-MI3275

## Documentation Files

### 1. [Hardware Specifications](hardware-specs.md)
Complete hardware specifications including:
- ESP32-32E module specifications
- Display (ST7796) detailed specs
- Touch screen specifications (E32R40T)
- Physical dimensions and mounting
- Electrical characteristics
- Audio, battery, storage, and peripheral specs
- Temperature ratings and operating conditions

### 2. [GPIO Pinout Reference](pinout-reference.md)
Comprehensive GPIO pin assignments:
- Complete pin allocation table for all peripherals
- Display, touch, LED, button, and interface pins
- SPI bus sharing configuration
- Available GPIO pins for custom use
- Strapping pin considerations
- Connector pinouts (Type-C, battery, speaker, etc.)
- Programming interface details
- Power pin information

### 3. [Display Configuration](display-configuration.md)
ST7796 display driver configuration:
- SPI connection and communication settings
- Display initialization sequence
- Pixel format configuration (RGB565/RGB666)
- Backlight control and PWM dimming
- Display rotation and coordinate system
- Performance optimization techniques
- Troubleshooting display issues
- Recommended libraries (TFT_eSPI, Adafruit, LovyanGFX)

### 4. [Troubleshooting Guide](troubleshooting.md)
Solutions for common issues:
- Power and startup problems
- Display issues (black screen, wrong colors, flickering)
- Touch screen problems (E32R40T)
- Programming and flashing issues
- Driver installation problems
- Battery and charging issues
- Peripheral problems (SD card, speaker, WiFi)
- Safety precautions and warnings

### 5. [Programming Examples](programming-examples.md)
Code examples and setup:
- Arduino IDE setup and configuration
- Basic examples (backlight, LEDs, buttons, PWM)
- Display programming with TFT_eSPI
- Touch screen calibration and usage
- WiFi connectivity examples
- SD card read/write operations
- Battery voltage monitoring
- Complete working code samples

### 6. [Power and Battery Management](power-battery.md)
Power system documentation:
- USB Type-C power specifications
- Battery specifications and safety
- Charging system details
- Battery voltage monitoring
- Runtime estimates
- Power optimization strategies
- Deep sleep and light sleep modes
- Power troubleshooting

## Quick Start

### Hardware Setup
1. Connect USB Type-C cable to computer (must support data + power)
2. Install CH340C driver (included in package)
3. Verify COM port appears in Device Manager

### First Program
1. Use flash download tool from `8-快速使用_Quick_Start/Flash_Download/`
2. Select demo .bin file from `8-快速使用_Quick_Start/bin/`
3. Configure: ESP32, 80MHz SPI, DIO mode, address 0x0
4. Flash and press RESET to run

### Arduino Development
1. Install Arduino IDE (1.8.x or 2.x)
2. Add ESP32 board support via Board Manager
3. Configure for "ESP32 Dev Module", 4MB flash
4. Install TFT_eSPI library
5. Configure User_Setup.h for this board (see programming examples)
6. Compile and upload via Type-C

## Key Technical Specifications

| Feature | Specification |
|---------|--------------|
| **Display** | 4.0" TFT, 320x480, ST7796 driver |
| **MCU** | ESP32-D0WD-V3, dual-core @ 240MHz |
| **Memory** | 4MB Flash, 520KB SRAM |
| **Touch** | XPT2046 resistive (E32R40T only) |
| **WiFi** | 2.4GHz 802.11b/g/n |
| **Bluetooth** | BT 4.2 BR/EDR + BLE |
| **Power** | 5V via USB-C or 3.7V LiPo battery |
| **Interfaces** | SPI, I2C, UART, SD card, Audio |

## Pin Quick Reference

### Critical Display Pins
```
IO15 - LCD_CS    IO27 - LCD_BL (Backlight)
IO2  - LCD_DC    IO14 - LCD_SCK
IO13 - LCD_MOSI  IO12 - LCD_MISO
EN   - LCD_RST
```

### Touch Pins (E32R40T)
```
IO33 - TOUCH_CS
IO36 - TOUCH_IRQ
(Shares SPI: IO14/13/12)
```

### User Interface
```
IO22 - Red LED    IO0 - BOOT Button
IO16 - Green LED  EN  - RESET Button
IO17 - Blue LED
```

## Development Resources

### Included in Package
- Demo programs (Arduino, ESP-IDF, MicroPython)
- Flash download tools
- USB serial drivers (CH340C)
- Datasheets for all components
- Schematic diagrams
- Example code with LVGL integration

### External Resources
- Arduino Library: TFT_eSPI, Adafruit_GFX, LovyanGFX
- ESP-IDF: Official ESP32 development framework
- LVGL: Graphics library for embedded displays
- Community forums and examples

## Safety Warnings

### Battery Safety
- **ONLY use 3.7V lithium polymer batteries**
- **Check polarity before connecting** - incorrect polarity causes fire risk
- **Monitor charging temperature** - should not exceed 62°C
- **Replace swollen or damaged batteries immediately**
- **Do not leave charging unattended** for extended periods

### Electrical Safety
- **Use 5V power supply only** - higher voltages damage the board
- **Ensure adequate current** - minimum 500mA, recommended 1A
- **Avoid ESD** - use proper grounding when handling
- **Do not short circuit pins**

### Display Care
- **Do not press hard on display** - may damage LCD or touch screen
- **Do not scratch with hard objects**
- **Clean with soft, dry cloth only**
- **Avoid extreme temperatures** during operation

## Common Issues and Solutions

| Problem | Solution |
|---------|----------|
| Black screen | Check backlight (IO27 HIGH), verify power supply |
| Can't upload code | Install CH340C driver, verify COM port, try manual boot mode |
| Wrong colors | Toggle BGR/RGB in 0x36 command |
| Touch not working | Verify touch initialization, run calibration |
| WiFi won't connect | Check 2.4GHz band, verify credentials, ensure adequate power |
| Battery not charging | Verify polarity, check battery type (3.7V LiPo only) |

See [Troubleshooting Guide](troubleshooting.md) for detailed solutions.

## File Structure

```
docs/
├── README.md (this file)
├── hardware-specs.md
├── pinout-reference.md
├── display-configuration.md
├── troubleshooting.md
├── programming-examples.md
└── power-battery.md
```

## Source Documentation

This documentation was extracted from official sources:
- Product Specification (E32R40T_E32N40T_Specification_V1.0.pdf)
- Quick Start Manual (4.0inch_ESP32-32E_E32R40T_E32N40T_Quick_Start_Manual.pdf)
- Component datasheets (ST7796, XPT2046, ESP32-32E, etc.)
- Demo code and examples included with the product

## Revision History

- **V1.0** (2024-02-23): Initial comprehensive documentation
  - Extracted from official PDF documentation
  - Organized into topic-specific markdown files
  - Added code examples and practical guidance

## License and Disclaimer

This documentation is provided for informational purposes. While every effort has been made to ensure accuracy:

- Always refer to official manufacturer documentation for critical specifications
- Test thoroughly before deploying in production applications
- The author assumes no liability for damages resulting from use of this information
- Battery and electrical safety warnings must be strictly observed

## Contributing

Found an error or have improvements? Contributions welcome:
1. Document the issue with clear description and reference to official docs
2. Provide corrected information with source citation
3. Include testing results if applicable

## Support

For official support:
- Visit: www.lcdwiki.com
- Refer to documentation package included with product
- Check community forums for similar issues

For documentation issues:
- Verify against official PDF documentation
- Note the discrepancy with page references
- Provide correct information with source

---

**Document Version:** 1.0
**Last Updated:** 2024-02-23
**Applies To:** E32R40T and E32N40T V1.0
**Product Revision:** V1.0 (April 2025 release)

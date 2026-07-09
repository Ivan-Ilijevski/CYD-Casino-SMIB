# Hardware Specifications - ESP32 CYD E32R40T/E32N40T

## Product Overview

The E32R40T and E32N40T are 4.0-inch display modules featuring ESP32-32E module main control.
- **E32R40T**: Has resistive touch screen
- **E32N40T**: No touch screen

**Manufacturer**: ShenZhen QDtech electronic technology Co., LTD.
**Product Code**: CR2025-MI3275
**Website**: www.lcdwiki.com

## ESP32-32E Main Control

### CPU Specifications
- **Module**: ESP32-32E
- **CPU**: ESP32-D0WD-V3, Xtensa dual-core 32-bit LX6 microprocessor
- **Max Frequency**: 240 MHz
- **Memory**:
  - 48KB ROM
  - 520KB SRAM
  - 16KB RTC SRAM
  - 4MB external QSPI Flash
- **Operating Voltage**: 3.0-3.6V
- **Operating Temperature**: -40°C to +85°C

### Wireless Connectivity
- **WiFi**: 2.4GHz, 802.11b/g/n modes
- **Bluetooth**: Bluetooth V4.2 BR/EDR and Bluetooth LE standards

## Display (TFT) Specifications

### Panel Information
- **Panel Size**: 4.0 inch
- **Panel Type**: TN TFT
- **Resolution**: 320 x 480 pixels (RGB)
- **Active Area**: 55.68mm (W) x 83.52mm (H)
- **Pixel Size**: 0.174mm (H) x 0.174mm (W)

### Display Capabilities
- **Driver IC**: ST7796U
- **Color Depth**:
  - Maximum: 262K colors (RGB666)
  - Common: 65K colors (RGB565)
- **Display Interface**: 4-Line SPI (connected to ESP32)
- **View Angle**: 12 O'CLOCK

### Backlight
- **Type**: White LED x 8
- **Brightness (Typical)**: 450 cd/m²
- **Brightness (Actual)**:
  - With touch screen: 370 cd/m²
  - Without touch screen: 472 cd/m²
- **Backlight Current**: 142 mA
- **Control**: IO27 (high level = on, low level = off)
- **PWM Dimming**: Supported via backlight control pin

### Display Temperature Range
- **Operation**: -10°C to +60°C
- **Storage**: -20°C to +70°C

### Display Outline Dimensions
- **TFT Size**: 60.88±0.2mm (W) x 94.57±0.2mm (H) x 2.5±0.1mm (D)
  - Excluding cables and adhesive backing

## Touch Screen Specifications (E32R40T only)

### Touch Panel
- **Type**: Resistive touch screen
- **Size**: 4.0 inch
- **Driver IC**: XPT2046
- **Material**: ITO film + ITO glass
- **Communication Interface**: SPI

### Touch Areas
- **Effective Touch Area**: 56.28mm (W) x 84.32mm (H)
- **Visual Area**: 56.88mm (W) x 85.22mm (H)

### Touch Temperature Range
- **Operation**: -10°C to +60°C
- **Storage**: -20°C to +70°C

### Touch Outline Dimensions
- **Touch Screen Size**: 60.48±0.2mm (W) x 93.87±0.2mm (H) x 1.05±0.1mm (D)
  - Excluding cables and adhesive backing

## Module Dimensions

### Overall Module Size
- **E32R40T (with touch)**: 60.88mm (W) x 111.11mm (H) x 5.65mm (D)
- **E32N40T (no touch)**: 60.88mm (W) x 111.11mm (H) x 4.60mm (D)

### PCB Dimensions
- **Width**: 60.88mm
- **Height**: 111.11mm
- **Hole Spacing**:
  - Vertical: 104.11mm
  - Horizontal: 53.28mm

### Mounting Holes
- **Quantity**: 4
- **Hole Diameter**: 3.20mm
- **Corner Radius**: R3.50
- **Standoff Diameter**: 5.60mm

## Electrical Specifications

### Power Supply
- **Working Voltage**: 5.0V
- **Power Supply Interface**: TYPE-C
- **Total Power**: 1.15W (display only)

### Current Consumption
- **ESP32 Reset**: 40 mA
- **Display Only**: 230 mA
- **Display + Speaker + Battery Charging**: 580 mA (maximum)

## Audio

### Speaker Specifications
- **Interface**: 1.25mm 2P connector
- **Power Amplifier IC**: FM8002E
- **Maximum Power**:
  - 1.5W @ 8Ω
  - 2W @ 4Ω
- **Audio Control**:
  - Enable Pin: IO4 (low = enable, high = disable)
  - DAC Output: IO26

## Battery System

### Battery Connector
- **Interface**: 1.25mm 2P connector
- **Battery Type**: 3.7V polymer lithium battery
- **Polarity**: Note positive and negative terminals

### Charging Specifications
- **Charging Voltage**:
  - Range: 4.2V to 6.5V
  - Typical: 5V
- **Charging Current**:
  - Maximum: 500 mA
  - Actual: 290 mA
- **Charging Saturation Voltage**: 4.24V
- **Charging Temperature**: Maximum 62°C (module)
- **Charge Management IC**: QD3958

### Battery Monitoring
- **Voltage Reading**: IO34 (ADC input)

## Storage

### MicroSD Card Slot
- **Interface**: Standard MicroSD card slot
- **Protocol**: SPI
- **Chip Select**: IO5
- **Shared SPI Pins**:
  - MOSI: IO23
  - CLK: IO18
  - MISO: IO19

## RGB LED Indicators

### LED Configuration
- **Type**: Common anode RGB LED
- **Control**:
  - Red LED: IO22 (low = on, high = off)
  - Green LED: IO16 (low = on, high = off)
  - Blue LED: IO17 (low = on, high = off)

## Buttons

### BOOT Button
- **Pin**: IO0
- **Function**: Download mode select
- **Usage**:
  - Press and hold to power on, release to enter download mode
  - Can be used as general purpose button when not downloading

### RESET Button
- **Pin**: EN (shared with LCD reset)
- **Function**: Reset ESP32 and LCD (low level reset)

## Communication Interfaces

### USB-to-Serial
- **Chip**: CH340C
- **Interface**: TYPE-C connector
- **Auto-Download**: Supported (one-click download circuit)

### UART Serial Port
- **Interface**: 1.25mm 4P connector
- **Pins**:
  - RXD0: IO3
  - TXD0: IO1
- **Note**: Can be used as ordinary GPIO if serial not needed

### SPI Peripheral Interface
- **Interface**: 1.25mm 4P connector
- **Chip Select**: IO21 (low level active)
- **Shared Pins** (with MicroSD):
  - CLK: IO18
  - MISO: IO19
  - MOSI: IO23
- **Note**: Can be used as ordinary GPIO if not using SPI devices

### I2C Peripheral Interface
- **Interface**: 1.25mm 4P connector
- **Pins**:
  - SCL: IO25
  - SDA: IO32
- **Note**: Can be used as ordinary GPIO if not using I2C devices

### Expansion Input Pins
- **Interface**: 1.25mm 2P connector
- **Pins**: IO35, IO39
- **Type**: Input-only pins

## Weight
- **E32R40T** (with touch, including package): 130g
- **E32N40T** (no touch, including package): 119g

## Included Accessories
- **Resistive Stylus** (E32R40T only): 87mm length, 5mm diameter
- **Type-C Data/Power Cable**: 1m length

## Operating Conditions

### Temperature Ranges
- **Module Operation**: -10°C to +60°C
- **ESP32 Operation**: -40°C to +85°C
- **Display Operation**: -10°C to +60°C
- **Touch Operation**: -10°C to +60°C (E32R40T)

### Storage Temperature
- **Module**: -20°C to +70°C

## Quality Standards
- Multiple aging tests
- Meets military-level standards
- Supports long-term stable operation

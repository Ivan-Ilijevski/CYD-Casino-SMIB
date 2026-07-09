# GPIO Pinout Reference - ESP32 CYD E32R40T/E32N40T

## Complete Pin Allocation Table

| Device | ESP32 Pin | Direction | Function Description |
|--------|-----------|-----------|---------------------|
| **LCD Display (ST7796)** |
| LCD CS | IO15 | Output | LCD chip select (active low) |
| LCD DC | IO2 | Output | Command/Data select (HIGH=data, LOW=command) |
| LCD SCK | IO14 | Output | SPI clock (shared with touch) |
| LCD MOSI | IO13 | Output | SPI data out (shared with touch) |
| LCD MISO | IO12 | Input | SPI data in (shared with touch) |
| LCD RST | EN | Output | LCD reset (active low, shared with ESP32 reset) |
| LCD BL | IO27 | Output | Backlight control (HIGH=on, LOW=off, PWM capable) |
| **Resistive Touch (XPT2046)** (E32R40T only) |
| Touch CS | IO33 | Output | Touch chip select (active low) |
| Touch IRQ | IO36 | Input | Touch interrupt (low when touched) |
| Touch SCK | IO14 | Output | SPI clock (shared with LCD) |
| Touch MOSI | IO13 | Output | SPI data out (shared with LCD) |
| Touch MISO | IO12 | Input | SPI data in (shared with LCD) |
| **RGB LED** |
| Red LED | IO22 | Output | Common anode (LOW=on, HIGH=off) |
| Green LED | IO16 | Output | Common anode (LOW=on, HIGH=off) |
| Blue LED | IO17 | Output | Common anode (LOW=on, HIGH=off) |
| **MicroSD Card** |
| SD CS | IO5 | Output | SD card chip select (active low) |
| SD MOSI | IO23 | Output | SPI data out (shared with SPI peripheral) |
| SD CLK | IO18 | Output | SPI clock (shared with SPI peripheral) |
| SD MISO | IO19 | Input | SPI data in (shared with SPI peripheral) |
| **Audio** |
| Audio EN | IO4 | Output | Audio enable (LOW=enable, HIGH=disable) |
| Audio OUT | IO26 | Output | DAC audio signal output |
| **Buttons** |
| BOOT | IO0 | Input | Boot mode / user button (with internal pullup) |
| RESET | EN | Input | Reset button (active low, shared with LCD reset) |
| **Serial Port (CH340C)** |
| UART RX | IO3 (RXD0) | Input | Serial receive (can be GPIO if UART unused) |
| UART TX | IO1 (TXD0) | Output | Serial transmit (can be GPIO if UART unused) |
| **Battery** |
| Battery ADC | IO34 | Input | Battery voltage monitoring (ADC) |
| **SPI Peripheral Connector** |
| SPI CS | IO21 | Output | Chip select (active low, GPIO if unused) |
| SPI CLK | IO18 | Output | SPI clock (shared with SD card) |
| SPI MISO | IO19 | Input | SPI data in (shared with SD card) |
| SPI MOSI | IO23 | Output | SPI data out (shared with SD card) |
| **I2C Peripheral Connector** |
| I2C SCL | IO25 | Output | I2C clock (GPIO if unused) |
| I2C SDA | IO32 | Bidirectional | I2C data (GPIO if unused) |
| **Expansion Input Pins** |
| Input 1 | IO35 | Input | Input-only pin (no pullup/pulldown) |
| Input 2 | IO39 | Input | Input-only pin (no pullup/pulldown) |

## Pin Usage by Category

### Display Pins (Required for LCD Operation)
```
IO15 - LCD_CS   (Chip Select)
IO2  - LCD_DC   (Data/Command)
IO14 - LCD_SCK  (SPI Clock)
IO13 - LCD_MOSI (SPI Master Out)
IO12 - LCD_MISO (SPI Master In)
EN   - LCD_RST  (Reset, shared)
IO27 - LCD_BL   (Backlight Control)
```

### Touch Pins (E32R40T only)
```
IO33 - TOUCH_CS  (Chip Select)
IO36 - TOUCH_IRQ (Interrupt)
IO14 - TOUCH_SCK (SPI Clock, shared)
IO13 - TOUCH_MOSI (SPI Master Out, shared)
IO12 - TOUCH_MISO (SPI Master In, shared)
```

### SPI Bus Sharing
The module uses multiple SPI devices that share bus lines:

**Shared SPI Bus 1** (Display & Touch):
- SCK: IO14
- MOSI: IO13
- MISO: IO12
- Individual CS: IO15 (LCD), IO33 (Touch)

**Shared SPI Bus 2** (SD Card & SPI Peripheral):
- SCK: IO18
- MOSI: IO23
- MISO: IO19
- Individual CS: IO5 (SD), IO21 (SPI Peripheral)

### Available GPIO Pins

#### Output/Bidirectional Pins (Can be Used as GPIO)
When not using the associated peripheral:
- **IO1, IO3**: If UART not needed
- **IO4**: If audio not needed (but keep LOW to avoid disabling audio circuit)
- **IO21, IO23, IO18, IO19**: If SPI peripheral and SD card not needed
- **IO25, IO32**: If I2C peripheral not needed

#### Input-Only Pins
- **IO34**: Used for battery voltage (can be used for other ADC input)
- **IO35**: Available input-only (expansion connector)
- **IO36**: Used for touch IRQ (input-only)
- **IO39**: Available input-only (expansion connector)

#### Pins Used by System (Not Recommended to Change)
- **IO0**: BOOT button (needed for programming)
- **EN**: RESET button (system reset)
- **IO15, IO2, IO14, IO13, IO12, IO27**: LCD (required for display)

### Strapping Pins (Special Considerations)

These pins have special boot-time behaviors:

- **IO0**: Must be HIGH during boot for normal operation, LOW for download mode
- **IO2**: Must be LOW or floating during boot
- **IO5**: Strapping pin, affects boot mode
- **IO12**: Strapping pin for flash voltage (should be LOW at boot for 3.3V flash)
- **IO15**: Strapping pin, affects boot mode (used for LCD CS)

## Connector Pinouts

### Type-C Connector
- USB D+/D- connected to CH340C
- 5V power input
- Auto-download circuit (DTR/RTS)

### Serial Port Connector (1.25mm 4P)
```
Pin 1: GND
Pin 2: 5V
Pin 3: RXD (IO3)
Pin 4: TXD (IO1)
```

### Battery Connector (1.25mm 2P)
```
Pin 1: BAT+ (Positive)
Pin 2: BAT- (Negative/GND)
```
**Warning**: Observe correct polarity!

### Speaker Interface (1.25mm 2P)
```
Pin 1: SPK+
Pin 2: SPK-
```
Supports 1.5W (8Ω) or 2W (4Ω) speakers

### SPI Peripheral Interface (1.25mm 4P)
```
Pin 1: GND
Pin 2: 3.3V
Pin 3: IO21 (CS)
Pin 4: IO18 (CLK)
Pin 5: IO19 (MISO)
Pin 6: IO23 (MOSI)
```
Note: Shared with MicroSD card

### I2C Peripheral Interface (1.25mm 4P)
```
Pin 1: GND
Pin 2: 3.3V
Pin 3: IO25 (SCL)
Pin 4: IO32 (SDA)
```

### Expansion Input Pins (1.25mm 2P)
```
Pin 1: GND
Pin 2: 3.3V
Pin 3: IO35
Pin 4: IO39
```
Input-only pins

## GPIO Limitations

### Input-Only Pins (No Output Capability)
- IO34, IO35, IO36, IO39
- These pins cannot drive outputs or use internal pull-up/pull-down resistors

### Output-Only Pins
- IO26 (DAC2) - Used for audio output

### ADC Capable Pins
- IO34, IO35, IO36, IO39 (ADC1)
- IO4, IO0, IO2, IO15, IO13, IO12, IO14, IO27, IO25, IO26, IO32, IO33 (ADC2)
- Note: ADC2 pins cannot be used when WiFi is active

### DAC Capable Pins
- IO25 (DAC1)
- IO26 (DAC2) - Used for audio

### Touch Sensor Capable Pins
- IO0, IO2, IO4, IO12, IO13, IO14, IO15, IO27, IO32, IO33
- Note: These are capacitive touch, not resistive touch screen

### PWM Capable Pins
- All output-capable pins support PWM via LEDC peripheral
- IO27 commonly used for backlight PWM dimming

## Programming Interface

### Download Mode Entry Methods

**Method 1: BOOT Button**
1. Press and hold BOOT button
2. Power on device
3. Release BOOT button
4. Device enters download mode

**Method 2: BOOT + RESET**
1. Press and hold BOOT button
2. Press and release RESET button
3. Release BOOT button
4. Device enters download mode

**Method 3: Auto-Download (via Type-C)**
- Connect via Type-C cable
- CH340C auto-download circuit handles boot mode entry automatically
- No button pressing required

## Pin Configuration Recommendations

### For Custom Applications

**Available for Custom Use:**
- IO35, IO39 (input-only, on expansion connector)
- IO1, IO3 (if not using UART serial port)
- IO21, IO23, IO18, IO19 (if not using SPI peripheral and SD card)
- IO25, IO32 (if not using I2C peripheral)

**Use with Caution:**
- IO0 (BOOT button, has pullup, avoid output during boot)
- IO2 (strapping pin, LCD DC, avoid conflicts)
- IO4 (audio enable, keep LOW if using audio)
- IO5 (SD CS, strapping pin)
- IO12 (strapping pin, should be LOW at boot)
- IO15 (strapping pin, LCD CS)

**Not Recommended for Repurposing:**
- IO13, IO14, IO27 (LCD critical signals)
- IO16, IO17, IO22 (RGB LEDs, useful for status)
- IO26 (DAC audio output)
- IO33, IO36 (touch screen, if using E32R40T)
- IO34 (battery monitoring)

## Power Pins

### 5V Power Sources
- Type-C connector
- Battery charging (when battery connected and charged)

### 3.3V Power
- Regulated from 5V via ME6217 LDO
- Available on peripheral connectors
- Powers ESP32 and peripherals

### Ground
- Common ground on all connectors
- Shared between USB, battery, and all peripherals

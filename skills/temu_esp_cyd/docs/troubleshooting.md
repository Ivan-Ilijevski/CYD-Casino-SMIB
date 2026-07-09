# Troubleshooting Guide - ESP32 CYD E32R40T/E32N40T

## Quick Start Procedure

### Initial Setup Steps

1. **Power Connection**
   - Use Type-C cable with both power and data capability
   - Connect to computer USB port or 5V power adapter
   - LED indicators should light up when powered

2. **Install CH340C Driver**
   - Download from "7-工具软件_Tool_software/USB-SERIAL_CH340.zip"
   - Run CH341SER.EXE
   - Click "Install" button
   - Verify in Device Manager: Should see "USB-SERIAL CH340 (COMx)"

3. **Flash Test Program**
   - Open "8-快速使用_Quick_Start/Flash_Download/flash_download_tool"
   - Run flash_download_tool_x.x.x.exe
   - Select: ChipType = ESP32, WorkMode = Develop, LoadMode = UART
   - Select bin file from "8-快速使用_Quick_Start/bin/"
   - Set address to "0"
   - Configure: SPI SPEED = 80MHz, SPI MODE = DIO
   - Select COM port and BAUD rate (921600 recommended)
   - Click START to flash

4. **Run Program**
   - Press RESET button after flashing
   - Display should show demo program

## Power Issues

### No Power / Dead Unit

**Symptoms:**
- No display
- No LED indicators
- No response when connected

**Solutions:**

1. **Check USB Cable**
   - Ensure cable supports both data and power
   - Some cables are charge-only
   - Try a different known-good cable
   - Test cable with another device

2. **Check Power Source**
   - USB port should provide 5V with at least 500mA capability
   - Try different USB port
   - Try direct wall adapter (5V, 1A minimum)
   - Avoid USB hubs without external power

3. **Check Battery (if connected)**
   - Ensure battery polarity is correct (positive to positive)
   - Battery may be completely discharged
   - Disconnect battery and power via USB only
   - Check battery voltage with multimeter (should be 3.7V nominal)

4. **Inspect for Physical Damage**
   - Check for bent pins on connectors
   - Look for damaged components
   - Check for shorts or burns on PCB

### Insufficient Power Symptoms

**Symptoms:**
- Display flickers
- Random resets
- Backlight dims periodically
- WiFi connection drops

**Solutions:**

1. **Use Adequate Power Supply**
   - Minimum: 5V @ 500mA
   - Recommended: 5V @ 1A or higher
   - Total current draw can reach 580mA with all features active

2. **Reduce Power Consumption**
   - Lower backlight brightness via PWM
   - Disconnect unnecessary peripherals
   - Reduce CPU frequency
   - Disable WiFi when not needed

## Display Issues

### Display Not Turning On / Black Screen

**Symptoms:**
- Backlight is off
- Screen remains black
- No visual output

**Solutions:**

1. **Check Backlight Control**
   - Backlight requires IO27 to be HIGH
   - Verify code sets pinMode(27, OUTPUT) and digitalWrite(27, HIGH)
   - Test by measuring voltage at IO27 (should be 3.3V when on)
   ```c
   pinMode(27, OUTPUT);
   digitalWrite(27, HIGH);  // Turn on backlight
   ```

2. **Verify Power Supply**
   - Backlight draws 142mA
   - Ensure power supply can provide sufficient current
   - Check for voltage drop under load

3. **Check Initialization Sequence**
   - Ensure proper reset sequence in code
   - ST7796 requires 120ms after reset before commands
   - Verify Sleep Out command (0x11) is sent
   - Verify Display On command (0x29) is sent

4. **Test with Known-Good Program**
   - Flash factory demo bin file
   - Eliminates software configuration issues

### Backlight On But No Display / Garbage Display

**Symptoms:**
- Backlight is lit
- Screen shows random pixels or nothing
- No recognizable image

**Solutions:**

1. **Verify SPI Configuration**
   ```c
   // Correct settings for ST7796:
   SPI_MODE = SPI_MODE0 (CPOL=0, CPHA=0)
   SPI_SPEED = 40000000 (40MHz recommended)
   BIT_ORDER = SPI_MSBFIRST
   ```

2. **Check Pin Connections**
   - LCD_CS: IO15 (must be LOW when transmitting)
   - LCD_DC: IO2 (controls command/data mode)
   - LCD_SCK: IO14
   - LCD_MOSI: IO13
   - LCD_MISO: IO12
   - LCD_RST: EN pin

3. **Verify Pixel Format**
   - Ensure 0x3A command sent with correct value
   - RGB565: 0x55 (most common)
   - RGB666: 0x66
   - Match pixel format in code to what's configured on display

4. **Reduce SPI Speed**
   - Start with 20MHz for testing
   - Gradually increase if stable
   - Some modules may not support full 80MHz

5. **Check Chip Select Timing**
   - Ensure CS is LOW before transmission
   - Keep CS LOW for entire transaction
   - Set CS HIGH when done

### Wrong Colors Displayed

**Symptoms:**
- Red appears as blue or vice versa
- Colors are inverted or incorrect

**Solutions:**

1. **Toggle BGR/RGB Order**
   - Modify Memory Access Control (0x36 command)
   - Add or remove 0x08 to toggle BGR/RGB
   ```c
   // If colors wrong with 0x48, try:
   writeCommand(0x36);
   writeData(0x40);  // Removed 0x08 for RGB instead of BGR
   ```

2. **Verify Pixel Encoding**
   - For RGB565:
     ```c
     uint16_t red = 0xF800;    // Should show red
     uint16_t green = 0x07E0;  // Should show green
     uint16_t blue = 0x001F;   // Should show blue
     ```
   - If colors are swapped, check byte order

3. **Check Display Inversion**
   - Send 0x21 for inversion ON
   - Send 0x20 for inversion OFF

### Display Orientation Wrong

**Symptoms:**
- Image is rotated
- Image is mirrored

**Solutions:**

1. **Set Correct Rotation**
   ```c
   writeCommand(0x36);  // Memory Access Control

   // Common values:
   writeData(0x48);  // Portrait (0°)
   writeData(0x28);  // Landscape (90°)
   writeData(0x88);  // Portrait inverted (180°)
   writeData(0xE8);  // Landscape inverted (270°)
   ```

2. **Adjust MY, MX, MV Bits**
   - MY (bit 7): Row address order
   - MX (bit 6): Column address order
   - MV (bit 5): Row/Column exchange

### Flickering Display

**Symptoms:**
- Display brightness fluctuates
- Image flickers or tears

**Solutions:**

1. **Check Power Supply Stability**
   - Use quality power adapter
   - Add decoupling capacitors if needed
   - Check for voltage drops under load

2. **Reduce Screen Update Rate**
   - Don't redraw entire screen every loop
   - Use partial updates
   - Implement frame rate limiting

3. **Check Backlight PWM**
   - PWM frequency should be 5kHz or higher
   - Lower frequencies cause visible flicker
   ```c
   ledcSetup(0, 5000, 8);  // 5kHz, 8-bit resolution
   ```

4. **Verify SPI Speed**
   - Too fast may cause instability
   - Try reducing to 40MHz

## Touch Screen Issues (E32R40T Only)

### Touch Not Responding

**Symptoms:**
- Display works but touch has no effect
- No touch detection

**Solutions:**

1. **Verify Touch is in Code**
   - Ensure touch initialization code is included
   - Check that touch library is configured
   - Verify IO pins are correctly defined:
     ```c
     TOUCH_CS: IO33
     TOUCH_IRQ: IO36
     Shared SPI: IO14 (SCK), IO13 (MOSI), IO12 (MISO)
     ```

2. **Check Touch Calibration**
   - Resistive touch requires calibration
   - Run calibration routine
   - Save calibration parameters
   - Verify calibration values are loaded

3. **Verify SPI Bus Sharing**
   - Touch shares SPI with display
   - Ensure proper CS line control
   - LCD CS (IO15) HIGH when accessing touch
   - Touch CS (IO33) LOW when accessing touch
   ```c
   digitalWrite(15, HIGH);  // Deselect LCD
   digitalWrite(33, LOW);   // Select touch
   // ... read touch data ...
   digitalWrite(33, HIGH);  // Deselect touch
   ```

4. **Check Touch IRQ**
   - IO36 goes LOW when touched
   - Verify IRQ is configured as input
   - Test with multimeter or oscilloscope

5. **Test Touch Hardware**
   - Gently press screen and check if IO36 goes LOW
   - If no response, hardware issue possible

### Touch Coordinates Wrong

**Symptoms:**
- Touch works but coordinates are incorrect
- Touch position doesn't match press location

**Solutions:**

1. **Recalibrate Touch Screen**
   - Run calibration routine
   - Touch calibration points carefully
   - Verify calibration values are reasonable

2. **Check Coordinate Mapping**
   - Ensure touch coordinates mapped to display resolution
   - Account for display rotation
   - Verify coordinate transformations in code

3. **Check for Display/Touch Mismatch**
   - Display and touch have slightly different active areas
   - May need offset or scaling adjustments

## Programming / Flashing Issues

### Cannot Enter Download Mode

**Symptoms:**
- Flash tool cannot connect
- "Failed to connect" error
- ESP32 not responding

**Solutions:**

1. **Manual Boot Mode Entry**
   - **Method 1**:
     - Press and hold BOOT button
     - Power on (connect USB)
     - Release BOOT button
   - **Method 2**:
     - Press and hold BOOT button
     - Press and release RESET button
     - Release BOOT button

2. **Check USB Connection**
   - Verify driver is installed (CH340C)
   - Check Device Manager for COM port
   - Try different USB cable
   - Try different USB port

3. **Verify COM Port Settings**
   - Correct COM port selected
   - Baud rate: 115200 (initial) or 921600 (fast)
   - No other program using the port

4. **Check Auto-Download Circuit**
   - Type-C should auto-enter download mode
   - If not working, use manual method
   - May indicate hardware issue with CH340C circuit

### Upload Fails / Verification Errors

**Symptoms:**
- Upload starts but fails partway
- "Hash mismatch" or verification errors
- Timeout during upload

**Solutions:**

1. **Reduce Baud Rate**
   - Try 115200 instead of 921600
   - Some USB ports/cables don't support high speeds

2. **Use Correct Flash Settings**
   - SPI SPEED: 80MHz (or 40MHz if issues)
   - SPI MODE: DIO
   - Flash Size: 4MB
   - Start Address: 0x0 for full bin files

3. **Check USB Cable Quality**
   - Use short, high-quality cable
   - Avoid USB hubs if possible
   - Cable should support data transfer

4. **Try Different Flash Tool**
   - esptool.py (command line)
   - Arduino IDE flash tool
   - Platform IO

5. **Erase Flash First**
   - Click ERASE button in flash tool
   - Then reflash program

### Program Uploaded But Not Running

**Symptoms:**
- Upload completes successfully
- Program doesn't execute
- Previous program still runs

**Solutions:**

1. **Press RESET Button**
   - After upload completes, press RESET
   - Ensures new program starts

2. **Power Cycle**
   - Disconnect and reconnect USB
   - Ensures clean start

3. **Check for Upload to Wrong Address**
   - Full bin files should start at 0x0
   - Verify address in flash tool

4. **Verify Binary File**
   - Ensure correct bin file selected
   - Check file is not corrupted
   - Try different demo bin file

## CH340C Driver Issues

### Driver Not Installing

**Symptoms:**
- Cannot install CH340C driver
- Installation fails with error

**Solutions:**

1. **Run as Administrator**
   - Right-click CH341SER.EXE
   - Select "Run as Administrator"

2. **Disable Driver Signature Enforcement** (Windows)
   - May be required on some systems
   - Search online for method for your Windows version

3. **Download Latest Driver**
   - From CH340 manufacturer website
   - Or from LCDWIKI support site

### COM Port Not Appearing

**Symptoms:**
- Driver installed but no COM port
- Device Manager shows unknown device

**Solutions:**

1. **Check Device Manager**
   - Look for "USB-SERIAL CH340"
   - Should be under "Ports (COM & LPT)"
   - If under "Other devices", driver not properly installed

2. **Reinstall Driver**
   - Uninstall existing driver
   - Restart computer
   - Reinstall fresh

3. **Try Different USB Port**
   - Some USB 3.0 ports have issues
   - Try USB 2.0 port

4. **Check USB Cable**
   - Must be data cable, not charge-only
   - Try different cable

## Battery and Charging Issues

### Battery Won't Charge

**Symptoms:**
- Battery not charging when connected
- No charging indicator

**Solutions:**

1. **Verify Battery Type**
   - Must be 3.7V lithium polymer battery
   - Other battery types not compatible

2. **Check Battery Polarity**
   - Positive to positive, negative to negative
   - **Incorrect polarity can damage circuit**

3. **Check Battery Voltage**
   - If battery completely dead (< 2.5V), may not charge
   - Charge management IC may have protection

4. **Verify Charging Voltage**
   - USB power should provide 5V
   - Check voltage at USB connector

5. **Check Charging IC**
   - If IC damaged, will not charge
   - May require hardware repair

### Battery Drains Quickly

**Symptoms:**
- Battery runs out fast
- Runtime much shorter than expected

**Solutions:**

1. **Reduce Power Consumption**
   - Lower backlight brightness
   - Disable WiFi when not needed
   - Reduce CPU frequency
   - Use deep sleep when idle

2. **Check for Battery Degradation**
   - Old batteries lose capacity
   - Replace with fresh battery

3. **Verify Charging is Complete**
   - Let battery charge fully (4.2V)
   - Charging may take several hours

## Peripheral Issues

### SD Card Not Detected

**Symptoms:**
- Cannot read SD card
- SD initialization fails

**Solutions:**

1. **Check SD Card Format**
   - Should be FAT32
   - Try reformatting card

2. **Verify SPI Pin Configuration**
   - SD_CS: IO5
   - SD_MOSI: IO23
   - SD_CLK: IO18
   - SD_MISO: IO19

3. **Check for SPI Conflicts**
   - SD shares SPI with SPI peripheral connector
   - Ensure no conflicts with other SPI devices

4. **Reduce SPI Speed**
   - SD cards may not support high speeds
   - Try 10-20 MHz for SD

### Speaker No Sound

**Symptoms:**
- No audio output from speaker
- Speaker doesn't work

**Solutions:**

1. **Check Audio Enable**
   - IO4 must be LOW to enable audio
   ```c
   pinMode(4, OUTPUT);
   digitalWrite(4, LOW);  // Enable audio
   ```

2. **Verify Speaker Connection**
   - Check speaker is properly connected
   - Check polarity (if relevant)
   - Verify speaker impedance (4Ω or 8Ω)

3. **Check Audio Output**
   - IO26 is DAC output
   - Verify audio signal is being generated
   - Test with oscilloscope if available

4. **Test with Simple Tone**
   ```c
   dacWrite(26, 128);  // Mid-level voltage
   ```

### RGB LED Not Working

**Symptoms:**
- LED indicators don't light up
- Wrong colors displayed

**Solutions:**

1. **Verify LED Control**
   - LEDs are common anode
   - LOW = ON, HIGH = OFF
   ```c
   pinMode(22, OUTPUT);  // Red
   pinMode(16, OUTPUT);  // Green
   pinMode(17, OUTPUT);  // Blue

   digitalWrite(22, LOW);   // Red ON
   digitalWrite(16, HIGH);  // Green OFF
   digitalWrite(17, HIGH);  // Blue OFF
   ```

2. **Check for Conflicts**
   - Ensure pins not used for other purposes
   - IO16, IO17, IO22 should be available

## WiFi Issues

### WiFi Not Connecting

**Symptoms:**
- Cannot connect to WiFi network
- Connection timeout

**Solutions:**

1. **Check WiFi Credentials**
   - Verify SSID is correct
   - Check password (case-sensitive)

2. **Check WiFi Band**
   - ESP32 only supports 2.4 GHz
   - Does not support 5 GHz WiFi

3. **Check Signal Strength**
   - Move closer to router
   - Check for interference

4. **Verify WiFi Code**
   - Ensure WiFi.begin() called correctly
   - Check for proper WiFi library usage

### WiFi Connection Drops

**Symptoms:**
- WiFi connects but disconnects randomly
- Unstable connection

**Solutions:**

1. **Check Power Supply**
   - WiFi requires significant current
   - Ensure adequate power (5V @ 1A minimum)

2. **Add Connection Monitoring**
   - Implement auto-reconnect logic
   - Check WiFi.status() regularly

3. **Reduce WiFi Power**
   - Lower WiFi transmit power if range not critical
   ```c
   WiFi.setTxPower(WIFI_POWER_11dBm);  // Lower power
   ```

## Safety Precautions

### Hardware Safety

- **Do not short circuit pins**
- **Observe battery polarity** - incorrect can cause fire/damage
- **Do not exceed 6.5V input** - will damage ESP32
- **Avoid ESD** - use proper grounding when handling
- **Do not touch components during operation** - may be hot
- **Keep away from moisture**
- **Ensure adequate ventilation** when charging battery

### Charging Safety

- **Monitor temperature** during charging - should not exceed 62°C
- **Use correct battery type** - 3.7V lithium polymer only
- **Do not overcharge** - charging circuit has protection but monitor
- **Charge in ventilated area**
- **Do not leave charging unattended** for extended periods
- **Disconnect if battery swells** or shows damage

## Getting Help

### Before Asking for Help

1. Try all troubleshooting steps in this guide
2. Test with factory demo bin files
3. Check all connections and settings
4. Note exact error messages
5. Document what you've tried

### Where to Get Support

1. **LCDWIKI Official Website**: www.lcdwiki.com
2. **Product Documentation** in the data package
3. **Demo Code and Examples** in the package
4. **Community Forums** - search for similar issues

### Information to Provide

When seeking help, provide:
- Exact product model (E32R40T or E32N40T)
- What you're trying to do
- Error messages (exact text)
- What you've already tried
- Photos of connections (if hardware issue)
- Code snippets (if software issue)

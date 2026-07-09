# Programming Examples - ESP32 CYD E32R40T/E32N40T

## Development Environment Setup

### Supported Development Platforms

The E32R40T/E32N40T supports three main development environments:

1. **Arduino IDE** (most popular, easiest to start)
2. **ESP-IDF** (official Espressif framework, most powerful)
3. **MicroPython** (Python-based, great for rapid prototyping)

Documentation for each is available in the package:
- Arduino: `1-板子程序_Demo/Arduino/`
- ESP-IDF: `1-板子程序_Demo/ESP32-IDF/`
- MicroPython: `1-板子程序_Demo/MicroPython/`

## Arduino IDE Setup

### Installing Arduino IDE

1. Download Arduino IDE 1.8.x or 2.x
2. Install ESP32 board support:
   - Open Arduino IDE
   - File → Preferences
   - Add to "Additional Board Manager URLs":
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - Tools → Board → Boards Manager
   - Search "esp32" and install "esp32 by Espressif Systems"

### Board Configuration

```
Board: "ESP32 Dev Module"
Upload Speed: "921600"
CPU Frequency: "240MHz (WiFi/BT)"
Flash Frequency: "80MHz"
Flash Mode: "QIO"
Flash Size: "4MB (32Mb)"
Partition Scheme: "Default 4MB with spiffs"
Core Debug Level: "None" (or "Info" for debugging)
PSRAM: "Disabled"
Arduino Runs On: "Core 1"
Events Run On: "Core 1"
```

## Basic Examples

### Example 1: Hello World - Backlight Control

```cpp
// Pin definitions
#define TFT_BL 27  // Backlight control

void setup() {
  Serial.begin(115200);
  Serial.println("ESP32 CYD Hello World");

  // Initialize backlight
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);  // Turn on backlight

  Serial.println("Backlight ON");
}

void loop() {
  // Blink backlight every 2 seconds
  delay(1000);
  digitalWrite(TFT_BL, LOW);   // Off
  Serial.println("Backlight OFF");

  delay(1000);
  digitalWrite(TFT_BL, HIGH);  // On
  Serial.println("Backlight ON");
}
```

### Example 2: RGB LED Control

```cpp
// Pin definitions
#define LED_RED   22
#define LED_GREEN 16
#define LED_BLUE  17

// Note: LEDs are common anode (LOW = ON, HIGH = OFF)

void setup() {
  Serial.begin(115200);

  // Initialize LED pins
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);

  // Turn all LEDs off initially
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

void loop() {
  // Red
  setColor(true, false, false);
  delay(500);

  // Green
  setColor(false, true, false);
  delay(500);

  // Blue
  setColor(false, false, true);
  delay(500);

  // Yellow (Red + Green)
  setColor(true, true, false);
  delay(500);

  // Cyan (Green + Blue)
  setColor(false, true, true);
  delay(500);

  // Magenta (Red + Blue)
  setColor(true, false, true);
  delay(500);

  // White (All on)
  setColor(true, true, true);
  delay(500);

  // Off
  setColor(false, false, false);
  delay(500);
}

void setColor(bool red, bool green, bool blue) {
  // Remember: LOW = ON, HIGH = OFF
  digitalWrite(LED_RED, !red);
  digitalWrite(LED_GREEN, !green);
  digitalWrite(LED_BLUE, !blue);
}
```

### Example 3: Button Input

```cpp
// Pin definitions
#define BOOT_BUTTON 0

void setup() {
  Serial.begin(115200);

  // Initialize button (has internal pullup)
  pinMode(BOOT_BUTTON, INPUT);

  Serial.println("Press BOOT button...");
}

void loop() {
  static bool lastState = HIGH;
  bool currentState = digitalRead(BOOT_BUTTON);

  // Detect button press (HIGH to LOW transition)
  if (lastState == HIGH && currentState == LOW) {
    Serial.println("Button PRESSED!");
  }

  // Detect button release (LOW to HIGH transition)
  if (lastState == LOW && currentState == HIGH) {
    Serial.println("Button RELEASED!");
  }

  lastState = currentState;
  delay(10);  // Debounce delay
}
```

### Example 4: PWM Backlight Dimming

```cpp
// Pin definitions
#define TFT_BL 27

// PWM settings
#define PWM_CHANNEL 0
#define PWM_FREQ 5000
#define PWM_RESOLUTION 8  // 8-bit: 0-255

void setup() {
  Serial.begin(115200);

  // Configure PWM for backlight
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(TFT_BL, PWM_CHANNEL);

  Serial.println("Backlight PWM Demo");
}

void loop() {
  // Fade in
  Serial.println("Fading in...");
  for (int brightness = 0; brightness <= 255; brightness++) {
    ledcWrite(PWM_CHANNEL, brightness);
    delay(5);
  }

  delay(500);

  // Fade out
  Serial.println("Fading out...");
  for (int brightness = 255; brightness >= 0; brightness--) {
    ledcWrite(PWM_CHANNEL, brightness);
    delay(5);
  }

  delay(500);
}

// Function to set brightness (0-100%)
void setBacklightBrightness(uint8_t percent) {
  if (percent > 100) percent = 100;
  uint8_t value = map(percent, 0, 100, 0, 255);
  ledcWrite(PWM_CHANNEL, value);
}
```

## Display Programming with TFT_eSPI

### TFT_eSPI Library Installation

1. Install TFT_eSPI library from Arduino Library Manager
2. Locate User_Setup.h (usually in `Arduino/libraries/TFT_eSPI/`)
3. Configure for E32R40T/E32N40T:

### User_Setup.h Configuration

```cpp
// User_Setup.h for ESP32 CYD E32R40T/E32N40T

#define ST7796_DRIVER

#define TFT_WIDTH  320
#define TFT_HEIGHT 480

// Pin definitions
#define TFT_MOSI 13
#define TFT_MISO 12
#define TFT_SCLK 14
#define TFT_CS   15  // Chip select
#define TFT_DC    2  // Data/command
#define TFT_RST  -1  // Reset (connected to EN, use -1)
#define TFT_BL   27  // Backlight

// SPI frequency
#define SPI_FREQUENCY  40000000  // 40 MHz
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000

// Optional: Use DMA for faster updates
#define SPI_DMA_CHANNEL 1

// Optional: Define colors
#define TFT_BLACK   0x0000
#define TFT_NAVY    0x000F
#define TFT_DARKGREEN 0x03E0
#define TFT_DARKCYAN 0x03EF
#define TFT_MAROON  0x7800
#define TFT_PURPLE  0x780F
#define TFT_OLIVE   0x7BE0
#define TFT_LIGHTGREY 0xC618
#define TFT_DARKGREY 0x7BEF
#define TFT_BLUE    0x001F
#define TFT_GREEN   0x07E0
#define TFT_CYAN    0x07FF
#define TFT_RED     0xF800
#define TFT_MAGENTA 0xF81F
#define TFT_YELLOW  0xFFE0
#define TFT_WHITE   0xFFFF
#define TFT_ORANGE  0xFD20
#define TFT_GREENYELLOW 0xAFE5
#define TFT_PINK    0xF81F

// Fonts
#define LOAD_GLCD   // Font 1. Original Adafruit 8 pixel font needs ~1820 bytes in FLASH
#define LOAD_FONT2  // Font 2. Small 16 pixel high font, needs ~3534 bytes in FLASH
#define LOAD_FONT4  // Font 4. Medium 26 pixel high font, needs ~5848 bytes in FLASH
#define LOAD_FONT6  // Font 6. Large 48 pixel font, needs ~2666 bytes in FLASH
#define LOAD_FONT7  // Font 7. 7 segment 48 pixel font, needs ~2438 bytes in FLASH
#define LOAD_FONT8  // Font 8. Large 75 pixel font, needs ~3256 bytes in FLASH

#define SMOOTH_FONT
```

### Example 5: Simple Display Test

```cpp
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);

  // Initialize display
  tft.init();
  tft.setRotation(0);  // Portrait mode

  // Turn on backlight
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  // Clear screen
  tft.fillScreen(TFT_BLACK);

  // Test display
  testDisplay();
}

void loop() {
  // Nothing to do
}

void testDisplay() {
  // Fill screen with colors
  tft.fillScreen(TFT_RED);
  delay(500);
  tft.fillScreen(TFT_GREEN);
  delay(500);
  tft.fillScreen(TFT_BLUE);
  delay(500);
  tft.fillScreen(TFT_BLACK);

  // Draw text
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("ESP32 CYD");
  tft.println("Display Test");

  // Draw shapes
  tft.drawRect(10, 100, 100, 50, TFT_YELLOW);
  tft.fillRect(20, 110, 80, 30, TFT_BLUE);

  tft.drawCircle(200, 125, 40, TFT_GREEN);
  tft.fillCircle(200, 125, 30, TFT_RED);

  tft.drawLine(10, 200, 310, 200, TFT_WHITE);
  tft.drawLine(160, 200, 160, 470, TFT_WHITE);

  // Draw some text
  tft.setTextColor(TFT_CYAN);
  tft.setTextSize(3);
  tft.setCursor(60, 250);
  tft.println("Hello");
  tft.setCursor(60, 280);
  tft.println("World!");
}
```

### Example 6: Graphics Demo

```cpp
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  tft.init();
  tft.setRotation(0);

  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  tft.fillScreen(TFT_BLACK);
}

void loop() {
  // Random colored rectangles
  for (int i = 0; i < 20; i++) {
    int x = random(0, 320);
    int y = random(0, 480);
    int w = random(10, 50);
    int h = random(10, 50);
    uint16_t color = random(0x0000, 0xFFFF);

    tft.fillRect(x, y, w, h, color);
    delay(100);
  }

  tft.fillScreen(TFT_BLACK);

  // Random circles
  for (int i = 0; i < 20; i++) {
    int x = random(0, 320);
    int y = random(0, 480);
    int r = random(5, 40);
    uint16_t color = random(0x0000, 0xFFFF);

    tft.drawCircle(x, y, r, color);
    delay(100);
  }

  tft.fillScreen(TFT_BLACK);
}
```

### Example 7: Display Text and Values

```cpp
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

int counter = 0;
unsigned long lastUpdate = 0;

void setup() {
  tft.init();
  tft.setRotation(0);

  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  tft.fillScreen(TFT_BLACK);

  // Draw static labels
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("ESP32 CYD Demo");

  tft.setCursor(10, 50);
  tft.println("Counter:");

  tft.setCursor(10, 100);
  tft.println("Uptime:");

  tft.setCursor(10, 150);
  tft.println("Free Heap:");
}

void loop() {
  if (millis() - lastUpdate >= 1000) {
    lastUpdate = millis();
    counter++;

    // Update counter value
    tft.setTextColor(TFT_YELLOW, TFT_BLACK);
    tft.setTextSize(3);
    tft.setCursor(10, 75);
    tft.printf("%d   ", counter);  // Spaces to clear old digits

    // Update uptime
    unsigned long seconds = millis() / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    seconds = seconds % 60;
    minutes = minutes % 60;

    tft.setCursor(10, 125);
    tft.printf("%02lu:%02lu:%02lu   ", hours, minutes, seconds);

    // Update free heap
    tft.setCursor(10, 175);
    tft.printf("%lu bytes   ", ESP.getFreeHeap());
  }
}
```

## Touch Screen Programming (E32R40T)

### Touch Library Configuration

The resistive touch uses XPT2046 chip. Install XPT2046_Touchscreen library.

### Example 8: Basic Touch Detection

```cpp
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

TFT_eSPI tft = TFT_eSPI();

// Touch pins
#define TOUCH_CS 33
#define TOUCH_IRQ 36

XPT2046_Touchscreen touch(TOUCH_CS, TOUCH_IRQ);

void setup() {
  Serial.begin(115200);

  // Initialize display
  tft.init();
  tft.setRotation(0);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  // Initialize touch
  touch.begin();
  touch.setRotation(0);

  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.println("Touch anywhere");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    // Map touch coordinates to screen coordinates
    int x = map(p.x, 200, 3700, 0, 320);
    int y = map(p.y, 200, 3900, 0, 480);

    // Constrain to screen bounds
    x = constrain(x, 0, 319);
    y = constrain(y, 0, 479);

    // Draw circle at touch point
    tft.fillCircle(x, y, 3, TFT_RED);

    // Print coordinates
    Serial.printf("Touch: X=%d, Y=%d\n", x, y);

    delay(10);
  }
}
```

### Example 9: Touch Calibration

```cpp
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <EEPROM.h>

#define EEPROM_SIZE 512
#define CALIBRATION_ADDR 0

TFT_eSPI tft = TFT_eSPI();
XPT2046_Touchscreen touch(33, 36);

struct CalibrationData {
  int x_min, x_max;
  int y_min, y_max;
  bool valid;
};

CalibrationData cal;

void setup() {
  Serial.begin(115200);
  EEPROM.begin(EEPROM_SIZE);

  tft.init();
  tft.setRotation(0);
  pinMode(27, OUTPUT);
  digitalWrite(27, HIGH);

  touch.begin();

  // Load calibration or run calibration
  loadCalibration();

  if (!cal.valid) {
    calibrateTouch();
  }

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Calibration complete!");
}

void loop() {
  if (touch.touched()) {
    TS_Point p = touch.getPoint();

    int x = map(p.x, cal.x_min, cal.x_max, 0, 320);
    int y = map(p.y, cal.y_min, cal.y_max, 0, 480);

    x = constrain(x, 0, 319);
    y = constrain(y, 0, 479);

    tft.fillCircle(x, y, 3, TFT_GREEN);
    delay(10);
  }
}

void calibrateTouch() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);

  // Calibration point 1: Top-left
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Touch top-left");
  tft.drawCircle(20, 20, 5, TFT_RED);

  while (!touch.touched()) delay(10);
  TS_Point p1 = touch.getPoint();
  cal.x_min = p1.x;
  cal.y_min = p1.y;
  delay(500);
  while (touch.touched()) delay(10);

  // Calibration point 2: Bottom-right
  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Touch bottom-right");
  tft.drawCircle(300, 460, 5, TFT_RED);

  while (!touch.touched()) delay(10);
  TS_Point p2 = touch.getPoint();
  cal.x_max = p2.x;
  cal.y_max = p2.y;
  delay(500);

  cal.valid = true;
  saveCalibration();
}

void saveCalibration() {
  EEPROM.put(CALIBRATION_ADDR, cal);
  EEPROM.commit();
}

void loadCalibration() {
  EEPROM.get(CALIBRATION_ADDR, cal);
}
```

## WiFi Examples

### Example 10: WiFi Connection

```cpp
#include <WiFi.h>

const char* ssid = "your_SSID";
const char* password = "your_PASSWORD";

void setup() {
  Serial.begin(115200);

  Serial.println("Connecting to WiFi...");
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nConnected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.print("Signal Strength: ");
  Serial.print(WiFi.RSSI());
  Serial.println(" dBm");
}

void loop() {
  // Check connection
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected!");
    WiFi.reconnect();
  }

  delay(10000);
}
```

## SD Card Examples

### Example 11: SD Card Read/Write

```cpp
#include <SPI.h>
#include <SD.h>

#define SD_CS 5

void setup() {
  Serial.begin(115200);

  // Initialize SD card
  if (!SD.begin(SD_CS)) {
    Serial.println("SD Card initialization failed!");
    return;
  }

  Serial.println("SD Card initialized");

  // Write file
  File file = SD.open("/test.txt", FILE_WRITE);
  if (file) {
    file.println("Hello from ESP32 CYD!");
    file.close();
    Serial.println("File written");
  }

  // Read file
  file = SD.open("/test.txt");
  if (file) {
    Serial.println("File contents:");
    while (file.available()) {
      Serial.write(file.read());
    }
    file.close();
  }
}

void loop() {
  // Nothing
}
```

## Battery Monitoring

### Example 12: Battery Voltage Reading

```cpp
#define BAT_ADC 34
#define BAT_VOLTAGE_DIVIDER 2.0  // Adjust based on actual circuit

void setup() {
  Serial.begin(115200);
  pinMode(BAT_ADC, INPUT);

  // Configure ADC
  analogReadResolution(12);  // 12-bit ADC
  analogSetAttenuation(ADC_11db);  // 0-3.3V range
}

void loop() {
  int adcValue = analogRead(BAT_ADC);

  // Convert to voltage (3.3V ref, 12-bit = 4096 levels)
  float voltage = (adcValue / 4095.0) * 3.3 * BAT_VOLTAGE_DIVIDER;

  // Calculate percentage (3.0V = 0%, 4.2V = 100%)
  float percentage = ((voltage - 3.0) / 1.2) * 100.0;
  percentage = constrain(percentage, 0, 100);

  Serial.printf("Battery: %.2fV (%.0f%%)\n", voltage, percentage);

  delay(5000);
}
```

## Demo Programs Included

The product comes with pre-compiled demo programs in:
`8-快速使用_Quick_Start/bin/`

Available demos include:
- `lvgl_demo_benchmark.bin` - Performance test
- `lvgl_demo_keypad_encoder.bin` - Input demo
- `lvgl_demo_music.bin` - Music player UI
- `lvgl_demo_stress.bin` - Stress test
- `lvgl_demo_widgets.bin` - Widget showcase

These can be flashed using the flash download tool to test hardware functionality.

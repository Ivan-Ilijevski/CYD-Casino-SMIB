# Power and Battery Management - ESP32 CYD E32R40T/E32N40T

## Power Supply Options

The ESP32 CYD board supports two power input methods:

1. **USB Type-C Power** (Primary)
2. **3.7V Lithium Battery** (Portable operation)

Both can be used simultaneously - the battery will charge when USB power is connected.

## USB Type-C Power Input

### Specifications

- **Input Voltage**: 5V DC
- **Current Requirements**:
  - Minimum: 500 mA
  - Recommended: 1A or higher
  - Maximum draw: 580 mA (all features active)
- **Connector Type**: USB Type-C
- **Function**: Power supply and data transfer (programming/serial)

### Power Consumption by Mode

| Mode | Current Draw | Power (@ 5V) |
|------|--------------|--------------|
| ESP32 Reset | 40 mA | 0.2 W |
| Display Only (backlight on) | 230 mA | 1.15 W |
| Display + Speaker + Battery Charging | 580 mA | 2.9 W |

### Component Power Breakdown

- **ESP32 Module**: ~80-150 mA (varies with WiFi activity)
- **Backlight LEDs**: 142 mA
- **Display Logic**: ~88 mA
- **RGB LEDs**: ~20 mA per LED (when on)
- **Touch Controller**: ~10 mA
- **SD Card**: 50-100 mA (when active)
- **Speaker Amplifier**: Up to 500 mA (at max volume)
- **Battery Charging**: Up to 290 mA

### USB Cable Requirements

**Important**: Not all USB cables are created equal!

- Must support both **power AND data**
- Charge-only cables will NOT work for programming
- Use cables rated for at least 1A current
- Shorter cables (< 1m) generally work better
- USB 2.0 specification cables recommended

### Power Supply Recommendations

**Good Power Sources:**
- Computer USB 3.0 ports (900 mA available)
- Dedicated 5V wall adapters (1A+)
- Quality USB power banks

**Avoid:**
- Unpowered USB hubs
- Computer USB 2.0 ports (may be insufficient under heavy load)
- Low-quality wall adapters
- USB ports labeled "charging only"

## Battery Power System

### Battery Specifications

- **Battery Type**: 3.7V Lithium Polymer (LiPo)
- **Connector**: 1.25mm 2-pin JST connector
- **Polarity**: RED = Positive (+), BLACK = Negative (-)

**CRITICAL**: Incorrect polarity will damage the board and may cause fire!

### Recommended Battery Capacity

The board works with various capacity batteries:

- **Minimum**: 500 mAh (short runtime)
- **Typical**: 1000-2000 mAh (good balance)
- **Maximum**: Limited only by physical size

### Runtime Estimation

Approximate runtime based on battery capacity and usage:

**Display-Only Mode** (~230 mA draw):
- 500 mAh battery: ~2 hours
- 1000 mAh battery: ~4 hours
- 2000 mAh battery: ~8 hours

**Heavy Use** (WiFi active, ~400 mA draw):
- 500 mAh battery: ~1.25 hours
- 1000 mAh battery: ~2.5 hours
- 2000 mAh battery: ~5 hours

**Light Use** (low backlight, ~150 mA draw):
- 500 mAh battery: ~3.3 hours
- 1000 mAh battery: ~6.6 hours
- 2000 mAh battery: ~13 hours

*Note: Actual runtime varies with battery quality, age, and temperature*

### Battery Connection

**Connector Pinout** (1.25mm JST 2-pin):
```
     ___________
    |  1  |  2  |
    |_____|_____|
      |     |
    BAT+  BAT-
    (Red) (Black)
```

**ALWAYS verify polarity before connecting!**

### Battery Safety Warnings

- **Use ONLY 3.7V lithium polymer batteries**
- **NEVER connect other battery types** (alkaline, NiMH, etc.)
- **ALWAYS observe correct polarity** - reverse polarity causes damage and fire risk
- **DO NOT short-circuit battery terminals**
- **DO NOT puncture, crush, or disassemble batteries**
- **DO NOT exceed 60°C temperature** during operation
- **DO NOT leave charging unattended** for extended periods
- **REPLACE batteries showing swelling**, damage, or reduced capacity
- **DISPOSE of batteries properly** - do not throw in regular trash

## Battery Charging System

### Charge Management IC

- **IC Model**: QD3958 (or similar)
- **Features**:
  - Constant current / constant voltage (CC/CV) charging
  - Over-charge protection
  - Over-discharge protection
  - Thermal protection
  - Automatic charge termination

### Charging Specifications

| Parameter | Value | Unit |
|-----------|-------|------|
| Charging Voltage (Input) | 4.2 - 6.5 (typical 5V) | V |
| Maximum Charging Current | 500 | mA |
| Actual Charging Current | 290 | mA |
| Charge Saturation Voltage | 4.24 | V |
| Maximum IC Temperature | 62 | °C |

### Charging Process

The charging system uses a three-stage process:

**Stage 1: Pre-charge** (if battery < 3.0V)
- Low current charge to safely bring battery voltage up
- Prevents damage to deeply discharged batteries

**Stage 2: Constant Current (CC)**
- Charges at ~290 mA until battery reaches ~4.2V
- Main charging phase
- Majority of capacity restored here

**Stage 3: Constant Voltage (CV)**
- Maintains 4.24V while current tapers off
- Tops off battery to full capacity
- Charging complete when current drops below threshold

### Charge Time Estimates

Approximate time to fully charge from empty:

- 500 mAh battery: ~2 hours
- 1000 mAh battery: ~4 hours
- 2000 mAh battery: ~8 hours

*Plus ~30 minutes for CV phase*

### Charging Indicators

The board does not have a dedicated charge indicator LED. You can:

1. **Monitor via software** - Read battery voltage on IO34
2. **Use RGB LEDs** - Program them to indicate charge status
3. **Display on screen** - Show charge status on TFT

### Charging from USB

- Connect USB Type-C cable to power source
- Battery will charge automatically if connected
- Can use device while charging
- Charging stops automatically when full

### Charging Safety

**Normal Operation:**
- IC may become warm (40-50°C) - this is normal
- Battery may become slightly warm - this is normal
- Charge in well-ventilated area

**Warning Signs:**
- IC temperature exceeds 62°C - DISCONNECT POWER
- Battery becomes very hot - DISCONNECT IMMEDIATELY
- Battery swells - DISCONNECT and dispose properly
- Unusual odor - DISCONNECT immediately
- Smoke - DISCONNECT and move to safe area

### Charging in Different Conditions

**Temperature Effects:**
- **Optimal**: 20-25°C (room temperature)
- **Acceptable**: 10-30°C
- **Not recommended**: < 0°C or > 40°C
- **DO NOT charge**: < 0°C or > 45°C

**Humidity:**
- Keep device in dry environment
- Avoid charging in high humidity

## Battery Voltage Monitoring

### Hardware Connection

- **ADC Pin**: IO34
- **Function**: Analog input for battery voltage measurement
- **Voltage Divider**: Circuit divides battery voltage to safe ADC range

### Reading Battery Voltage

```cpp
#define BAT_ADC 34

void setup() {
  pinMode(BAT_ADC, INPUT);
  analogReadResolution(12);  // 12-bit resolution
  analogSetAttenuation(ADC_11db);  // For 0-3.3V range
}

float getBatteryVoltage() {
  int adcValue = analogRead(BAT_ADC);

  // Convert ADC reading to voltage
  // Note: Adjust multiplier based on actual voltage divider
  float voltage = (adcValue / 4095.0) * 3.3 * 2.0;

  return voltage;
}

int getBatteryPercent() {
  float voltage = getBatteryVoltage();

  // LiPo voltage to percentage (approximate)
  // 4.2V = 100%, 3.0V = 0%
  float percent = ((voltage - 3.0) / 1.2) * 100.0;

  return constrain((int)percent, 0, 100);
}
```

### Battery Voltage Reference

| Voltage | Charge Level | Status |
|---------|--------------|--------|
| 4.2V | 100% | Fully charged |
| 4.0V | 80% | Good |
| 3.8V | 60% | Medium |
| 3.6V | 40% | Low |
| 3.4V | 20% | Very low |
| 3.0V | 0% | Depleted - recharge immediately |
| < 3.0V | Critical | Risk of damage |

**Note**: These are approximate values. Actual percentages depend on battery chemistry, age, temperature, and load.

### Battery Monitoring Best Practices

1. **Sample multiple readings** and average them for accuracy
2. **Wait for load to stabilize** before reading (e.g., after turning on backlight)
3. **Implement low battery warning** at ~20% (3.4V)
4. **Implement auto-shutdown** at ~5% (3.1V) to protect battery
5. **Consider load compensation** - voltage drops under load

### Advanced Battery Monitor Example

```cpp
#define BAT_ADC 34
#define LOW_BATTERY_THRESHOLD 3.4
#define CRITICAL_BATTERY_THRESHOLD 3.1
#define NUM_SAMPLES 10

float getFilteredBatteryVoltage() {
  float sum = 0;

  // Take multiple samples
  for (int i = 0; i < NUM_SAMPLES; i++) {
    int adcValue = analogRead(BAT_ADC);
    float voltage = (adcValue / 4095.0) * 3.3 * 2.0;
    sum += voltage;
    delay(10);
  }

  return sum / NUM_SAMPLES;
}

void checkBattery() {
  static unsigned long lastCheck = 0;

  // Check every 30 seconds
  if (millis() - lastCheck < 30000) return;
  lastCheck = millis();

  float voltage = getFilteredBatteryVoltage();
  int percent = getBatteryPercent();

  Serial.printf("Battery: %.2fV (%d%%)\n", voltage, percent);

  if (voltage < CRITICAL_BATTERY_THRESHOLD) {
    Serial.println("CRITICAL BATTERY - SHUTTING DOWN!");
    // Display warning on screen
    // Save important data
    // Initiate safe shutdown
    shutdownDevice();
  }
  else if (voltage < LOW_BATTERY_THRESHOLD) {
    Serial.println("Low battery warning!");
    // Display low battery icon on screen
    // Reduce backlight brightness
    // Disable non-essential features
    powerSavingMode();
  }
}

void powerSavingMode() {
  // Reduce backlight to 50%
  ledcWrite(0, 128);

  // Reduce CPU frequency
  setCpuFrequencyMhz(80);

  // Disable WiFi if not critical
  WiFi.mode(WIFI_OFF);
}

void shutdownDevice() {
  // Display shutdown message
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED);
  tft.setCursor(50, 200);
  tft.println("Battery Critical");
  tft.setCursor(50, 230);
  tft.println("Shutting down...");

  delay(3000);

  // Turn off backlight
  digitalWrite(27, LOW);

  // Enter deep sleep
  esp_deep_sleep_start();
}
```

## Power Optimization Strategies

### Reducing Power Consumption

**Backlight Control:**
```cpp
// Reduce backlight brightness (biggest power saver)
void setBacklight(int percent) {
  int pwmValue = map(percent, 0, 100, 0, 255);
  ledcWrite(0, pwmValue);
}

// At 50% brightness, saves ~70mA
setBacklight(50);
```

**CPU Frequency:**
```cpp
// Reduce CPU frequency when performance not critical
setCpuFrequencyMhz(80);   // ~30% power reduction
setCpuFrequencyMhz(160);  // ~15% power reduction
setCpuFrequencyMhz(240);  // Full performance (default)
```

**WiFi Management:**
```cpp
// Disable WiFi when not needed
WiFi.mode(WIFI_OFF);  // Saves ~100mA

// Use WiFi sleep modes
WiFi.setSleep(WIFI_PS_MIN_MODEM);  // Light sleep
WiFi.setSleep(WIFI_PS_MAX_MODEM);  // Deep sleep
```

**Bluetooth:**
```cpp
// Disable Bluetooth if not used
btStop();  // Saves ~50mA
```

**Peripheral Management:**
```cpp
// Disable unused peripherals
digitalWrite(AUDIO_EN, HIGH);  // Disable audio amp

// Put SD card to sleep when not in use
SD.end();
```

### Deep Sleep Mode

For battery-powered applications with infrequent use:

```cpp
void goToDeepSleep(uint64_t seconds) {
  // Turn off display
  digitalWrite(27, LOW);

  // Configure wakeup (e.g., button on IO0)
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);  // Wake on LOW

  // Or timer wakeup
  esp_sleep_enable_timer_wakeup(seconds * 1000000);

  // Enter deep sleep (uses < 1mA)
  esp_deep_sleep_start();
}

// Wake up after 60 seconds or button press
goToDeepSleep(60);
```

**Deep Sleep Power:**
- Current draw: < 1 mA
- Wake sources: Timer, external GPIO, touchpad, ULP
- RAM contents lost (except RTC memory)
- Must restart program on wake

### Light Sleep Mode

For temporary power saving while maintaining state:

```cpp
// Enter light sleep for 1 second
esp_sleep_enable_timer_wakeup(1000000);  // 1 second
esp_light_sleep_start();

// Program continues from here after wake
```

**Light Sleep Power:**
- Current draw: 1-5 mA
- RAM contents preserved
- Faster wake time
- Can wake on various sources

## Power Supply Circuit

### Voltage Regulation

The board includes a **ME6217 LDO** regulator:

- **Input**: 5V (from USB or battery management IC)
- **Output**: 3.3V (for ESP32 and peripherals)
- **Maximum Current**: 500 mA
- **Dropout Voltage**: ~300 mV

### Power Distribution

```
USB 5V ──┬──> Battery Charger IC (QD3958)
         │         │
         │         └──> Battery
         │              │
         └──────────────┴──> ME6217 LDO ──> 3.3V
                                                │
                                                ├──> ESP32
                                                ├──> Display Logic
                                                ├──> Touch Controller
                                                ├──> SD Card
                                                └──> Peripherals

Backlight LEDs ────────> Direct 5V (via driver)
```

### Protection Features

**Overcurrent Protection:**
- USB port current limiting
- LDO thermal shutdown

**Battery Protection:**
- Overcharge protection (stops at 4.24V)
- Over-discharge protection
- Short circuit protection
- Thermal protection (max 62°C)

**ESD Protection:**
- USB data lines protected
- GPIO pins have some ESD tolerance

## Power Troubleshooting

### Device Won't Power On

1. Check USB cable (must carry power)
2. Try different power source
3. Check for physical damage
4. If battery connected, check polarity and voltage

### Unexpected Resets / Brownouts

1. Insufficient power supply - use 1A+ adapter
2. USB cable resistance too high - use shorter cable
3. Battery depleted - charge or use USB
4. Too many peripherals drawing power simultaneously

### Battery Not Charging

1. Wrong battery type - must be 3.7V LiPo
2. Polarity reversed - **CHECK IMMEDIATELY**
3. Battery too depleted (< 2.5V) - may need special charger
4. Charge management IC damaged - requires repair

### Battery Drains Quickly

1. Reduce backlight brightness
2. Lower CPU frequency
3. Disable WiFi/Bluetooth when not needed
4. Check for battery degradation (old battery)
5. Verify no short circuits drawing excess current

## Best Practices

1. **Always use quality USB cables** rated for data and power
2. **Use adequate power supply** - 5V @ 1A minimum
3. **Monitor battery voltage** in software
4. **Implement low battery shutdown** to protect battery
5. **Never reverse battery polarity**
6. **Charge in well-ventilated area**
7. **Don't leave device on continuously** on battery - use sleep modes
8. **Store with battery at ~50% charge** for long-term storage
9. **Replace batteries showing degradation**
10. **Keep firmware updated** for optimal power management

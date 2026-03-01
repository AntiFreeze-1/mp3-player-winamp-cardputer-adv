# Required Library Versions

## The Problem

ESP8266Audio versions 2.0+ require the new ESP-IDF 5.x I2S API (`driver/i2s_std.h`),
which is NOT available in Arduino-ESP32 2.x (ESP-IDF 4.4).

If you install the latest ESP8266Audio (2.4.1) with the standard M5Stack board
package in Arduino IDE, you will get compilation errors:
```
fatal error: driver/i2s_std.h: No such file or directory
```

## Solution: Use ESP8266Audio 1.9.7

This is the last version that works with Arduino-ESP32 2.x / ESP-IDF 4.4.

## Tested Working Configuration

### Arduino IDE

| Component | Version | How to install |
|-----------|---------|----------------|
| **Arduino IDE** | 2.x | https://www.arduino.cc/en/software |
| **Board Package** | M5Stack 2.1.3 or esp32 2.0.17 | Board Manager |
| **M5Cardputer** | 1.1.1 | Library Manager |
| **M5Unified** | 0.2.10+ | Auto-installed with M5Cardputer |
| **M5GFX** | 0.2.17+ | Auto-installed with M5Cardputer |
| **ESP8266Audio** | **1.9.7** (NOT 2.x!) | Library Manager -> select version |

### How to install ESP8266Audio 1.9.7 in Arduino IDE

1. Open Arduino IDE
2. Go to **Sketch -> Include Library -> Manage Libraries**
3. Search for **ESP8266Audio**
4. Click on the version dropdown (DO NOT install latest!)
5. Select **1.9.7**
6. Click Install

If you already have a newer version installed:
1. Go to Library Manager
2. Find ESP8266Audio
3. Click Remove
4. Search again and install version **1.9.7**

### PlatformIO

Use the included `platformio.ini`:
```ini
platform = espressif32@6.9.0
lib_deps = 
    m5stack/M5Cardputer@^1.1.1
    earlephilhower/ESP8266Audio@1.9.7
```

### Board Settings (Arduino IDE)

| Setting | Value |
|---------|-------|
| Board | M5Stack-StampS3 (or M5Cardputer if available) |
| USB CDC On Boot | Enabled |
| Upload Speed | 921600 |
| CPU Frequency | 240 MHz |
| Flash Size | 8MB |

## Changes from Original

- `setFreeFont()` replaced with `setFont()` (deprecated in newer M5GFX)

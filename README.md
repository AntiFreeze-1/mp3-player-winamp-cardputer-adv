# 🎵 M5Mp3 Winamp Player for Cardputer-Adv

Winamp-style MP3 player for M5Stack Cardputer-Adv with ES8311 audio codec support.

## 🙏 Credits

This project is adapted from the original **M5Mp3** by [VolosR](https://github.com/VolosR/M5Mp3).

**Original Project:** https://github.com/VolosR/M5Mp3  
**Original Author:** VolosR  
**License:** Same as original M5Mp3 project

Many thanks to VolosR for creating the original Winamp-style interface and audio playback implementation! This adaptation adds support for M5Stack Cardputer-Adv with ES8311 audio codec.

## 📸 Screenshots

### Main Interface
![Main Interface](screenshots/2025-11-13%2016.40.08.jpg)

Winamp-style MP3 player running on M5Stack Cardputer-Adv

## ✨ Features

- ✅ **Winamp-style interface** - Classic retro look
- ✅ **MP3 playback** from SD card
- ✅ **ES8311 audio codec** support via M5Cardputer.Speaker
- ✅ **File browser** - Navigate through MP3 files
- ✅ **Volume control** - 5 levels (0-20)
- ✅ **Brightness control** - 5 levels
- ✅ **Battery indicator**
- ✅ **Visual equalizer** - Animated bars
- ✅ **FreeRTOS tasks** - Separate TFT and Audio tasks

## ⚠️ Important: Library Version Compatibility

**ESP8266Audio 2.0+ is NOT compatible** with the standard M5Stack Arduino board package (ESP-IDF 4.4).
You will get `fatal error: driver/i2s_std.h: No such file or directory` if you install the latest version.

**You MUST use ESP8266Audio version 1.9.7.**

See [LIBRARY_VERSIONS.md](LIBRARY_VERSIONS.md) for detailed installation instructions.

## 📦 Required Libraries

Install via Arduino Library Manager:

| Library | Version | Notes |
|---------|---------|-------|
| **ESP8266Audio** | **1.9.7** (NOT 2.x!) | [Install guide](LIBRARY_VERSIONS.md) |
| **M5Cardputer** | 1.1.1+ | Board support package |
| **M5Unified** | 0.2.10+ | Auto-installed with M5Cardputer |
| **M5GFX** | 0.2.17+ | Auto-installed with M5Cardputer |

~~**ESP32Time**~~ - Not required (replaced with simple millis() based timer)

## 🎮 Controls

| Key | Function |
|-----|----------|
| `A` | Play/Pause ▶️⏸️ |
| `N` | Next track ⏭️ |
| `P` | Previous track ⏮️ |
| `V` | Volume up 🔊 (cycles 5→10→15→20→5) |
| `L` | Brightness control 💡 |
| `B` | Random track 🎲 |
| `ENTER` | Restart current track 🔄 |
| `;` | Scroll up in list |
| `.` | Scroll down in list |

## 💾 SD Card Setup

1. Format SD card as **FAT32**
2. Copy MP3 files to root directory:
```
/
├── song1.mp3
├── song2.mp3
├── song3.mp3
└── ...
```

3. Insert SD card into Cardputer-Adv
4. Power on - player starts automatically!

## 🔧 Hardware Configuration

### SD Card Pins (SPI)
- SCK: GPIO 40
- MISO: GPIO 39
- MOSI: GPIO 14
- CS: GPIO 12

### Audio
- Uses built-in **ES8311** codec
- Output: Built-in speaker or 3.5mm jack (auto-switch)

## 📝 Technical Details

### Architecture

- **FreeRTOS Tasks:**
  - `Task_TFT` (Core 0) - Display and keyboard handling
  - `Task_Audio` (Core 1) - MP3 decoding and playback

- **Audio Pipeline:**
  ```
  SD Card → AudioFileSourceSD → AudioFileSourceID3 → 
  AudioGeneratorMP3 → AudioOutputM5CardputerSpeaker → 
  M5Cardputer.Speaker (ES8311)
  ```

- **Custom AudioOutput:**
  - Triple buffering for smooth playback
  - Uses `M5Cardputer.Speaker.playRaw()` for PCM output
  - Automatic buffer management

### Key Adaptations from Original M5Mp3

1. **Replaced ESP32-audioI2S** → **ESP8266Audio**
   - Better compatibility with M5Cardputer API
   - Cleaner integration

2. **Custom AudioOutput class:**
   - `AudioOutputM5CardputerSpeaker` wraps `M5Cardputer.Speaker`
   - Handles triple buffering
   - Manages playback state

3. **Removed I2S pins:**
   - No need for I2S_DOUT, I2S_BCLK, I2S_LRCK
   - Uses ES8311 via M5Cardputer.Speaker API

4. **Battery reading:**
   - Uses `M5Cardputer.Power.getBatteryVoltage()` instead of `analogRead(10)`

## 🐛 Troubleshooting

### No sound?
- Check SD card is inserted correctly
- Verify MP3 files are in root directory
- Check volume level (press `V` to increase)

### SD card not detected?
- Format as FAT32
- Check card is inserted fully
- Try different SD card

### Playback stuttering?
- Use lower bitrate MP3 files (128-192 kbps recommended)
- Check SD card speed class (Class 10 recommended)

### Files not showing?
- Only `.mp3` files are scanned
- Files must be in root directory (not in subfolders)
- Maximum 100 files supported

## 📚 Based On

- Original: https://github.com/VolosR/M5Mp3
- Adapted for: M5Stack Cardputer-Adv (ES8311)

## 📄 License

Same as original M5Mp3 project.

## 📋 Changelog

### v1.1.0 (2026-03-01)
- **Fixed:** Compilation error with ESP8266Audio 2.x (`driver/i2s_std.h` missing)
  - Root cause: ESP8266Audio 2.0+ requires ESP-IDF 5.x I2S API, incompatible with M5Stack board package
  - Solution: Pin ESP8266Audio to version 1.9.7
- **Fixed:** `setFreeFont()` deprecation warning in newer M5GFX versions (replaced with `setFont()`)
- **Added:** `LIBRARY_VERSIONS.md` with detailed version compatibility guide
- **Updated:** README with library version requirements

### v1.0.0 (Initial Release)
- Adapted from original M5Mp3 by VolosR
- ESP8266Audio instead of ESP32-audioI2S
- Custom AudioOutput for M5Cardputer.Speaker (ES8311)
- FreeRTOS dual-core architecture

## 🎯 Future Improvements

- [ ] Support for subdirectories
- [ ] WAV file support
- [ ] ID3 tag display (artist, title)
- [ ] Playlist support
- [ ] External display support (ILI9488)
- [ ] Shuffle mode
- [ ] Repeat mode


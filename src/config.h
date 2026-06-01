#pragma once
#include <stdint.h>

// ── I²C bus (keyboard + IMU + codec control) ──────────────────────────────
#define PIN_I2C_SDA     8
#define PIN_I2C_SCL     9
#define I2C_FREQ_HZ     400000

// ── TCA8418 keyboard controller ────────────────────────────────────────────
#define TCA8418_ADDR    0x34
#define PIN_KBD_IRQ     11

// ── I²S audio bus (ES8311 codec) ───────────────────────────────────────────
#define PIN_I2S_BCLK    41
#define PIN_I2S_LRCLK   43
#define PIN_I2S_DOUT    42   // ESP → codec (playback)
#define PIN_I2S_DIN     46   // codec → ESP (mic input, shares CLK with LRCLK)

// ── ES8311 codec ───────────────────────────────────────────────────────────
#define ES8311_ADDR     0x18
#define PIN_HP_DETECT   38   // headphone jack-detect GPIO (check schematic)

// ── microSD (SPI) ──────────────────────────────────────────────────────────
// Same SPI bus as the original Cardputer. The SPI pins MUST be configured
// explicitly before SD.begin() — the Arduino defaults do not match this board.
#define PIN_SD_SCK      40
#define PIN_SD_MISO     39
#define PIN_SD_MOSI     14
#define PIN_SD_CS       12
#define SD_SPI_FREQ_HZ  20000000   // 20 MHz; drop to 4 MHz if a card is flaky

// ── Battery ADC ────────────────────────────────────────────────────────────
#define PIN_BATT_ADC    4
#define BATT_ADC_VREF   3300   // mV
#define BATT_ADC_DIV    2      // voltage divider ratio

// ── Display ────────────────────────────────────────────────────────────────
// ST7789V2 driven by M5GFX — pins handled by M5Unified BSP

// ── Playback ───────────────────────────────────────────────────────────────
#define VOLUME_DEFAULT  10
#define VOLUME_MAX      30

// ── File scanning ──────────────────────────────────────────────────────────
#define SD_SCAN_MAX_DEPTH   4
#define LIBRARY_MAX_TRACKS  2000

// ── NVS namespace ──────────────────────────────────────────────────────────
#define NVS_NAMESPACE   "mp3player"

// ── Sleep timer options (minutes, 0 = off) ─────────────────────────────────
static const uint16_t SLEEP_TIMER_OPTIONS[] = {0, 15, 30, 45, 60, 90};
#define SLEEP_TIMER_COUNT 6

// ── FreeRTOS ───────────────────────────────────────────────────────────────
#define AUDIO_TASK_STACK    8192
#define UI_TASK_STACK       6144
#define KBD_TASK_STACK      2048
#define REC_TASK_STACK      6144
#define BAT_TASK_STACK      1024
#define KEY_QUEUE_LEN       16

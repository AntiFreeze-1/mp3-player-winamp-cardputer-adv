#pragma once
#include <stdint.h>

// ES8311 audio codec driver for the M5Stack Cardputer-ADV.
//
// The ADV routes I2S audio through an ES8311 codec (I2C addr 0x18) into an
// NS4150B amplifier. Unlike the original Cardputer (which has a plain I2S amp),
// the ES8311 MUST be register-configured over I2C or it produces no output,
// no matter what I2S data is sent. M5Unified 0.1.17 does not know this board,
// so we configure the codec ourselves.
//
// Clocking: the ESP32-audioI2S library provides BCLK/WS/DOUT only — there is
// NO separate MCLK pin — so the codec is told to derive its master clock from
// BCLK (register 0x01 = 0xB5). The init is rate-independent: the codec runs as
// an I2S slave and follows whatever BCLK/WS the SoC generates, so the sample
// rate can change per track without re-writing codec registers.
//
// There is no PA-enable GPIO on this board; output is gated purely by the
// codec's own output-enable bit (register 0x13).
//
// Register values verified against M5Unified's board_M5CardputerADV speaker
// init and Espressif's es8311.c reference driver.
class ES8311 {
public:
    // Configure the codec for 16-bit I2S DAC playback. Wire must already be
    // initialised (Wire.begin) before calling. Returns true if the codec
    // acknowledged on the I2C bus.
    static bool begin();

    // True if a device acknowledges at the ES8311 I2C address.
    static bool present();

    // Set the codec DAC output volume directly (0x00 = mute .. 0xFF = max,
    // 0xBF ~= 0 dB). Optional — software volume is handled in AudioEngine.
    static void setDacVolume(uint8_t vol);

private:
    static void writeReg(uint8_t reg, uint8_t val);
    static uint8_t readReg(uint8_t reg);
};

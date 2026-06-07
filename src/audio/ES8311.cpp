#include "ES8311.h"
#include <Wire.h>
#include <Arduino.h>
#include "../config.h"

// ── ES8311 register map (subset used here) ─────────────────────────────────
#define ES8311_REG_RESET        0x00  // reset / CSM power
#define ES8311_REG_CLK_MANAGER  0x01  // clock source + enables
#define ES8311_REG_CLK_DIV      0x02  // pre-multiplier / pre-divider
#define ES8311_REG_SDPIN        0x09  // serial data port (input) format
#define ES8311_REG_SDPOUT       0x0A  // serial data port (output) format
#define ES8311_REG_SYSTEM_0D    0x0D  // power up analog
#define ES8311_REG_SYSTEM_DAC   0x12  // power up DAC
#define ES8311_REG_SYSTEM_OUT   0x13  // enable output drive
#define ES8311_REG_DAC_VOLUME   0x32  // DAC volume
#define ES8311_REG_DAC_EQ       0x37  // DAC equalizer bypass
#define ES8311_REG_CHIP_ID1     0xFD  // expected 0x83
#define ES8311_REG_CHIP_ID2     0xFE  // expected 0x11

void ES8311::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

uint8_t ES8311::readReg(uint8_t reg) {
    Wire.beginTransmission(ES8311_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)ES8311_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

bool ES8311::present() {
    Wire.beginTransmission(ES8311_ADDR);
    return Wire.endTransmission() == 0;
}

bool ES8311::begin() {
    if (!present()) {
        Serial.println("[ES8311] codec not found on I2C");
        return false;
    }

    // Playback init sequence (matches M5Unified board_M5CardputerADV).
    writeReg(ES8311_REG_RESET,       0x80);  // reset, CSM power on
    delay(10);
    writeReg(ES8311_REG_CLK_MANAGER, 0xB5);  // MCLK = BCLK (no MCLK pin), clocks on
    writeReg(ES8311_REG_CLK_DIV,     0x18);  // pre_multi=3, pre_div=1
    writeReg(ES8311_REG_SDPIN,       0x0C);  // SDP IN: 16-bit, I2S format
    writeReg(ES8311_REG_SYSTEM_0D,   0x01);  // power up analog circuitry
    writeReg(ES8311_REG_SYSTEM_DAC,  0x00);  // power up DAC
    writeReg(ES8311_REG_SYSTEM_OUT,  0x10);  // enable output drive (HP/line)
    writeReg(ES8311_REG_DAC_VOLUME,  0xBF);  // DAC volume ~0 dB
    writeReg(ES8311_REG_DAC_EQ,      0x08);  // bypass DAC EQ

    uint8_t id1 = readReg(ES8311_REG_CHIP_ID1);
    uint8_t id2 = readReg(ES8311_REG_CHIP_ID2);
    Serial.printf("[ES8311] init OK, chip id = 0x%02X 0x%02X\n", id1, id2);
    return true;
}

void ES8311::setDacVolume(uint8_t vol) {
    writeReg(ES8311_REG_DAC_VOLUME, vol);
}

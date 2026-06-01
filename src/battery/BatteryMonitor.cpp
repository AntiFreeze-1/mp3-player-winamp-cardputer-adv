#include "BatteryMonitor.h"
#include <Arduino.h>
#include <driver/adc.h>

int      BatteryMonitor::s_pct     = 0;
uint32_t BatteryMonitor::s_mv      = 3700;
bool     BatteryMonitor::s_charging = false;

// LiPo discharge curve: mV → % (20 points)
static const struct { uint32_t mv; int pct; } LIPO_CURVE[] = {
    { 4200, 100 }, { 4150, 95 }, { 4100, 90 }, { 4050, 85 },
    { 4000, 80  }, { 3950, 75 }, { 3900, 70 }, { 3850, 65 },
    { 3800, 60  }, { 3750, 55 }, { 3700, 50 }, { 3650, 45 },
    { 3600, 40  }, { 3550, 35 }, { 3500, 28 }, { 3450, 20 },
    { 3400, 12  }, { 3350,  6 }, { 3300,  2 }, { 3000,  0 },
};
static const int LIPO_CURVE_LEN = sizeof(LIPO_CURVE) / sizeof(LIPO_CURVE[0]);

void BatteryMonitor::begin() {
    analogReadResolution(12);
    analogSetAttenuation(ADC_11db);
    pinMode(PIN_BATT_ADC, INPUT);
    update();
}

void BatteryMonitor::update() {
    // Average 8 samples to reduce ADC noise
    uint32_t raw = 0;
    for (int i = 0; i < 8; i++) raw += analogRead(PIN_BATT_ADC);
    raw /= 8;

    // Convert ADC reading to mV, accounting for voltage divider
    // ADC range 0–4095 = 0–3300 mV (with 11dB attenuation ~3.3V)
    uint32_t adc_mv = (raw * 3300UL) / 4095;
    s_mv = adc_mv * BATT_ADC_DIV;

    s_pct = voltToPercent(s_mv);

    // Charging detection: if voltage rising over 4.15V while on USB,
    // approximate by checking if GPIO USB_DETECT is high (if available).
    // Simple heuristic: if ADC shows >4.15V, likely charging.
    s_charging = (s_mv > 4150);
}

int BatteryMonitor::voltToPercent(uint32_t mv) {
    if (mv >= LIPO_CURVE[0].mv) return 100;
    if (mv <= LIPO_CURVE[LIPO_CURVE_LEN - 1].mv) return 0;

    for (int i = 0; i < LIPO_CURVE_LEN - 1; i++) {
        if (mv <= LIPO_CURVE[i].mv && mv > LIPO_CURVE[i + 1].mv) {
            // Linear interpolation
            uint32_t v0 = LIPO_CURVE[i].mv,    v1 = LIPO_CURVE[i + 1].mv;
            int      p0 = LIPO_CURVE[i].pct,   p1 = LIPO_CURVE[i + 1].pct;
            return p1 + (int)((int64_t)(mv - v1) * (p0 - p1) / (int64_t)(v0 - v1));
        }
    }
    return 0;
}

int      BatteryMonitor::percent()        { return s_pct; }
uint32_t BatteryMonitor::voltageMilliV()  { return s_mv; }
bool     BatteryMonitor::isCharging()     { return s_charging; }

void BatteryMonitor::task(void* arg) {
    for (;;) {
        update();
        vTaskDelay(pdMS_TO_TICKS(30000));  // every 30s
    }
}

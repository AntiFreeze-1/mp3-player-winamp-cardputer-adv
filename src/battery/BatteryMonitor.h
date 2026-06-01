#pragma once
#include <stdint.h>
#include "../config.h"

class BatteryMonitor {
public:
    static void    begin();
    static void    update();           // sample ADC, update state
    static int     percent();          // 0–100
    static uint32_t voltageMilliV();   // raw mV reading
    static bool    isCharging();

    // FreeRTOS task
    static void task(void* arg);

private:
    static int     s_pct;
    static uint32_t s_mv;
    static bool    s_charging;

    // LiPo voltage → percent lookup table
    static int voltToPercent(uint32_t mv);
};

#pragma once
#include <Wire.h>
#include "../types.h"
#include "../config.h"

// TCA8418 register addresses
#define TCA8418_REG_CFG         0x01
#define TCA8418_REG_INT_STAT    0x02
#define TCA8418_REG_KEY_LCK_EC  0x03
#define TCA8418_REG_KEY_EVENT_A 0x04
#define TCA8418_REG_KP_GPIO1    0x1D
#define TCA8418_REG_KP_GPIO2    0x1E
#define TCA8418_REG_KP_GPIO3    0x1F
#define TCA8418_REG_GPI_EM1     0x09
#define TCA8418_REG_GPI_EM2     0x0A
#define TCA8418_REG_GPI_EM3     0x0B
#define TCA8418_REG_KP_LCK_KMAX 0x03
#define TCA8418_CFG_KE_IEN      0x01
#define TCA8418_CFG_GPI_IEN     0x02
#define TCA8418_CFG_AI          0x08

class TCA8418 {
public:
    static void     begin();
    static bool     available();    // true if key event waiting
    static uint8_t  readEvent();    // raw key event byte from FIFO
    static void     clearInterrupt();

    // High-level: returns decoded KeyEvent; call repeatedly until code==NONE
    static KeyEvent nextKey();

    // ISR-safe flag set by interrupt handler
    static volatile bool irq_flag;

    static uint8_t readReg(uint8_t reg);
    static void    writeReg(uint8_t reg, uint8_t val);

private:
    static bool    s_fn_held;       // Fn modifier state
    static uint8_t rawToChar(uint8_t raw_code);
    static KeyCode rawToKeyCode(uint8_t raw_code, bool fn);
};

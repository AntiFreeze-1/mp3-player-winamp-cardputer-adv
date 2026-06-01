#include "TCA8418.h"
#include <Arduino.h>

volatile bool TCA8418::irq_flag = false;
bool          TCA8418::s_fn_held = false;

// Raw key codes for TCA8418 8×10 matrix (rows 0-7, cols 0-9)
// Adjust this map to match actual Cardputer-Adv keycap layout.
// Key event byte: bits[6:0] = key index (1-based), bit7 = pressed(1)/released(0)
// Key index = (row * 10) + col + 1  (for 10-column config)
static const struct {
    uint8_t  raw;   // TCA8418 key code (1-based, 0=none)
    KeyCode  code;
    char     ch;
    KeyCode  fn_code;  // when Fn held
} KEY_MAP[] = {
    // Row 0 — function/nav row
    { 1,  KeyCode::ESC,    0,   KeyCode::ESC       },
    { 2,  KeyCode::NONE,   '1', KeyCode::NONE      },
    { 3,  KeyCode::NONE,   '2', KeyCode::NONE      },
    { 4,  KeyCode::NONE,   '3', KeyCode::NONE      },
    { 5,  KeyCode::NONE,   '4', KeyCode::NONE      },
    { 6,  KeyCode::NONE,   '5', KeyCode::NONE      },
    { 7,  KeyCode::NONE,   '6', KeyCode::NONE      },
    { 8,  KeyCode::NONE,   '7', KeyCode::NONE      },
    { 9,  KeyCode::NONE,   '8', KeyCode::NONE      },
    { 10, KeyCode::NONE,   '9', KeyCode::NONE      },
    // Row 1
    { 11, KeyCode::NONE,   'q', KeyCode::NONE      },
    { 12, KeyCode::NONE,   'w', KeyCode::NONE      },
    { 13, KeyCode::FN_E,   'e', KeyCode::FN_E      },
    { 14, KeyCode::FN_R,   'r', KeyCode::FN_R      },
    { 15, KeyCode::FN_T,   't', KeyCode::FN_T      },
    { 16, KeyCode::NONE,   'y', KeyCode::NONE      },
    { 17, KeyCode::NONE,   'u', KeyCode::NONE      },
    { 18, KeyCode::NONE,   'i', KeyCode::NONE      },
    { 19, KeyCode::NONE,   'o', KeyCode::NONE      },
    { 20, KeyCode::NONE,   'p', KeyCode::NONE      },
    // Row 2
    { 21, KeyCode::NONE,   'a', KeyCode::NONE      },
    { 22, KeyCode::FN_S,   's', KeyCode::FN_S      },
    { 23, KeyCode::NONE,   'd', KeyCode::NONE      },
    { 24, KeyCode::FN_F,   'f', KeyCode::FN_F      },
    { 25, KeyCode::NONE,   'g', KeyCode::NONE      },
    { 26, KeyCode::NONE,   'h', KeyCode::NONE      },
    { 27, KeyCode::NONE,   'j', KeyCode::NONE      },
    { 28, KeyCode::NONE,   'k', KeyCode::NONE      },
    { 29, KeyCode::NONE,   'l', KeyCode::NONE      },
    { 30, KeyCode::ENTER,  0,   KeyCode::ENTER     },
    // Row 3
    { 31, KeyCode::NONE,   0,   KeyCode::NONE      },  // Fn modifier key
    { 32, KeyCode::NONE,   'z', KeyCode::NONE      },
    { 33, KeyCode::NONE,   'x', KeyCode::NONE      },
    { 34, KeyCode::NONE,   'c', KeyCode::NONE      },
    { 35, KeyCode::NONE,   'v', KeyCode::NONE      },
    { 36, KeyCode::NONE,   'b', KeyCode::NONE      },
    { 37, KeyCode::NONE,   'n', KeyCode::NONE      },
    { 38, KeyCode::FN_M,   'm', KeyCode::FN_M      },
    { 39, KeyCode::PLUS,   '+', KeyCode::NONE      },
    { 40, KeyCode::MINUS,  '-', KeyCode::NONE      },
    // Row 4 — navigation row
    { 41, KeyCode::UP,     0,   KeyCode::NONE      },
    { 42, KeyCode::DOWN,   0,   KeyCode::NONE      },
    { 43, KeyCode::LEFT,   0,   KeyCode::FN_LEFT   },
    { 44, KeyCode::RIGHT,  0,   KeyCode::FN_RIGHT  },
    { 45, KeyCode::NONE,   ' ', KeyCode::NONE      },
    { 46, KeyCode::FN_REC, 0,   KeyCode::FN_REC   },
    { 0,  KeyCode::NONE,   0,   KeyCode::NONE      },  // sentinel
};

static void IRAM_ATTR kbd_isr() {
    TCA8418::irq_flag = true;
}

uint8_t TCA8418::readReg(uint8_t reg) {
    Wire.beginTransmission(TCA8418_ADDR);
    Wire.write(reg);
    Wire.endTransmission(false);
    Wire.requestFrom((uint8_t)TCA8418_ADDR, (uint8_t)1);
    return Wire.available() ? Wire.read() : 0;
}

void TCA8418::writeReg(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(TCA8418_ADDR);
    Wire.write(reg);
    Wire.write(val);
    Wire.endTransmission();
}

void TCA8418::begin() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // Configure 8-row × 10-col key matrix
    // KP_GPIO1..3 set which pins are key matrix vs GPIO
    writeReg(TCA8418_REG_KP_GPIO1, 0xFF);  // rows 0-7 → keypad
    writeReg(TCA8418_REG_KP_GPIO2, 0xFF);  // cols 0-7 → keypad
    writeReg(TCA8418_REG_KP_GPIO3, 0x03);  // cols 8-9 → keypad

    // Enable key event FIFO interrupt, auto-increment
    writeReg(TCA8418_REG_CFG, TCA8418_CFG_AI | TCA8418_CFG_KE_IEN);

    // Clear any pending interrupts
    clearInterrupt();

    // Attach IRQ
    pinMode(PIN_KBD_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KBD_IRQ), kbd_isr, FALLING);
}

bool TCA8418::available() {
    if (!irq_flag) return false;
    uint8_t ec = readReg(TCA8418_REG_KEY_LCK_EC) & 0x0F;
    return ec > 0;
}

uint8_t TCA8418::readEvent() {
    return readReg(TCA8418_REG_KEY_EVENT_A);
}

void TCA8418::clearInterrupt() {
    writeReg(TCA8418_REG_INT_STAT, 0xFF);
    irq_flag = false;
}

KeyEvent TCA8418::nextKey() {
    KeyEvent ev = { KeyCode::NONE, false, 0 };

    uint8_t ec = readReg(TCA8418_REG_KEY_LCK_EC) & 0x0F;
    if (ec == 0) {
        irq_flag = false;
        clearInterrupt();
        return ev;
    }

    uint8_t raw = readEvent();
    bool pressed = (raw & 0x80) != 0;
    uint8_t key_code = raw & 0x7F;

    // Fn key (raw code 31) — track modifier state
    if (key_code == 31) {
        s_fn_held = pressed;
        return ev;  // Don't emit a key event for the modifier itself
    }

    // Look up in map
    for (int i = 0; KEY_MAP[i].raw != 0; i++) {
        if (KEY_MAP[i].raw == key_code) {
            ev.pressed = pressed;
            if (s_fn_held && KEY_MAP[i].fn_code != KeyCode::NONE) {
                ev.code = KEY_MAP[i].fn_code;
                ev.ch   = 0;
            } else {
                ev.code = KEY_MAP[i].code;
                ev.ch   = KEY_MAP[i].ch;
            }
            return ev;
        }
    }

    return ev;
}

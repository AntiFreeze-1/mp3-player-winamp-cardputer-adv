#include "TCA8418.h"
#include <Arduino.h>

volatile bool TCA8418::irq_flag = false;
bool          TCA8418::s_fn_held = false;

// TCA8418 key codes for the Cardputer-ADV keyboard.
//
// The ADV keyboard is wired so the chip scans by column: the event code for a
// physical key is  (col * 10) + row + 1  with rows 0-7. The codes are therefore
// NOT sequential across a visual row. This table is the empirically-verified
// mapping from the M5Stack Cardputer-ADV layout (matches m5stack's CardputerADV
// reference firmware), e.g. code 42 = 'i', code 53 = Up, code 63 = Enter.
//
// Key code 3 is the Fn modifier and is handled specially in nextKey().
static const struct {
    uint8_t  raw;   // TCA8418 key code (1-based, 0=none)
    KeyCode  code;
    char     ch;
    KeyCode  fn_code;  // emitted instead of `code` when Fn is held
} KEY_MAP[] = {
    // ── Column 0 ──────────────────────────────────────────────────────────
    { 1,  KeyCode::ESC,    0,    KeyCode::ESC     },  // esc
    { 2,  KeyCode::NONE,   '\t', KeyCode::NONE    },  // tab
    // 3 = Fn (handled in nextKey)
    { 4,  KeyCode::NONE,   0,    KeyCode::NONE    },  // ctrl
    { 5,  KeyCode::NONE,   '1',  KeyCode::NONE    },
    { 6,  KeyCode::NONE,   'q',  KeyCode::NONE    },
    { 7,  KeyCode::NONE,   0,    KeyCode::NONE    },  // Aa / caps
    { 8,  KeyCode::NONE,   0,    KeyCode::NONE    },  // opt
    // ── Column 1 ──────────────────────────────────────────────────────────
    { 11, KeyCode::NONE,   '2',  KeyCode::NONE    },
    { 12, KeyCode::NONE,   'w',  KeyCode::NONE    },
    { 13, KeyCode::NONE,   'a',  KeyCode::NONE    },
    { 14, KeyCode::NONE,   0,    KeyCode::NONE    },  // alt
    { 15, KeyCode::NONE,   '3',  KeyCode::NONE    },
    { 16, KeyCode::NONE,   'e',  KeyCode::FN_E    },
    { 17, KeyCode::NONE,   's',  KeyCode::FN_S    },
    { 18, KeyCode::NONE,   'z',  KeyCode::NONE    },
    // ── Column 2 ──────────────────────────────────────────────────────────
    { 21, KeyCode::NONE,   '4',  KeyCode::NONE    },
    { 22, KeyCode::NONE,   'r',  KeyCode::FN_R    },
    { 23, KeyCode::NONE,   'd',  KeyCode::NONE    },
    { 24, KeyCode::NONE,   'x',  KeyCode::NONE    },
    { 25, KeyCode::NONE,   '5',  KeyCode::NONE    },
    { 26, KeyCode::NONE,   't',  KeyCode::FN_T    },
    { 27, KeyCode::NONE,   'f',  KeyCode::FN_F    },
    { 28, KeyCode::NONE,   'c',  KeyCode::NONE    },
    // ── Column 3 ──────────────────────────────────────────────────────────
    { 31, KeyCode::NONE,   '6',  KeyCode::NONE    },
    { 32, KeyCode::NONE,   'y',  KeyCode::NONE    },
    { 33, KeyCode::NONE,   'g',  KeyCode::NONE    },
    { 34, KeyCode::NONE,   'v',  KeyCode::FN_REC  },  // Fn+V = voice recorder
    { 35, KeyCode::NONE,   '7',  KeyCode::NONE    },
    { 36, KeyCode::NONE,   'u',  KeyCode::NONE    },
    { 37, KeyCode::NONE,   'h',  KeyCode::NONE    },
    { 38, KeyCode::NONE,   'b',  KeyCode::NONE    },
    // ── Column 4 ──────────────────────────────────────────────────────────
    { 41, KeyCode::NONE,   '8',  KeyCode::NONE    },
    { 42, KeyCode::NONE,   'i',  KeyCode::NONE    },
    { 43, KeyCode::NONE,   'j',  KeyCode::NONE    },
    { 44, KeyCode::NONE,   'n',  KeyCode::NONE    },
    { 45, KeyCode::NONE,   '9',  KeyCode::NONE    },
    { 46, KeyCode::NONE,   'o',  KeyCode::FN_O    },
    { 47, KeyCode::NONE,   'k',  KeyCode::NONE    },
    { 48, KeyCode::NONE,   'm',  KeyCode::FN_M    },
    // ── Column 5 ──────────────────────────────────────────────────────────
    { 51, KeyCode::NONE,   '0',  KeyCode::NONE    },
    { 52, KeyCode::NONE,   'p',  KeyCode::NONE    },
    { 53, KeyCode::UP,     0,    KeyCode::NONE    },  // up arrow
    { 54, KeyCode::LEFT,   0,    KeyCode::FN_LEFT },  // left arrow
    { 55, KeyCode::MINUS,  '-',  KeyCode::NONE    },
    { 56, KeyCode::NONE,   '[',  KeyCode::NONE    },
    { 57, KeyCode::NONE,   '\'', KeyCode::NONE    },
    { 58, KeyCode::DOWN,   0,    KeyCode::NONE    },  // down arrow
    // ── Column 6 ──────────────────────────────────────────────────────────
    { 61, KeyCode::PLUS,   '=',  KeyCode::NONE    },
    { 62, KeyCode::NONE,   ']',  KeyCode::NONE    },
    { 63, KeyCode::ENTER,  0,    KeyCode::ENTER   },  // enter
    { 64, KeyCode::RIGHT,  0,    KeyCode::FN_RIGHT},  // right arrow
    { 65, KeyCode::NONE,   0x08, KeyCode::NONE    },  // del / backspace
    { 66, KeyCode::NONE,   '\\', KeyCode::NONE    },
    { 67, KeyCode::NONE,   ' ',  KeyCode::NONE    },  // space
    { 68, KeyCode::NONE,   ' ',  KeyCode::NONE    },  // space
    { 0,  KeyCode::NONE,   0,    KeyCode::NONE    },  // sentinel
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
    // Poll the key-event counter directly rather than gating on the hardware
    // IRQ flag. The INT line (GPIO11) wiring/polarity isn't guaranteed on this
    // board, and a missed FALLING edge would silently wedge input forever.
    // The counter (lower nibble of KEY_LCK_EC) is the source of truth.
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

    // Fn key (raw code 3) — track modifier state
    if (key_code == 3) {
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

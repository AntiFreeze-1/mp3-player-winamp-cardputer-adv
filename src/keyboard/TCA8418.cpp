#include "TCA8418.h"
#include <Arduino.h>

volatile bool TCA8418::irq_flag = false;
bool          TCA8418::s_fn_held = false;

// TCA8418 key codes for the Cardputer-ADV keyboard.
//
// Reference: m5stack/M5Cardputer-UserDemo CardputerADV branch
// main/hal/keyboard/keyboard.cpp (keyboard.cpp's get_key_event_raw + remap)
//
// The TCA8418 always uses a stride of 10 regardless of configured cols:
//   key_code = row * 10 + col + 1
// (row/col are the RAW scan indices before the remap step.)
//
// The reference firmware then applies a remap:
//   logical_col = raw_row * 2 + (raw_col > 3 ? 1 : 0)
//   logical_row = raw_col % 4
//
// Working that back to key codes, the physical layout is:
//
// Row 0:  `   1   2   3   4   5   6   7   8   9   0   -   =   del
// Row 1: tab  q   w   e   r   t   y   u   i   o   p   [   ]    \
// Row 2:  Fn  Aa  a   s   d   f   g   h   j   k   l   ;   '  enter
// Row 3: ctrl opt alt  z   x   c   v   b   n   m   ,   .   /  space
//
// Key 3 (Fn) is the modifier; tracked in s_fn_held, never forwarded as an event.
// Navigation: , . / ; → LEFT DOWN RIGHT UP (without Fn, for easy list browsing)
// Fn+,  Fn+/  → seek back/forward
// Backtick (code 1) → ESC (no physical ESC key on this board)

static const struct {
    uint8_t  raw;      // TCA8418 key code (1-based, 0=sentinel)
    KeyCode  code;     // emitted normally
    char     ch;       // printable character (0 if none)
    KeyCode  fn_code;  // emitted instead when Fn is held (NONE = same as normal)
} KEY_MAP[] = {
    // ── Row 0, cols 0-3  (codes 1-4) ──────────────────────────────────────
    { 1,  KeyCode::ESC,   '`',  KeyCode::ESC    },  // ` / ESC  (bare ` = ESC for player)
    { 2,  KeyCode::NONE,  '\t', KeyCode::NONE   },  // tab
    // 3 = Fn modifier — handled in nextKey(), not in this table
    { 4,  KeyCode::NONE,  0,    KeyCode::NONE   },  // ctrl
    // ── Row 0, cols 4-7  (codes 5-8) ──────────────────────────────────────
    { 5,  KeyCode::NONE,  '1',  KeyCode::NONE   },
    { 6,  KeyCode::NONE,  'q',  KeyCode::NONE   },
    { 7,  KeyCode::NONE,  0,    KeyCode::NONE   },  // Aa / shift
    { 8,  KeyCode::NONE,  0,    KeyCode::NONE   },  // opt
    // ── Row 1, cols 0-3  (codes 11-14) ────────────────────────────────────
    { 11, KeyCode::NONE,  '2',  KeyCode::NONE   },
    { 12, KeyCode::NONE,  'w',  KeyCode::NONE   },
    { 13, KeyCode::NONE,  'a',  KeyCode::NONE   },
    { 14, KeyCode::NONE,  0,    KeyCode::NONE   },  // alt
    // ── Row 1, cols 4-7  (codes 15-18) ────────────────────────────────────
    { 15, KeyCode::NONE,  '3',  KeyCode::NONE   },
    { 16, KeyCode::NONE,  'e',  KeyCode::FN_E   },  // e / EQ preset
    { 17, KeyCode::NONE,  's',  KeyCode::FN_S   },  // s / Shuffle
    { 18, KeyCode::NONE,  'z',  KeyCode::NONE   },
    // ── Row 2, cols 0-3  (codes 21-24) ────────────────────────────────────
    { 21, KeyCode::NONE,  '4',  KeyCode::NONE   },
    { 22, KeyCode::NONE,  'r',  KeyCode::FN_R   },  // r / Repeat
    { 23, KeyCode::NONE,  'd',  KeyCode::NONE   },
    { 24, KeyCode::NONE,  'x',  KeyCode::NONE   },
    // ── Row 2, cols 4-7  (codes 25-28) ────────────────────────────────────
    { 25, KeyCode::NONE,  '5',  KeyCode::NONE   },
    { 26, KeyCode::NONE,  't',  KeyCode::FN_T   },  // t / sleep Timer
    { 27, KeyCode::NONE,  'f',  KeyCode::FN_F   },  // f / FullSound
    { 28, KeyCode::NONE,  'c',  KeyCode::NONE   },
    // ── Row 3, cols 0-3  (codes 31-34) ────────────────────────────────────
    { 31, KeyCode::NONE,  '6',  KeyCode::NONE   },
    { 32, KeyCode::NONE,  'y',  KeyCode::NONE   },
    { 33, KeyCode::NONE,  'g',  KeyCode::NONE   },
    { 34, KeyCode::NONE,  'v',  KeyCode::FN_REC },  // v / Voice recorder
    // ── Row 3, cols 4-7  (codes 35-38) ────────────────────────────────────
    { 35, KeyCode::NONE,  '7',  KeyCode::NONE   },
    { 36, KeyCode::NONE,  'u',  KeyCode::NONE   },
    { 37, KeyCode::NONE,  'h',  KeyCode::NONE   },
    { 38, KeyCode::NONE,  'b',  KeyCode::NONE   },
    // ── Row 4, cols 0-3  (codes 41-44) ────────────────────────────────────
    { 41, KeyCode::NONE,  '8',  KeyCode::NONE   },
    { 42, KeyCode::NONE,  'i',  KeyCode::NONE   },
    { 43, KeyCode::NONE,  'j',  KeyCode::NONE   },
    { 44, KeyCode::NONE,  'n',  KeyCode::NONE   },
    // ── Row 4, cols 4-7  (codes 45-48) ────────────────────────────────────
    { 45, KeyCode::NONE,  '9',  KeyCode::NONE   },
    { 46, KeyCode::NONE,  'o',  KeyCode::FN_O   },  // o / mOnO
    { 47, KeyCode::NONE,  'k',  KeyCode::NONE   },
    { 48, KeyCode::NONE,  'm',  KeyCode::FN_M   },  // m / Mute
    // ── Row 5, cols 0-3  (codes 51-54) ────────────────────────────────────
    { 51, KeyCode::NONE,  '0',  KeyCode::NONE   },
    { 52, KeyCode::NONE,  'p',  KeyCode::NONE   },
    { 53, KeyCode::NONE,  'l',  KeyCode::NONE   },
    { 54, KeyCode::LEFT,  ',',  KeyCode::FN_LEFT},  // , → prev track / seek back
    // ── Row 5, cols 4-7  (codes 55-58) ────────────────────────────────────
    { 55, KeyCode::MINUS, '-',  KeyCode::NONE   },  // - → volume down
    { 56, KeyCode::NONE,  '[',  KeyCode::NONE   },
    { 57, KeyCode::UP,    ';',  KeyCode::NONE   },  // ; → cursor up
    { 58, KeyCode::DOWN,  '.',  KeyCode::NONE   },  // . → cursor down
    // ── Row 6, cols 0-3  (codes 61-64) ────────────────────────────────────
    { 61, KeyCode::PLUS,  '=',  KeyCode::NONE   },  // = → volume up
    { 62, KeyCode::NONE,  ']',  KeyCode::NONE   },
    { 63, KeyCode::NONE,  '\'', KeyCode::NONE   },  // apostrophe
    { 64, KeyCode::RIGHT, '/',  KeyCode::FN_RIGHT}, // / → next track / seek fwd
    // ── Row 6, cols 4-7  (codes 65-68) ────────────────────────────────────
    { 65, KeyCode::NONE,  0x08, KeyCode::NONE   },  // del / backspace
    { 66, KeyCode::NONE,  '\\', KeyCode::NONE   },
    { 67, KeyCode::ENTER, 0,    KeyCode::ENTER  },  // enter
    { 68, KeyCode::ENTER, ' ',  KeyCode::NONE   },  // space → also play/pause
    { 0,  KeyCode::NONE,  0,    KeyCode::NONE   },  // sentinel
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
    // Wire already started in setup(); don't call Wire.begin() again here.

    // 7 rows (R0-R6), 8 columns (C0-C7) — matches reference firmware matrix(7,8).
    // Enabling extra rows/cols beyond what's physically wired can generate
    // phantom key events on floating pins.
    writeReg(TCA8418_REG_KP_GPIO1, 0x7F);  // R0-R6 → keypad (7 rows)
    writeReg(TCA8418_REG_KP_GPIO2, 0xFF);  // C0-C7 → keypad (8 cols)
    writeReg(TCA8418_REG_KP_GPIO3, 0x00);  // C8-C9 → GPIO (unused)

    // Enable key event FIFO interrupt
    writeReg(TCA8418_REG_CFG, TCA8418_CFG_KE_IEN);

    // Clear any pending interrupts
    clearInterrupt();

    // Attach IRQ (kept for efficiency — available() polled directly as backup)
    pinMode(PIN_KBD_IRQ, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_KBD_IRQ), kbd_isr, FALLING);
}

bool TCA8418::available() {
    // Poll the key-event counter directly rather than gating on the IRQ flag.
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
        clearInterrupt();
        return ev;
    }

    uint8_t raw     = readEvent();
    bool    pressed = (raw & 0x80) != 0;
    uint8_t kc      = raw & 0x7F;

    // Fn modifier (code 3) — update state, do not forward as a key event
    if (kc == 3) {
        s_fn_held = pressed;
        return ev;
    }

    for (int i = 0; KEY_MAP[i].raw != 0; i++) {
        if (KEY_MAP[i].raw == kc) {
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

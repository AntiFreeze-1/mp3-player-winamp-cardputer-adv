#pragma once
#include <stdint.h>
#include <Arduino.h>

// ── Track metadata ─────────────────────────────────────────────────────────
struct TrackInfo {
    char path[256];
    char title[128];
    char artist[128];
    char album[128];
    uint8_t  track_num;
    uint32_t duration_ms;
    bool     has_art;
};

// ── Playback state ─────────────────────────────────────────────────────────
enum class PlaybackState : uint8_t {
    STOPPED = 0,
    PLAYING,
    PAUSED,
};

// ── Repeat mode ────────────────────────────────────────────────────────────
enum class RepeatMode : uint8_t {
    OFF  = 0,
    ONE  = 1,
    ALL  = 2,
};

// ── EQ presets ─────────────────────────────────────────────────────────────
enum class EQPreset : uint8_t {
    FLAT      = 0,
    ROCK      = 1,
    POP       = 2,
    JAZZ      = 3,
    CLASSICAL = 4,
    HIP_HOP   = 5,
    FUNK      = 6,
    TECHNO    = 7,
    CUSTOM    = 8,
    EQ_COUNT
};

static const char* EQ_PRESET_NAMES[] = {
    "Flat", "Rock", "Pop", "Jazz", "Classical",
    "Hip-Hop", "Funk", "Techno", "Custom"
};

// ── Key codes ──────────────────────────────────────────────────────────────
enum class KeyCode : uint8_t {
    NONE = 0,
    UP, DOWN, LEFT, RIGHT,
    ENTER, ESC,
    PLUS, MINUS,
    FN_LEFT, FN_RIGHT,
    FN_S, FN_R, FN_E, FN_F, FN_T, FN_M, FN_O, FN_REC,
    OK_LONG,
    CHAR_A = 0x40,  // printable characters start here
};

struct KeyEvent {
    KeyCode code;
    bool    pressed;   // true = key down, false = key up
    char    ch;        // printable char (0 if not printable)
};

// ── App screens ────────────────────────────────────────────────────────────
enum class Screen : uint8_t {
    NOW_PLAYING = 0,
    LIBRARY,
    PLAYLIST,
    EQ_SETTINGS,
    SLEEP_TIMER,
    VOICE_RECORDER,
    RECORDINGS,
    SETTINGS,
};

// ── Application state (shared across tasks via mutex) ──────────────────────
struct AppState {
    PlaybackState playback;
    RepeatMode    repeat;
    bool          shuffle;
    uint8_t       volume;          // 0–30
    bool          muted;
    EQPreset      eq_preset;
    int8_t        eq_custom[5];    // bands: 60Hz 250Hz 1kHz 4kHz 12kHz, ±12 dB
    bool          fullsound;
    bool          mono;            // mono-sum L/R to both channels
    uint8_t       sleep_timer_idx; // index into SLEEP_TIMER_OPTIONS
    uint32_t      sleep_deadline;  // millis() when to sleep, 0=off
    bool          headphones_in;
    int           battery_pct;     // 0–100
    bool          charging;
    Screen        current_screen;
    int           lib_cursor;      // cursor position in library list
    int           current_track_idx;
    uint32_t      track_pos_ms;    // current playback position
};

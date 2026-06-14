#include "UIManager.h"
#include <M5Unified.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "../config.h"
#include "../battery/BatteryMonitor.h"

M5Canvas UIManager::canvas(&M5.Display);
bool     UIManager::s_art_loaded  = false;
bool     UIManager::s_canvas_ok   = false;
char     UIManager::s_notif[64]   = {0};
uint32_t UIManager::s_notif_until = 0;
int      UIManager::s_lib_scroll  = 0;

static constexpr int W      = 240;
static constexpr int H      = 135;
static constexpr int HINT_H = 9;   // reserved at bottom for key hints

// ── Fixed colors (not themed) ─────────────────────────────────────────────
static constexpr uint16_t COL_BG     = 0x0000;
static constexpr uint16_t COL_FG     = 0xFFFF;
static constexpr uint16_t COL_GREEN  = 0x07E0;
static constexpr uint16_t COL_RED    = 0xF800;
static constexpr uint16_t COL_YELLOW = 0xFFE0;

// ── Themed colors (updated at the start of every draw() call) ────────────
static uint16_t COL_ACCENT   = 0x2104;
static uint16_t COL_SELECTED = 0x3616;
static uint16_t COL_DIM      = 0x7BEF;

// ── Theme table: Gray / Red / Yellow ─────────────────────────────────────
static const struct { uint16_t accent, selected, dim; } THEMES[3] = {
    { 0x2104, 0x3616, 0x7BEF },  // Gray   (default)
    { 0x6000, 0xA000, 0xD000 },  // Red
    { 0x6300, 0x9480, 0xC600 },  // Yellow
};

static constexpr float PI_F = 3.14159265f;

// ── Animation helpers (drawn inside the 100×121 left panel, cy ≈ 69) ─────

static void drawVinyl(M5Canvas& cvs, int cx, int cy, bool playing, uint32_t ms) {
    int r = 42;
    cvs.fillCircle(cx, cy, r, COL_BG);
    // Grooves
    for (int g = r - 2; g > 18; g -= 3)
        cvs.drawCircle(cx, cy, g, COL_ACCENT);
    // Outer rim
    cvs.drawCircle(cx, cy, r, COL_DIM);
    // Center label
    cvs.fillCircle(cx, cy, 16, COL_ACCENT);
    // Rotating line on label when playing
    if (playing) {
        float a = (float)(ms % 3000) / 3000.0f * 2.0f * PI_F;
        cvs.drawLine(cx, cy,
                     cx + (int)(11.0f * cosf(a)),
                     cy + (int)(11.0f * sinf(a)), COL_FG);
    }
    // Center hole
    cvs.fillCircle(cx, cy, 3, COL_BG);
    cvs.drawCircle(cx, cy, 3, COL_DIM);
}

static void drawCD(M5Canvas& cvs, int cx, int cy, bool playing, uint32_t ms) {
    int r = 42;
    // Disc body
    cvs.fillCircle(cx, cy, r, COL_DIM);
    cvs.fillCircle(cx, cy, r - 5, COL_ACCENT);
    cvs.drawCircle(cx, cy, r,     COL_FG);
    cvs.drawCircle(cx, cy, r - 5, COL_DIM);
    // Rotating highlight
    if (playing) {
        float a = (float)(ms % 2000) / 2000.0f * 2.0f * PI_F;
        int hx = cx + (int)(26.0f * cosf(a));
        int hy = cy + (int)(26.0f * sinf(a));
        cvs.fillRect(hx - 3, hy - 1, 7, 3, COL_FG);
    }
    // Hub and hole
    cvs.fillCircle(cx, cy, 10, COL_SELECTED);
    cvs.drawCircle(cx, cy, 10, COL_DIM);
    cvs.fillCircle(cx, cy,  4, COL_BG);
    cvs.drawCircle(cx, cy,  4, COL_DIM);
}

static void drawCassette(M5Canvas& cvs, int cx, int cy, bool playing, uint32_t ms) {
    // Body
    cvs.fillRoundRect(cx - 44, cy - 26, 88, 52, 5, COL_ACCENT);
    cvs.drawRoundRect(cx - 44, cy - 26, 88, 52, 5, COL_DIM);
    // Tape window
    cvs.fillRect(cx - 26, cy - 11, 52, 22, COL_BG);
    cvs.drawRect(cx - 26, cy - 11, 52, 22, COL_DIM);
    // Bottom label line
    cvs.drawFastHLine(cx - 36, cy + 17, 72, COL_DIM);
    // Two reels
    float angle = playing ? (float)(ms % 1000) / 1000.0f * 2.0f * PI_F : 0.0f;
    for (int side = -1; side <= 1; side += 2) {
        int rx = cx + side * 16, ry = cy;
        cvs.fillCircle(rx, ry, 8, COL_SELECTED);
        cvs.drawCircle(rx, ry, 8, COL_DIM);
        cvs.fillCircle(rx, ry, 2, COL_BG);
        if (playing) {
            for (int sp = 0; sp < 3; sp++) {
                float sa = angle + sp * (2.0f * PI_F / 3.0f);
                cvs.drawLine(rx, ry,
                             rx + (int)(6.0f * cosf(sa)),
                             ry + (int)(6.0f * sinf(sa)), COL_FG);
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────

void UIManager::begin() {
    M5.Display.setRotation(1);
    M5.Display.fillScreen(COL_BG);

    canvas.setColorDepth(16);
    s_canvas_ok = canvas.createSprite(W, H) != nullptr;
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextSize(1);
}

void UIManager::draw(const AppState& state, const Library& lib,
                     const PlaylistManager& playlist) {
    (void)playlist;

    // Apply theme
    uint8_t tidx = state.theme_idx < 3 ? state.theme_idx : 0;
    COL_ACCENT   = THEMES[tidx].accent;
    COL_SELECTED = THEMES[tidx].selected;
    COL_DIM      = THEMES[tidx].dim;

    if (!s_canvas_ok) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.setTextColor(COL_FG, COL_BG);
        M5.Display.setCursor(4, 4);
        M5.Display.printf("Vol: %d", state.volume);
        return;
    }

    canvas.fillScreen(COL_BG);

    switch (state.current_screen) {
        case Screen::NOW_PLAYING:    drawNowPlaying(state, lib);   break;
        case Screen::LIBRARY:        drawLibrary(state, lib);      break;
        case Screen::EQ_SETTINGS:    drawEQSettings(state);        break;
        case Screen::SLEEP_TIMER:    drawSleepTimer(state);        break;
        case Screen::SCREEN_TIMEOUT: drawScreenTimeout(state);     break;
        case Screen::SETTINGS:       drawSettings(state);          break;
        case Screen::VOICE_RECORDER: break;
        default: break;
    }

    // Hint bar at bottom of every interactive screen
    switch (state.current_screen) {
        case Screen::NOW_PLAYING:
            drawHintBar(",=prev /=next  Entr=pause  =/- vol  ESC=list");   break;
        case Screen::LIBRARY:
            drawHintBar(";=up .=dn  Entr=open  ESC=back  =/- vol");        break;
        case Screen::SLEEP_TIMER:
        case Screen::SCREEN_TIMEOUT:
            drawHintBar(";=up .=dn  Entr=set  ESC=cancel");                break;
        case Screen::SETTINGS:
            drawHintBar(";=up .=dn  Entr=change  ESC=back");               break;
        default: break;
    }

    drawStatusBar(state);

    if (s_notif[0] && millis() < s_notif_until) {
        canvas.fillRoundRect(20, H/2 - 10, W - 40, 20, 4, COL_ACCENT);
        canvas.setTextColor(COL_FG, COL_ACCENT);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.drawString(s_notif, W/2, H/2);
        canvas.setTextDatum(textdatum_t::top_left);
        canvas.setTextColor(COL_FG, COL_BG);
    } else if (s_notif[0] && millis() >= s_notif_until) {
        s_notif[0] = '\0';
    }

    canvas.pushSprite(0, 0);
}

void UIManager::drawStatusBar(const AppState& state) {
    canvas.fillRect(0, 0, W, 12, COL_ACCENT);
    canvas.setTextColor(COL_FG, COL_ACCENT);

    // Battery
    char batt[12];
    if (state.charging) snprintf(batt, sizeof(batt), "CHG %d%%", state.battery_pct);
    else                snprintf(batt, sizeof(batt), "%d%%",     state.battery_pct);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(batt, W - 2, 2);

    // Repeat / shuffle / mode icons
    canvas.setTextDatum(textdatum_t::top_left);
    if (state.repeat == RepeatMode::ONE)      canvas.drawString("[1]", 2, 2);
    else if (state.repeat == RepeatMode::ALL) canvas.drawString("[A]", 2, 2);
    if (state.shuffle)       canvas.drawString("~",   22, 2);
    if (state.muted)         canvas.drawString("M",   32, 2);
    if (state.mono)          canvas.drawString("MNO", 42, 2);
    if (state.headphones_in) canvas.drawString("HP",  66, 2);

    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_left);
}

void UIManager::drawHintBar(const char* text) {
    // Clear over any content that extended into the hint area (e.g. left panel fill)
    canvas.fillRect(0, H - HINT_H - 1, W, HINT_H + 1, COL_BG);
    canvas.drawFastHLine(0, H - HINT_H - 1, W, COL_DIM);
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.setTextDatum(textdatum_t::bottom_left);
    canvas.drawString(text, 2, H - 1);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.setTextColor(COL_FG, COL_BG);
}

void UIManager::drawNowPlaying(const AppState& state, const Library& lib) {
    (void)lib;

    bool playing = (state.playback == PlaybackState::PLAYING);
    uint32_t ms  = millis();

    // Left panel background
    int panel_cy = 14 + (H - HINT_H - 1 - 14) / 2;  // vertical center of content area
    canvas.fillRect(0, 14, 100, H - 14, COL_ACCENT);

    // Animation
    switch (state.anim_type) {
        default:
        case 0: drawVinyl   (canvas, 50, panel_cy, playing, ms); break;
        case 1: drawCD      (canvas, 50, panel_cy, playing, ms); break;
        case 2: drawCassette(canvas, 50, panel_cy, playing, ms); break;
    }

    int tx = 104;
    int y  = 16;
    canvas.setTextColor(COL_FG, COL_BG);

    // Track name
    char name[48];
    strncpy(name, state.current_track_name[0] ? state.current_track_name : "No track",
            sizeof(name) - 1);
    name[sizeof(name)-1] = '\0';
    canvas.drawString(name, tx, y); y += 12;

    // Directory (dim)
    canvas.setTextColor(COL_DIM, COL_BG);
    char dir[40] = "/";
    const char* slash = strrchr(state.current_track_path, '/');
    if (slash && slash != state.current_track_path) {
        int len = (int)(slash - state.current_track_path);
        if (len > 39) len = 39;
        strncpy(dir, state.current_track_path, len);
        dir[len] = '\0';
    }
    canvas.drawString(dir, tx, y); y += 11;

    // Progress bar — pulsing dot stays inside the bar (bar_w-3 keeps 4px dot within bounds)
    int bar_w = W - tx - 2;
    canvas.fillRect(tx, y, bar_w, 4, COL_DIM);
    int dot_x = tx + (int)((ms / 500) % (uint32_t)(bar_w - 3));
    canvas.fillRect(dot_x, y, 4, 4, COL_FG);
    y += 8;

    // Elapsed time
    uint32_t pos_s = state.track_pos_ms / 1000;
    char time_str[12];
    snprintf(time_str, sizeof(time_str), "%lu:%02lu",
             (unsigned long)(pos_s / 60), (unsigned long)(pos_s % 60));
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString(time_str, tx, y); y += 11;

    // Play state
    const char* play_icon =
        (state.playback == PlaybackState::PLAYING) ? "> PLAY"   :
        (state.playback == PlaybackState::PAUSED)  ? "|| PAUSE" : "[] STOP";
    canvas.setTextColor(COL_GREEN, COL_BG);
    canvas.drawString(play_icon, tx, y); y += 11;

    // Volume + EQ
    canvas.setTextColor(COL_FG, COL_BG);
    char vol[12];
    snprintf(vol, sizeof(vol), "VOL %d", state.volume);
    canvas.drawString(vol, tx, y);
    if (state.fullsound) {
        canvas.setTextColor(COL_YELLOW, COL_BG);
        canvas.drawString("FS", tx + 50, y);
    }
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString(EQ_PRESET_NAMES[(uint8_t)state.eq_preset], tx + 65, y);
}

void UIManager::drawLibrary(const AppState& state, const Library& lib) {
    int count = lib.count();
    int y = 14;
    int visible_rows = (H - HINT_H - 1 - y) / 11;  // leave room for hint bar

    // Auto-scroll to keep cursor on screen
    if (state.lib_cursor < s_lib_scroll)
        s_lib_scroll = state.lib_cursor;
    if (state.lib_cursor >= s_lib_scroll + visible_rows)
        s_lib_scroll = state.lib_cursor - visible_rows + 1;

    canvas.setTextSize(1);

    if (count == 0) {
        canvas.setTextColor(COL_DIM, COL_BG);
        canvas.drawString("(empty)", 4, y + 4);
        return;
    }

    for (int r = 0; r < visible_rows && (s_lib_scroll + r) < count; r++) {
        int idx = s_lib_scroll + r;
        const Library::Entry& e = lib.entry(idx);
        bool selected = (idx == state.lib_cursor);

        if (selected) {
            canvas.fillRect(0, y, W - 3, 11, COL_SELECTED);
            canvas.setTextColor(COL_FG, COL_SELECTED);
        } else {
            canvas.setTextColor(e.is_dir ? COL_YELLOW : COL_FG, COL_BG);
        }

        char row[48];
        if (e.is_dir) snprintf(row, sizeof(row), "[%s]", e.name);
        else          strncpy(row, e.name, sizeof(row) - 1);
        row[sizeof(row) - 1] = '\0';
        canvas.drawString(row, 2, y + 1);
        y += 11;
    }

    // Scroll indicator
    if (count > visible_rows) {
        int total_h = H - HINT_H - 1 - 14;
        int bar_h   = total_h * visible_rows / count;
        int bar_y   = 14 + (total_h - bar_h) * s_lib_scroll / (count - visible_rows);
        canvas.fillRect(W - 3, bar_y, 3, bar_h, COL_DIM);
    }
}

void UIManager::drawEQSettings(const AppState& state) {
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("EQUALIZER", W/2, 16);
    canvas.setTextDatum(textdatum_t::top_left);

    char line[32];
    snprintf(line, sizeof(line), "Preset: %s", EQ_PRESET_NAMES[(uint8_t)state.eq_preset]);
    canvas.drawString(line, 4, 28);

    static const char* BAND_LABELS[] = {"60", "250", "1k", "4k", "12k"};
    int bx = 10;
    for (int b = 0; b < 5; b++) {
        int gain  = state.eq_custom[b];
        int mid_y = 90;
        int bar_h = (gain * 30) / 12;
        if (bar_h >= 0) canvas.fillRect(bx, mid_y - bar_h, 18, bar_h, COL_ACCENT);
        else            canvas.fillRect(bx, mid_y, 18, -bar_h, COL_DIM);
        canvas.drawRect(bx, mid_y - 30, 18, 60, COL_DIM);
        canvas.setTextColor(COL_DIM, COL_BG);
        canvas.drawString(BAND_LABELS[b], bx + 2, 122);
        bx += 44;
    }
}

void UIManager::drawSleepTimer(const AppState& state) {
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("SLEEP TIMER", W/2, 16);
    canvas.setTextDatum(textdatum_t::top_left);

    for (int i = 0; i < SLEEP_TIMER_COUNT; i++) {
        bool sel = (i == state.sleep_timer_idx);
        char opt[16];
        if (SLEEP_TIMER_OPTIONS[i] == 0) snprintf(opt, sizeof(opt), "Off");
        else                              snprintf(opt, sizeof(opt), "%d min", SLEEP_TIMER_OPTIONS[i]);
        if (sel) {
            canvas.fillRect(4, 30 + i * 14, W - 8, 13, COL_SELECTED);
            canvas.setTextColor(COL_FG, COL_SELECTED);
        } else {
            canvas.setTextColor(COL_FG, COL_BG);
        }
        canvas.drawString(opt, 8, 32 + i * 14);
    }
}

void UIManager::drawScreenTimeout(const AppState& state) {
    static const char* OPTS[SCREEN_TIMEOUT_COUNT] = {
        "Never",
        "Dim 15s / Off 30s",
        "Dim 30s / Off 60s",
        "Dim 60s / Off 2 min",
    };

    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("SCREEN TIMEOUT", W/2, 16);
    canvas.setTextDatum(textdatum_t::top_left);

    for (int i = 0; i < SCREEN_TIMEOUT_COUNT; i++) {
        bool sel = (i == state.screen_timeout_idx);
        if (sel) {
            canvas.fillRect(4, 30 + i * 14, W - 8, 13, COL_SELECTED);
            canvas.setTextColor(COL_FG, COL_SELECTED);
        } else {
            canvas.setTextColor(COL_FG, COL_BG);
        }
        canvas.drawString(OPTS[i], 8, 32 + i * 14);
    }
}

void UIManager::drawRecorder(const AppState& state, uint32_t elapsed_ms, uint8_t level) {
    (void)state;
    if (!s_canvas_ok) return;
    canvas.fillScreen(COL_BG);
    canvas.setTextColor(COL_RED, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("RECORDING", W/2, 20);

    char elapsed[12];
    uint32_t s = elapsed_ms / 1000;
    snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu",
             (unsigned long)(s / 60), (unsigned long)(s % 60));
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.drawString(elapsed, W/2, 40);

    int bar_w  = W - 40;
    int filled = (int)((int64_t)level * bar_w / 100);
    uint16_t col = (level > 80) ? COL_RED : (level > 60) ? COL_YELLOW : COL_GREEN;
    canvas.fillRect(20, 70, filled, 14, col);
    canvas.drawRect(20, 70, bar_w, 14, COL_DIM);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.drawString("STOP: Fn+REC", W/2, 110);
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.pushSprite(0, 0);
}

void UIManager::drawSettings(const AppState& state) {
    static const char* ANIM_NAMES[]  = { "Vinyl", "CD", "Cassette" };
    static const char* THEME_NAMES[] = { "Gray",  "Red", "Yellow"  };

    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("SETTINGS", W/2, 16);
    canvas.setTextDatum(textdatum_t::top_left);

    struct { const char* label; const char* value; } items[] = {
        { "Animation", ANIM_NAMES [state.anim_type  < 3 ? state.anim_type  : 0] },
        { "Theme",     THEME_NAMES[state.theme_idx  < 3 ? state.theme_idx  : 0] },
    };
    static constexpr int N_ITEMS = (int)(sizeof(items) / sizeof(items[0]));

    int y = 32;
    for (int i = 0; i < N_ITEMS; i++) {
        bool sel = (i == state.settings_cursor);
        if (sel) {
            canvas.fillRect(4, y - 1, W - 8, 13, COL_SELECTED);
            canvas.setTextColor(COL_FG, COL_SELECTED);
        } else {
            canvas.setTextColor(COL_FG, COL_BG);
        }
        char line[40];
        snprintf(line, sizeof(line), "%-12s %s", items[i].label, items[i].value);
        canvas.drawString(line, 8, y + 1);
        y += 16;
    }

    // Read-only info below
    canvas.setTextColor(COL_DIM, COL_BG);
    y += 4;
    char info[40];
    snprintf(info, sizeof(info), "Vol %-3d  %s", state.volume,
             EQ_PRESET_NAMES[(uint8_t)state.eq_preset]);
    canvas.drawString(info, 8, y); y += 13;
    snprintf(info, sizeof(info), "Battery %d%%  %s",
             state.battery_pct, state.charging ? "CHG" : "");
    canvas.drawString(info, 8, y);
}

void UIManager::loadAlbumArt(const char* track_path, bool has_embedded) {
    (void)has_embedded;
    s_art_loaded = false;

    char art_path[128] = {0};
    strncpy(art_path, track_path, sizeof(art_path) - 1);
    char* slash = strrchr(art_path, '/');
    if (!slash) return;

    size_t rem = sizeof(art_path) - (size_t)(slash + 1 - art_path);
    snprintf(slash + 1, rem, "cover.jpg");
    if (!SD.exists(art_path)) {
        snprintf(slash + 1, rem, "folder.jpg");
        if (!SD.exists(art_path)) return;
    }

    File f = SD.open(art_path);
    if (!f) return;
    size_t len = f.size();
    if (len == 0 || len > 48 * 1024) { f.close(); return; }

    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) { f.close(); return; }

    size_t n = f.read(buf, len);
    f.close();
    if (n == len) {
        M5.Display.drawJpg(buf, len, 0, 14, 100, H - 14);
        s_art_loaded = true;
    }
    free(buf);
}

void UIManager::showNotif(const char* text) {
    strncpy(s_notif, text, sizeof(s_notif) - 1);
    s_notif[sizeof(s_notif) - 1] = '\0';
    s_notif_until = millis() + 1500;
}

#include "UIManager.h"
#include <M5Unified.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "../config.h"
#include "../battery/BatteryMonitor.h"

M5Canvas UIManager::canvas(&M5.Display);
bool     UIManager::s_art_loaded  = false;
bool     UIManager::s_canvas_ok   = false;
char     UIManager::s_notif[64]   = {0};
uint32_t UIManager::s_notif_until = 0;
int      UIManager::s_lib_scroll  = 0;

static constexpr int W = 240;
static constexpr int H = 135;

static constexpr uint16_t COL_BG       = 0x0000;
static constexpr uint16_t COL_FG       = 0xFFFF;
static constexpr uint16_t COL_ACCENT   = 0x051F;
static constexpr uint16_t COL_SELECTED = 0x3616;
static constexpr uint16_t COL_DIM      = 0x7BEF;
static constexpr uint16_t COL_GREEN    = 0x07E0;
static constexpr uint16_t COL_RED      = 0xF800;
static constexpr uint16_t COL_YELLOW   = 0xFFE0;

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

    if (!s_canvas_ok) {
        M5.Display.fillScreen(COL_BG);
        M5.Display.setTextColor(COL_FG, COL_BG);
        M5.Display.setCursor(4, 4);
        M5.Display.printf("Vol: %d", state.volume);
        return;
    }

    canvas.fillScreen(COL_BG);

    switch (state.current_screen) {
        case Screen::NOW_PLAYING:    drawNowPlaying(state, lib);  break;
        case Screen::LIBRARY:        drawLibrary(state, lib);     break;
        case Screen::EQ_SETTINGS:    drawEQSettings(state);       break;
        case Screen::SLEEP_TIMER:    drawSleepTimer(state);       break;
        case Screen::SETTINGS:       drawSettings(state);         break;
        case Screen::VOICE_RECORDER: break;
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

    // Repeat / shuffle icons
    canvas.setTextDatum(textdatum_t::top_left);
    if (state.repeat == RepeatMode::ONE)      canvas.drawString("[1]", 2, 2);
    else if (state.repeat == RepeatMode::ALL) canvas.drawString("[A]", 2, 2);
    if (state.shuffle)      canvas.drawString("~",    22, 2);
    if (state.muted)        canvas.drawString("M",    32, 2);
    if (state.mono)         canvas.drawString("MNO",  42, 2);
    if (state.headphones_in) canvas.drawString("HP",  66, 2);

    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_left);
}

void UIManager::drawNowPlaying(const AppState& state, const Library& lib) {
    (void)lib;

    // Left panel: music note placeholder
    canvas.fillRect(0, 14, 100, H - 14, COL_ACCENT);
    canvas.setTextDatum(textdatum_t::middle_center);
    canvas.setTextSize(3);
    canvas.drawString("=|>", 50, H/2 + 7);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    // If album art was loaded, it was drawn directly to M5.Display underneath
    // the canvas, so it shows through on the left 100 px when canvas has
    // a transparent fill — we just leave the accent block instead.

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

    // Progress bar (elapsed / no total since we skip pre-scan)
    canvas.fillRect(tx, y, W - tx - 2, 4, COL_DIM);
    // We don't know total duration, so just show a pulsing dot
    int dot_x = tx + (int)((millis() / 500) % (uint32_t)(W - tx - 2));
    canvas.fillRect(dot_x, y, 4, 4, COL_FG);
    y += 8;

    // Elapsed time
    uint32_t pos_s = state.track_pos_ms / 1000;
    char time_str[12];
    snprintf(time_str, sizeof(time_str), "%lu:%02lu",
             (unsigned long)(pos_s / 60), (unsigned long)(pos_s % 60));
    canvas.drawString(time_str, tx, y); y += 11;

    // Play state
    const char* play_icon =
        (state.playback == PlaybackState::PLAYING) ? "> PLAY"  :
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
    int visible_rows = (H - y) / 11;

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
        int total_h = H - 14;
        int bar_h   = total_h * visible_rows / count;
        int bar_y   = 14 + (total_h - bar_h) * s_lib_scroll /
                      (count - visible_rows);
        canvas.fillRect(W - 3, bar_y, 3, bar_h, COL_DIM);
    }

    // Current path hint in dim at top right of status bar is already handled
    // by drawStatusBar. We additionally show it at the very bottom.
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.setTextDatum(textdatum_t::bottom_right);
    canvas.drawString(lib.currentPath(), W - 4, H - 1);
    canvas.setTextDatum(textdatum_t::top_left);
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
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("SETTINGS", W/2, 16);
    canvas.setTextDatum(textdatum_t::top_left);

    char line[40];
    int y = 32;
    snprintf(line, sizeof(line), "FullSound: %s",      state.fullsound ? "ON" : "OFF");     canvas.drawString(line, 4, y); y += 14;
    snprintf(line, sizeof(line), "Output:    %s",      state.mono      ? "Mono" : "Stereo"); canvas.drawString(line, 4, y); y += 14;
    snprintf(line, sizeof(line), "Shuffle:   %s",      state.shuffle   ? "ON" : "OFF");      canvas.drawString(line, 4, y); y += 14;
    const char* rm = (state.repeat == RepeatMode::OFF) ? "Off" :
                     (state.repeat == RepeatMode::ONE) ? "One" : "All";
    snprintf(line, sizeof(line), "Repeat:    %s", rm);                                        canvas.drawString(line, 4, y); y += 14;
    snprintf(line, sizeof(line), "Volume:    %d/30",   state.volume);                         canvas.drawString(line, 4, y); y += 14;
    snprintf(line, sizeof(line), "Battery:   %d%% (%s)", state.battery_pct, state.charging ? "CHG" : "DIS"); canvas.drawString(line, 4, y);
}

void UIManager::loadAlbumArt(const char* track_path, bool has_embedded) {
    (void)has_embedded;
    s_art_loaded = false;

    char art_path[128] = {0};
    strncpy(art_path, track_path, sizeof(art_path) - 1);
    char* slash = strrchr(art_path, '/');
    if (!slash) return;

    strcpy(slash + 1, "cover.jpg");
    if (!SD.exists(art_path)) {
        strcpy(slash + 1, "folder.jpg");
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

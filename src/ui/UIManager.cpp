#include "UIManager.h"
#include <M5Unified.h>
#include <esp_heap_caps.h>
#include <string.h>
#include <stdio.h>
#include "../config.h"
#include "../battery/BatteryMonitor.h"

M5GFX   UIManager::display;
M5Canvas UIManager::canvas(&UIManager::display);
bool     UIManager::s_art_loaded = false;
char     UIManager::s_notif[64] = {0};
uint32_t UIManager::s_notif_until = 0;
int      UIManager::s_lib_scroll = 0;

// Display dimensions
static constexpr int W = 240;
static constexpr int H = 135;

// Colour palette — feel free to restyle
static constexpr uint16_t COL_BG       = 0x0000;  // black
static constexpr uint16_t COL_FG       = 0xFFFF;  // white
static constexpr uint16_t COL_ACCENT   = 0x051F;  // deep blue
static constexpr uint16_t COL_SELECTED = 0x3616;  // teal highlight
static constexpr uint16_t COL_DIM      = 0x7BEF;  // grey
static constexpr uint16_t COL_GREEN    = 0x07E0;
static constexpr uint16_t COL_RED      = 0xF800;
static constexpr uint16_t COL_YELLOW   = 0xFFE0;

void UIManager::begin() {
    display.begin();
    display.setRotation(1);
    display.fillScreen(COL_BG);
    canvas.createSprite(W, H);
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextSize(1);
}

void UIManager::draw(const AppState& state, const Library& lib, const PlaylistManager& playlist) {
    (void)playlist;
    canvas.fillScreen(COL_BG);

    switch (state.current_screen) {
        case Screen::NOW_PLAYING:   drawNowPlaying(state, lib); break;
        case Screen::LIBRARY:       drawLibrary(state, lib);    break;
        case Screen::EQ_SETTINGS:   drawEQSettings(state);      break;
        case Screen::SLEEP_TIMER:   drawSleepTimer(state);      break;
        case Screen::VOICE_RECORDER: break;  // drawn by recorder task
        case Screen::SETTINGS:      drawSettings(state);        break;
        default: break;
    }

    drawStatusBar(state);

    // Notification overlay
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

    // Battery
    char batt[12];
    if (state.charging) {
        snprintf(batt, sizeof(batt), "CHG %d%%", state.battery_pct);
    } else {
        snprintf(batt, sizeof(batt), "%d%%", state.battery_pct);
    }
    canvas.setTextColor(COL_FG, COL_ACCENT);
    canvas.setTextDatum(textdatum_t::top_right);
    canvas.drawString(batt, W - 2, 2);

    // Repeat/shuffle icons
    const char* mode = "";
    if (state.repeat == RepeatMode::ONE)  mode = "[1]";
    else if (state.repeat == RepeatMode::ALL) mode = "[A]";
    canvas.setTextDatum(textdatum_t::top_left);
    canvas.drawString(mode, 2, 2);

    if (state.shuffle) canvas.drawString("~", 22, 2);
    if (state.muted)   canvas.drawString("M", 32, 2);
    if (state.mono)    canvas.drawString("MONO", 42, 2);
    if (state.headphones_in) canvas.drawString("HP", 72, 2);

    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextDatum(textdatum_t::top_left);
}

void UIManager::drawNowPlaying(const AppState& state, const Library& lib) {
    const TrackInfo& t = lib.track(state.current_track_idx);

    int y = 16;

    // Album art area (left side, 100×100 centered in height)
    bool has_art = s_art_loaded;
    if (has_art) {
        // Art already pushed to display via loadAlbumArt
        // Leave left 100px for art
    } else {
        // Music note placeholder
        canvas.fillRect(0, 14, 100, H - 14, COL_ACCENT);
        canvas.setTextDatum(textdatum_t::middle_center);
        canvas.setTextSize(3);
        canvas.drawString("=|>", 50, H/2 + 7);
        canvas.setTextSize(1);
        canvas.setTextDatum(textdatum_t::top_left);
    }

    int tx = 104;
    int tw = W - tx - 2;

    // Track title (truncated)
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.setTextSize(1);
    canvas.setTextDatum(textdatum_t::top_left);

    // Truncate title to fit width
    char truncated[32];
    strncpy(truncated, t.title, 31);
    truncated[31] = '\0';
    canvas.drawString(truncated, tx, y);      y += 12;

    // Artist
    canvas.setTextColor(COL_DIM, COL_BG);
    strncpy(truncated, t.artist, 31);
    canvas.drawString(truncated, tx, y);      y += 11;

    // Album
    strncpy(truncated, t.album, 31);
    canvas.drawString(truncated, tx, y);      y += 12;

    // Playback position / duration bar
    uint32_t dur = t.duration_ms > 0 ? t.duration_ms : 1;
    uint32_t pos = state.track_pos_ms;
    int bar_w = tw;
    int filled = (int)((int64_t)pos * bar_w / dur);
    if (filled > bar_w) filled = bar_w;

    canvas.fillRect(tx, y, bar_w, 4, COL_DIM);
    canvas.fillRect(tx, y, filled, 4, COL_FG);
    y += 8;

    // Time mm:ss / mm:ss
    char time_str[24];
    uint32_t pos_s = pos / 1000;
    uint32_t dur_s = dur / 1000;
    snprintf(time_str, sizeof(time_str), "%lu:%02lu / %lu:%02lu",
             (unsigned long)(pos_s / 60), (unsigned long)(pos_s % 60),
             (unsigned long)(dur_s / 60), (unsigned long)(dur_s % 60));
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString(time_str, tx, y);       y += 11;

    // Play state indicator
    const char* play_icon = (state.playback == PlaybackState::PLAYING) ? "> PLAY" :
                            (state.playback == PlaybackState::PAUSED)  ? "|| PAUSE" : "[] STOP";
    canvas.setTextColor(COL_GREEN, COL_BG);
    canvas.drawString(play_icon, tx, y);      y += 11;

    // Volume bar
    canvas.setTextColor(COL_FG, COL_BG);
    char vol_str[12];
    snprintf(vol_str, sizeof(vol_str), "VOL %d", state.volume);
    canvas.drawString(vol_str, tx, y);

    // FullSound / EQ indicator
    if (state.fullsound) {
        canvas.setTextColor(COL_YELLOW, COL_BG);
        canvas.drawString("FS", tx + 55, y);
    }
    canvas.setTextColor(COL_DIM, COL_BG);
    canvas.drawString(EQ_PRESET_NAMES[(uint8_t)state.eq_preset], tx + 70, y);
}

void UIManager::drawLibrary(const AppState& state, const Library& lib) {
    static int indices[LIBRARY_MAX_TRACKS];
    int count = lib.getFiltered(indices, LIBRARY_MAX_TRACKS);

    int y = 14;
    int visible_rows = (H - y) / 11;

    // Scroll so cursor stays visible
    if (state.lib_cursor < s_lib_scroll) s_lib_scroll = state.lib_cursor;
    if (state.lib_cursor >= s_lib_scroll + visible_rows) s_lib_scroll = state.lib_cursor - visible_rows + 1;

    canvas.setTextSize(1);
    for (int r = 0; r < visible_rows && (s_lib_scroll + r) < count; r++) {
        int idx = indices[s_lib_scroll + r];
        const TrackInfo& t = lib.track(idx);
        bool selected = ((s_lib_scroll + r) == state.lib_cursor);

        if (selected) {
            canvas.fillRect(0, y, W, 11, COL_SELECTED);
            canvas.setTextColor(COL_FG, COL_SELECTED);
        } else {
            canvas.setTextColor(COL_FG, COL_BG);
        }

        // "Artist - Title" or just title if no artist
        char row[48];
        if (t.artist[0]) {
            snprintf(row, sizeof(row), "%s - %s", t.artist, t.title);
        } else {
            strncpy(row, t.title, 47);
        }
        row[47] = '\0';
        canvas.drawString(row, 2, y + 1);
        y += 11;
    }

    // Scroll indicator
    if (count > visible_rows) {
        int total_h = H - 14;
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

    // Preset name
    char line[32];
    snprintf(line, sizeof(line), "Preset: %s", EQ_PRESET_NAMES[(uint8_t)state.eq_preset]);
    canvas.drawString(line, 4, 28);

    // Band bars
    static const char* BAND_LABELS[] = {"60", "250", "1k", "4k", "12k"};
    int bx = 10;
    for (int b = 0; b < 5; b++) {
        int gain = state.eq_custom[b];  // if preset == CUSTOM
        // Draw a vertical bar centered at mid, ±12 dB → ±30 px
        int mid_y = 90;
        int bar_h = (gain * 30) / 12;
        if (bar_h >= 0) {
            canvas.fillRect(bx, mid_y - bar_h, 18, bar_h, COL_ACCENT);
        } else {
            canvas.fillRect(bx, mid_y, 18, -bar_h, COL_DIM);
        }
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
        if (SLEEP_TIMER_OPTIONS[i] == 0) {
            snprintf(opt, sizeof(opt), "Off");
        } else {
            snprintf(opt, sizeof(opt), "%d min", SLEEP_TIMER_OPTIONS[i]);
        }
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
    canvas.fillScreen(COL_BG);
    canvas.setTextColor(COL_RED, COL_BG);
    canvas.setTextDatum(textdatum_t::top_center);
    canvas.drawString("● RECORDING", W/2, 20);

    // Elapsed time
    char elapsed[12];
    uint32_t s = elapsed_ms / 1000;
    snprintf(elapsed, sizeof(elapsed), "%02lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
    canvas.setTextColor(COL_FG, COL_BG);
    canvas.drawString(elapsed, W/2, 40);

    // Level meter bar
    int bar_x  = 20;
    int bar_w  = W - 40;
    int filled = (int)((int64_t)level * bar_w / 100);
    uint16_t col = (level > 80) ? COL_RED : (level > 60) ? COL_YELLOW : COL_GREEN;
    canvas.fillRect(bar_x, 70, filled, 14, col);
    canvas.drawRect(bar_x, 70, bar_w, 14, COL_DIM);
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

    snprintf(line, sizeof(line), "FullSound: %s", state.fullsound ? "ON" : "OFF");
    canvas.drawString(line, 4, y); y += 14;

    snprintf(line, sizeof(line), "Output:    %s", state.mono ? "Mono" : "Stereo");
    canvas.drawString(line, 4, y); y += 14;

    snprintf(line, sizeof(line), "Shuffle:   %s", state.shuffle ? "ON" : "OFF");
    canvas.drawString(line, 4, y); y += 14;

    const char* rmode = (state.repeat == RepeatMode::OFF) ? "Off" :
                        (state.repeat == RepeatMode::ONE) ? "One" : "All";
    snprintf(line, sizeof(line), "Repeat:    %s", rmode);
    canvas.drawString(line, 4, y); y += 14;

    snprintf(line, sizeof(line), "Volume:    %d/30", state.volume);
    canvas.drawString(line, 4, y); y += 14;

    snprintf(line, sizeof(line), "Battery:   %d%% (%s)", state.battery_pct, state.charging ? "CHG" : "DIS");
    canvas.drawString(line, 4, y);
}

void UIManager::loadAlbumArt(const char* track_path, bool has_embedded) {
    s_art_loaded = false;

    // Resolve the art file: cover.jpg / folder.jpg in the track's directory.
    // (Embedded APIC extraction would write a temp file we then load the same way.)
    char art_path[256] = {0};
    strncpy(art_path, track_path, sizeof(art_path) - 1);
    char* slash = strrchr(art_path, '/');
    if (!slash) return;

    strcpy(slash + 1, "cover.jpg");
    if (!SD.exists(art_path)) {
        strcpy(slash + 1, "folder.jpg");
        if (!SD.exists(art_path)) return;
    }

    // Read the JPEG into a PSRAM buffer and decode from memory. This avoids
    // M5GFX's filesystem-wrapper template, which can't bind to fs::SDFS directly.
    File f = SD.open(art_path);
    if (!f) return;
    size_t len = f.size();
    if (len == 0 || len > 256 * 1024) { f.close(); return; }

    uint8_t* buf = (uint8_t*)heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!buf) buf = (uint8_t*)malloc(len);
    if (!buf) { f.close(); return; }

    size_t read = f.read(buf, len);
    f.close();

    if (read == len) {
        // Scale to fit a 100×(H-14) square in the upper-left of the now-playing view.
        display.drawJpg(buf, len, 0, 14, 100, H - 14);
        s_art_loaded = true;
    }
    free(buf);
}

void UIManager::showNotif(const char* text) {
    strncpy(s_notif, text, sizeof(s_notif) - 1);
    s_notif[sizeof(s_notif) - 1] = '\0';
    s_notif_until = millis() + 1500;
}

#pragma once
#include <M5GFX.h>
#include "../types.h"
#include "../storage/Library.h"
#include "../storage/PlaylistManager.h"

class UIManager {
public:
    static void begin();
    static void draw(const AppState& state, const Library& lib, const PlaylistManager& playlist);

    // Individual screen renderers
    static void drawNowPlaying(const AppState& state, const Library& lib);
    static void drawLibrary(const AppState& state, const Library& lib);
    static void drawEQSettings(const AppState& state);
    static void drawSleepTimer(const AppState& state);
    static void drawRecorder(const AppState& state, uint32_t elapsed_ms, uint8_t level);
    static void drawSettings(const AppState& state);

    // Album art
    static void loadAlbumArt(const char* track_path, bool has_embedded);

    // Notification popup (auto-clears after ~1.5s)
    static void showNotif(const char* text);

private:
    static M5Canvas canvas;    // off-screen framebuffer

    // Status bar (top row)
    static void drawStatusBar(const AppState& state);

    // List rendering helper
    static void drawList(const char* const* items, int count, int cursor, int scroll);

    static bool    s_art_loaded;
    static bool    s_canvas_ok;
    static char    s_notif[64];
    static uint32_t s_notif_until;

    // Scroll animation
    static int s_lib_scroll;
};

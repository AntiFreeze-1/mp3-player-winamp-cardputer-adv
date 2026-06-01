#include "NVSConfig.h"
#include "../config.h"
#include <Preferences.h>
#include <string.h>

static Preferences prefs;

void NVSConfig::load(AppState& state, char* last_track_out, uint32_t* last_pos_ms_out) {
    prefs.begin(NVS_NAMESPACE, true);

    state.volume       = prefs.getUChar("volume", VOLUME_DEFAULT);
    state.eq_preset    = (EQPreset)prefs.getUChar("eq_preset", 0);
    state.fullsound    = prefs.getBool("fullsound", false);
    state.mono         = prefs.getBool("mono", false);
    state.shuffle      = prefs.getBool("shuffle", false);
    state.repeat       = (RepeatMode)prefs.getUChar("repeat_mode", 0);
    state.sleep_timer_idx = prefs.getUChar("sleep_timer", 0);

    // Clamp volume in case stored value is out of range
    if (state.volume > VOLUME_MAX) state.volume = VOLUME_DEFAULT;

    // Custom EQ bands
    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "eq_custom_%d", i);
        state.eq_custom[i] = (int8_t)prefs.getChar(key, 0);
    }

    // Last track + position for resume
    if (last_track_out) {
        prefs.getString("last_track", last_track_out, 256);
    }
    if (last_pos_ms_out) {
        *last_pos_ms_out = prefs.getULong("last_pos_ms", 0);
    }

    prefs.end();
}

void NVSConfig::save(const AppState& state, const char* last_track, uint32_t last_pos_ms) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("volume",       state.volume);
    prefs.putUChar("eq_preset",    (uint8_t)state.eq_preset);
    prefs.putBool ("fullsound",    state.fullsound);
    prefs.putBool ("mono",         state.mono);
    prefs.putBool ("shuffle",      state.shuffle);
    prefs.putUChar("repeat_mode",  (uint8_t)state.repeat);
    prefs.putUChar("sleep_timer",  state.sleep_timer_idx);
    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "eq_custom_%d", i);
        prefs.putChar(key, state.eq_custom[i]);
    }
    if (last_track) prefs.putString("last_track", last_track);
    prefs.putULong("last_pos_ms", last_pos_ms);
    prefs.end();
}

void NVSConfig::saveVolume(uint8_t vol) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("volume", vol);
    prefs.end();
}

void NVSConfig::saveEQPreset(EQPreset preset, const int8_t custom[5]) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("eq_preset", (uint8_t)preset);
    for (int i = 0; i < 5; i++) {
        char key[16];
        snprintf(key, sizeof(key), "eq_custom_%d", i);
        prefs.putChar(key, custom[i]);
    }
    prefs.end();
}

void NVSConfig::savePlaybackMode(bool shuffle, RepeatMode repeat) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool ("shuffle",     shuffle);
    prefs.putUChar("repeat_mode", (uint8_t)repeat);
    prefs.end();
}

void NVSConfig::saveTrackPosition(const char* path, uint32_t pos_ms) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putString("last_track",  path);
    prefs.putULong ("last_pos_ms", pos_ms);
    prefs.end();
}

void NVSConfig::saveFullSound(bool enabled) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool("fullsound", enabled);
    prefs.end();
}

void NVSConfig::saveMono(bool enabled) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putBool("mono", enabled);
    prefs.end();
}

void NVSConfig::saveSleepTimer(uint8_t idx) {
    prefs.begin(NVS_NAMESPACE, false);
    prefs.putUChar("sleep_timer", idx);
    prefs.end();
}

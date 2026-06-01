#pragma once
#include "../types.h"

class NVSConfig {
public:
    static void load(AppState& state, char* last_track_out, uint32_t* last_pos_ms_out);
    static void save(const AppState& state, const char* last_track, uint32_t last_pos_ms);
    static void saveVolume(uint8_t vol);
    static void saveEQPreset(EQPreset preset, const int8_t custom[5]);
    static void savePlaybackMode(bool shuffle, RepeatMode repeat);
    static void saveTrackPosition(const char* path, uint32_t pos_ms);
    static void saveFullSound(bool enabled);
    static void saveSleepTimer(uint8_t idx);
};

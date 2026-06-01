#pragma once
#include <Audio.h>
#include <SD.h>
#include "../types.h"
#include "../config.h"
#include "DSP.h"

class AudioEngine {
public:
    static void     begin();
    static void     loop();   // call from AudioTask

    // Playback control
    static bool     play(const char* path);
    static void     pause();
    static void     resume();
    static void     stop();
    static bool     seekMs(uint32_t ms);

    // Volume: 0–VOLUME_MAX
    static void     setVolume(uint8_t vol);
    static uint8_t  volume() { return s_vol; }

    // Mute
    static void     setMute(bool muted);
    static bool     muted() { return s_muted; }

    // EQ / DSP
    static void     setEQPreset(EQPreset preset, const int8_t custom[5]);
    static void     setFullSound(bool enabled);
    static void     setMono(bool enabled);

    // State queries
    static PlaybackState state()     { return s_state; }
    static uint32_t      positionMs(){ return s_position_ms; }
    static bool          isEOF()     { return s_eof; }
    static void          clearEOF()  { s_eof = false; }

    // Headphone detection
    static bool headphonesIn();

    // Called by ESP32-audioI2S callback
    static void onEOF();
    static void onInfo(const char* info);
    static void onID3Tag(const char* info);
    static void onPCM(int16_t* data, size_t len);

private:
    static Audio        s_audio;
    static PlaybackState s_state;
    static uint8_t      s_vol;
    static bool         s_muted;
    static bool         s_eof;
    static uint32_t     s_position_ms;
    static char         s_current_path[256];

    // Map software volume 0-30 to ES8311 DAC value
    static uint8_t volToI2S(uint8_t vol);
};

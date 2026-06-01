#pragma once
#include <SD.h>
#include "../config.h"
#include "../types.h"

enum class RecordState : uint8_t {
    IDLE = 0,
    RECORDING,
    STOPPING,
};

enum class RecordQuality : uint8_t {
    VOICE = 0,   // 16 kHz mono, 64 kbps
    HQ    = 1,   // 44.1 kHz stereo, 128 kbps
};

class VoiceRecorder {
public:
    static void        begin();
    static bool        startRecording(RecordQuality quality = RecordQuality::VOICE);
    static void        stopRecording();
    static RecordState state() { return s_state; }
    static uint32_t    elapsedMs();

    // Peak level for level meter (0–100)
    static uint8_t     peakLevel() { return s_peak; }

    // Task entry point (pinned to Core 1)
    static void        task(void* arg);

    static SemaphoreHandle_t sd_mutex;  // shared with AudioEngine

private:
    static RecordState  s_state;
    static File         s_file;
    static uint32_t     s_start_ms;
    static uint8_t      s_peak;
    static RecordQuality s_quality;
    static char         s_filepath[256];

    static void buildFilePath(char* out, size_t max);
    static void writeWAVHeader(File& f, uint32_t sample_rate, uint8_t channels, uint32_t num_samples);
    static void updateWAVHeader(File& f, uint32_t num_samples);
};

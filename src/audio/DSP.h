#pragma once
#include <stdint.h>
#include "../types.h"

// ── Biquad filter (Direct Form II) ────────────────────────────────────────
struct Biquad {
    float b0, b1, b2;   // numerator coefficients
    float a1, a2;       // denominator (a0 normalised to 1)
    float z1L, z2L;     // left channel state
    float z1R, z2R;     // right channel state

    void clear() { z1L = z2L = z1R = z2R = 0.0f; }

    inline float processSample(float x, float& z1, float& z2) {
        float y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }

    inline void processStereo(float& l, float& r) {
        l = processSample(l, z1L, z2L);
        r = processSample(r, z1R, z2R);
    }
};

class DSP {
public:
    static void init(float sample_rate = 48000.0f);

    // Apply EQ preset — recomputes biquad coefficients
    static void setEQPreset(EQPreset preset, const int8_t custom_gains[5]);

    // Enable/disable FullSound enhancement
    static void setFullSound(bool enabled);
    static bool fullSoundEnabled() { return s_fullsound; }

    // Mono downmix: sum L+R and write to both channels
    static void setMono(bool enabled);
    static bool monoEnabled() { return s_mono; }

    // Process one stereo frame (interleaved int16 L/R pairs)
    // count = total number of int16 samples (pairs * 2)
    static void process(int16_t* samples, int count);

private:
    static float s_sr;
    static bool  s_fullsound;
    static bool  s_mono;

    // 5-band EQ biquads
    static Biquad s_eq[5];

    // FullSound: bass shelf + presence boost + harmonic exciter HP
    static Biquad s_bass_shelf;
    static Biquad s_presence;
    static Biquad s_exciter_hp;

    // Coefficient calculation helpers
    static void calcLowShelf(Biquad& bq, float freq, float gain_db, float slope = 1.0f);
    static void calcHighShelf(Biquad& bq, float freq, float gain_db, float slope = 1.0f);
    static void calcPeaking(Biquad& bq, float freq, float gain_db, float Q);
    static void calcHighPass(Biquad& bq, float freq, float Q);

    // EQ band centre frequencies (Hz)
    static constexpr float EQ_FREQS[5] = {60.0f, 250.0f, 1000.0f, 4000.0f, 12000.0f};
    static constexpr float EQ_Q = 1.4f;
};

// ── EQ preset gain tables (5 bands, ±12 dB) ──────────────────────────────
// Order: 60Hz, 250Hz, 1kHz, 4kHz, 12kHz
static constexpr int8_t EQ_GAINS[][5] = {
    /* FLAT      */ {  0,  0,  0,  0,  0 },
    /* ROCK      */ {  5,  3, -1,  3,  5 },
    /* POP       */ { -1,  4,  6,  4, -1 },
    /* JAZZ      */ {  4,  2,  0,  2,  4 },
    /* CLASSICAL */ {  5,  3, -2,  3,  4 },
    /* HIP_HOP   */ {  6,  5,  0, -1,  2 },
    /* FUNK      */ {  5,  4, -1,  4,  3 },
    /* TECHNO    */ {  5, -1,  0,  4,  5 },
};

#include "DSP.h"
#include <math.h>
#include <string.h>

float  DSP::s_sr       = 48000.0f;
bool   DSP::s_fullsound = false;
bool   DSP::s_mono      = false;
Biquad DSP::s_eq[5]    = {};
Biquad DSP::s_bass_shelf = {};
Biquad DSP::s_presence   = {};
Biquad DSP::s_exciter_hp = {};

static constexpr float PI_F = 3.14159265358979f;

// Out-of-class definitions for ODR-used constexpr static members
constexpr float DSP::EQ_FREQS[5];
constexpr float DSP::EQ_Q;

void DSP::calcPeaking(Biquad& bq, float freq, float gain_db, float Q) {
    float A  = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * freq / s_sr;
    float alpha = sinf(w0) / (2.0f * Q);

    float b0 =  1.0f + alpha * A;
    float b1 = -2.0f * cosf(w0);
    float b2 =  1.0f - alpha * A;
    float a0 =  1.0f + alpha / A;
    float a1 = -2.0f * cosf(w0);
    float a2 =  1.0f - alpha / A;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.clear();
}

void DSP::calcLowShelf(Biquad& bq, float freq, float gain_db, float slope) {
    float A  = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * freq / s_sr;
    float cw = cosf(w0);
    float sw = sinf(w0);
    float alpha = sw / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);

    float b0 =  A * ((A + 1.0f) - (A - 1.0f) * cw + 2.0f * sqrtf(A) * alpha);
    float b1 =  2.0f * A * ((A - 1.0f) - (A + 1.0f) * cw);
    float b2 =  A * ((A + 1.0f) - (A - 1.0f) * cw - 2.0f * sqrtf(A) * alpha);
    float a0 =        (A + 1.0f) + (A - 1.0f) * cw + 2.0f * sqrtf(A) * alpha;
    float a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cw);
    float a2 =         (A + 1.0f) + (A - 1.0f) * cw - 2.0f * sqrtf(A) * alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.clear();
}

void DSP::calcHighShelf(Biquad& bq, float freq, float gain_db, float slope) {
    float A  = powf(10.0f, gain_db / 40.0f);
    float w0 = 2.0f * PI_F * freq / s_sr;
    float cw = cosf(w0);
    float sw = sinf(w0);
    float alpha = sw / 2.0f * sqrtf((A + 1.0f / A) * (1.0f / slope - 1.0f) + 2.0f);

    float b0 =  A * ((A + 1.0f) + (A - 1.0f) * cw + 2.0f * sqrtf(A) * alpha);
    float b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cw);
    float b2 =  A * ((A + 1.0f) + (A - 1.0f) * cw - 2.0f * sqrtf(A) * alpha);
    float a0 =        (A + 1.0f) - (A - 1.0f) * cw + 2.0f * sqrtf(A) * alpha;
    float a1 =  2.0f * ((A - 1.0f) - (A + 1.0f) * cw);
    float a2 =         (A + 1.0f) - (A - 1.0f) * cw - 2.0f * sqrtf(A) * alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.clear();
}

void DSP::calcHighPass(Biquad& bq, float freq, float Q) {
    float w0 = 2.0f * PI_F * freq / s_sr;
    float alpha = sinf(w0) / (2.0f * Q);
    float cw    = cosf(w0);

    float b0 =  (1.0f + cw) / 2.0f;
    float b1 = -(1.0f + cw);
    float b2 =  (1.0f + cw) / 2.0f;
    float a0 =   1.0f + alpha;
    float a1 =  -2.0f * cw;
    float a2 =   1.0f - alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.clear();
}

void DSP::init(float sample_rate) {
    s_sr = sample_rate;
    // Default: flat EQ
    setEQPreset(EQPreset::FLAT, nullptr);
    // FullSound filters
    calcLowShelf (s_bass_shelf,  150.0f, 5.0f);
    calcHighShelf(s_presence,   6000.0f, 4.0f);
    calcHighPass (s_exciter_hp, 3000.0f, 0.7f);
}

void DSP::setEQPreset(EQPreset preset, const int8_t custom_gains[5]) {
    const int8_t* gains;
    int8_t        custom_copy[5] = {0};

    if (preset == EQPreset::CUSTOM && custom_gains) {
        memcpy(custom_copy, custom_gains, 5);
        gains = custom_copy;
    } else {
        uint8_t idx = (uint8_t)preset;
        if (idx >= (uint8_t)EQPreset::CUSTOM) idx = 0;
        gains = EQ_GAINS[idx];
    }

    for (int i = 0; i < 5; i++) {
        calcPeaking(s_eq[i], EQ_FREQS[i], (float)gains[i], EQ_Q);
    }
}

void DSP::setFullSound(bool enabled) {
    s_fullsound = enabled;
}

void DSP::setMono(bool enabled) {
    s_mono = enabled;
}

// Inline soft-clip for harmonic exciter
static inline float softClip(float x) {
    // tanh approximation
    if (x >  1.5f) return  1.0f;
    if (x < -1.5f) return -1.0f;
    return x - (x * x * x) / 4.5f;
}

void DSP::process(int16_t* samples, int count) {
    // count is total int16 values; stereo pairs = count/2
    int pairs = count / 2;
    for (int i = 0; i < pairs; i++) {
        float l = samples[i * 2]     / 32768.0f;
        float r = samples[i * 2 + 1] / 32768.0f;

        // 5-band EQ
        for (int b = 0; b < 5; b++) {
            s_eq[b].processStereo(l, r);
        }

        // FullSound
        if (s_fullsound) {
            // Bass boost
            float bl = l, br = r;
            s_bass_shelf.processStereo(bl, br);

            // Presence boost
            float pl = l, pr = r;
            s_presence.processStereo(pl, pr);

            // Harmonic exciter: high-pass → soft clip → mix back at low level
            float el = l, er = r;
            s_exciter_hp.processStereo(el, er);
            el = softClip(el * 2.0f) * 0.1f;
            er = softClip(er * 2.0f) * 0.1f;

            // Recombine: take EQ'd signal, add FullSound layers
            l = bl * 0.5f + pl * 0.3f + el + l * 0.2f;
            r = br * 0.5f + pr * 0.3f + er + r * 0.2f;
        }

        // Mono downmix: average both channels into a single signal
        if (s_mono) {
            float m = (l + r) * 0.5f;
            l = m;
            r = m;
        }

        // Hard clip and convert back to int16
        l = l >  1.0f ?  1.0f : (l < -1.0f ? -1.0f : l);
        r = r >  1.0f ?  1.0f : (r < -1.0f ? -1.0f : r);
        samples[i * 2]     = (int16_t)(l * 32767.0f);
        samples[i * 2 + 1] = (int16_t)(r * 32767.0f);
    }
}

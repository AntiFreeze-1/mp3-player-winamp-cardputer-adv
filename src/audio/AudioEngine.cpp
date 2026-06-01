#include "AudioEngine.h"
#include <Arduino.h>
#include <math.h>

Audio         AudioEngine::s_audio;
PlaybackState AudioEngine::s_state       = PlaybackState::STOPPED;
uint8_t       AudioEngine::s_vol         = VOLUME_DEFAULT;
bool          AudioEngine::s_muted       = false;
bool          AudioEngine::s_eof         = false;
uint32_t      AudioEngine::s_position_ms = 0;
char          AudioEngine::s_current_path[256] = {0};

// ── ESP32-audioI2S global callbacks (weak symbol overrides) ────────────────
// audio_eof_mp3 fires at the end of ANY decoded file (mp3/flac/wav/aac/ogg).
void audio_eof_mp3(const char* info)  { (void)info; AudioEngine::onEOF(); }
void audio_info(const char* info)     { AudioEngine::onInfo(info); }
void audio_id3data(const char* info)  { AudioEngine::onID3Tag(info); }

// PCM hook — called once per stereo frame just before the I2S write.
// `sample` packs the two 16-bit channels into one uint32_t.
void audio_process_i2s(uint32_t* sample, bool* continueI2S) {
    *continueI2S = true;                       // let the library still write the frame
    int16_t* pcm = reinterpret_cast<int16_t*>(sample);
    AudioEngine::onPCM(pcm, 2);                 // 2 int16 = one L/R frame
}

void AudioEngine::begin() {
    // I2S pin configuration — must match Cardputer-Adv schematic
    s_audio.setPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
    s_audio.setVolume(volToI2S(s_vol));

    // Headphone detect pin
    pinMode(PIN_HP_DETECT, INPUT_PULLUP);

    DSP::init(48000.0f);
}

void AudioEngine::loop() {
    s_audio.loop();
    if (s_state == PlaybackState::PLAYING) {
        s_position_ms = s_audio.getAudioCurrentTime() * 1000UL;
    }
}

bool AudioEngine::play(const char* path) {
    strncpy(s_current_path, path, sizeof(s_current_path) - 1);
    s_eof = false;
    bool ok = s_audio.connecttoFS(SD, path);
    if (ok) {
        s_state = PlaybackState::PLAYING;
        s_position_ms = 0;
    }
    return ok;
}

void AudioEngine::pause() {
    if (s_state == PlaybackState::PLAYING) {
        s_audio.pauseResume();
        s_state = PlaybackState::PAUSED;
    }
}

void AudioEngine::resume() {
    if (s_state == PlaybackState::PAUSED) {
        s_audio.pauseResume();
        s_state = PlaybackState::PLAYING;
    }
}

void AudioEngine::stop() {
    s_audio.stopSong();
    s_state = PlaybackState::STOPPED;
    s_position_ms = 0;
}

bool AudioEngine::seekMs(uint32_t ms) {
    return s_audio.setAudioPlayPosition(ms / 1000);
}

uint8_t AudioEngine::volToI2S(uint8_t vol) {
    // ESP32-audioI2S volume: 0–21
    // Map our 0-30 scale to 0-21 linearly, log-weighted
    if (vol == 0) return 0;
    float frac = (float)vol / (float)VOLUME_MAX;
    // Apply log curve: perceived loudness ∝ log(vol)
    float log_vol = logf(1.0f + frac * (expf(1.0f) - 1.0f));
    return (uint8_t)(log_vol * 21.0f + 0.5f);
}

void AudioEngine::setVolume(uint8_t vol) {
    if (vol > VOLUME_MAX) vol = VOLUME_MAX;
    s_vol = vol;
    if (!s_muted) s_audio.setVolume(volToI2S(vol));
}

void AudioEngine::setMute(bool muted) {
    s_muted = muted;
    s_audio.setVolume(muted ? 0 : volToI2S(s_vol));
}

void AudioEngine::setEQPreset(EQPreset preset, const int8_t custom[5]) {
    DSP::setEQPreset(preset, custom);
}

void AudioEngine::setFullSound(bool enabled) {
    DSP::setFullSound(enabled);
}

void AudioEngine::setMono(bool enabled) {
    DSP::setMono(enabled);
}

bool AudioEngine::headphonesIn() {
    // Jack-detect: active low (jack in = GPIO pulled low by detection switch)
    return digitalRead(PIN_HP_DETECT) == LOW;
}

void AudioEngine::onEOF() {
    s_eof   = true;
    s_state = PlaybackState::STOPPED;
}

void AudioEngine::onInfo(const char* info) {
    (void)info;
}

void AudioEngine::onID3Tag(const char* info) {
    (void)info;
}

void AudioEngine::onPCM(int16_t* data, size_t len) {
    DSP::process(data, (int)len);
}

#include "AudioEngine.h"
#include "ES8311.h"
#include <Arduino.h>
#include <math.h>

// s_audio is allocated inside begin() so the Audio constructor
// (which calls i2s_driver_install and xSemaphoreCreateMutex) runs
// inside a proper FreeRTOS task, after M5.begin() has completed.
Audio*        AudioEngine::s_audio       = nullptr;
PlaybackState AudioEngine::s_state       = PlaybackState::STOPPED;
uint8_t       AudioEngine::s_vol         = VOLUME_DEFAULT;
bool          AudioEngine::s_muted       = false;
bool          AudioEngine::s_eof         = false;
uint32_t      AudioEngine::s_position_ms = 0;
char          AudioEngine::s_current_path[128] = {0};

// ── ESP32-audioI2S global callbacks ───────────────────────────────────────
void audio_eof_mp3(const char* info)  { (void)info; AudioEngine::onEOF(); }
void audio_info(const char* info)     { AudioEngine::onInfo(info); }
void audio_id3data(const char* info)  { AudioEngine::onID3Tag(info); }

void audio_process_i2s(uint32_t* sample, bool* continueI2S) {
    *continueI2S = true;
    int16_t* pcm = reinterpret_cast<int16_t*>(sample);
    AudioEngine::onPCM(pcm, 2);
}

void AudioEngine::begin() {
    // Allocate the Audio object here, inside the audioTask FreeRTOS context.
    // This ensures i2s_driver_install() and xSemaphoreCreateMutex() are called
    // after the scheduler is running and M5.begin() has already released its
    // I2S claim via M5.Speaker.end().
    s_audio = new Audio();
    if (!s_audio) return;

    // Cap input buffer to internal RAM only — no PSRAM available on this board.
    s_audio->setBufsize(8000, 0);

    s_audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
    s_audio->setVolume(volToI2S(s_vol));

    // Configure the ES8311 codec now that the I2S pins are routed. Without this
    // the codec stays powered down and no audio reaches the amplifier. Done
    // after setPinout() so BCLK (the codec's clock source) is already assigned.
    ES8311::begin();

    pinMode(PIN_HP_DETECT, INPUT_PULLUP);

    DSP::init(48000.0f);
}

void AudioEngine::loop() {
    if (!s_audio) return;
    s_audio->loop();
    if (s_state == PlaybackState::PLAYING)
        s_position_ms = s_audio->getAudioCurrentTime() * 1000UL;
}

bool AudioEngine::play(const char* path) {
    if (!s_audio) return false;
    strncpy(s_current_path, path, sizeof(s_current_path) - 1);
    s_eof = false;
    bool ok = s_audio->connecttoFS(SD, path);
    if (ok) {
        s_state       = PlaybackState::PLAYING;
        s_position_ms = 0;
    }
    return ok;
}

void AudioEngine::pause() {
    if (!s_audio) return;
    if (s_state == PlaybackState::PLAYING) {
        s_audio->pauseResume();
        s_state = PlaybackState::PAUSED;
    }
}

void AudioEngine::resume() {
    if (!s_audio) return;
    if (s_state == PlaybackState::PAUSED) {
        s_audio->pauseResume();
        s_state = PlaybackState::PLAYING;
    }
}

void AudioEngine::stop() {
    if (!s_audio) return;
    s_audio->stopSong();
    s_state       = PlaybackState::STOPPED;
    s_position_ms = 0;
}

bool AudioEngine::seekMs(uint32_t ms) {
    if (!s_audio) return false;
    return s_audio->setAudioPlayPosition(ms / 1000);
}

uint8_t AudioEngine::volToI2S(uint8_t vol) {
    if (vol == 0) return 0;
    float frac    = (float)vol / (float)VOLUME_MAX;
    float log_vol = logf(1.0f + frac * (expf(1.0f) - 1.0f));
    return (uint8_t)(log_vol * 21.0f + 0.5f);
}

void AudioEngine::setVolume(uint8_t vol) {
    if (vol > VOLUME_MAX) vol = VOLUME_MAX;
    s_vol = vol;
    if (!s_audio) return;
    if (!s_muted) s_audio->setVolume(volToI2S(vol));
}

void AudioEngine::setMute(bool muted) {
    s_muted = muted;
    if (!s_audio) return;
    s_audio->setVolume(muted ? 0 : volToI2S(s_vol));
}

void AudioEngine::setEQPreset(EQPreset preset, const int8_t custom[5]) {
    DSP::setEQPreset(preset, custom);
}

void AudioEngine::setFullSound(bool enabled) { DSP::setFullSound(enabled); }
void AudioEngine::setMono(bool enabled)      { DSP::setMono(enabled); }

bool AudioEngine::headphonesIn() {
    return digitalRead(PIN_HP_DETECT) == LOW;
}

void AudioEngine::restorePins() {
    if (!s_audio) return;
    // Re-assert BCLK/LRCLK/DOUT in the GPIO matrix.  The VoiceRecorder
    // installs I2S_NUM_1 on these same pins, which steals them from I2S_NUM_0.
    // Calling setPinout() again routes them back to AudioEngine.
    s_audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
}

void AudioEngine::onEOF() {
    s_eof   = true;
    s_state = PlaybackState::STOPPED;
}

void AudioEngine::onInfo(const char* info)   { (void)info; }
void AudioEngine::onID3Tag(const char* info) { (void)info; }

void AudioEngine::onPCM(int16_t* data, size_t len) {
    DSP::process(data, (int)len);
}

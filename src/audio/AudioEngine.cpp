#include "AudioEngine.h"
#include "ES8311.h"
#include <Arduino.h>
#include <math.h>

// s_audio is allocated inside begin() so the Audio constructor
// (which calls i2s_driver_install and xSemaphoreCreateMutex) runs
// inside a proper FreeRTOS task, after M5.begin() has completed.
Audio*        AudioEngine::s_audio                = nullptr;
PlaybackState AudioEngine::s_state                = PlaybackState::STOPPED;
uint8_t       AudioEngine::s_vol                  = VOLUME_DEFAULT;
bool          AudioEngine::s_muted                = false;
bool          AudioEngine::s_eof                  = false;
uint32_t      AudioEngine::s_position_ms          = 0;
uint32_t      AudioEngine::s_computed_duration_ms = 0;
char          AudioEngine::s_current_path[128]    = {0};

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

    // Use 16 KB input buffer (internal RAM only — no PSRAM on this board).
    // 16 KB ≈ 90 ms at 44.1 kHz 16-bit stereo, giving headroom for SD latency spikes.
    s_audio->setBufsize(16000, 0);

    s_audio->setPinout(PIN_I2S_BCLK, PIN_I2S_LRCLK, PIN_I2S_DOUT);
    s_audio->setVolume(volToI2S(s_vol));

    // Configure the ES8311 codec now that the I2S pins are routed. Without this
    // the codec stays powered down and no audio reaches the amplifier. Done
    // after setPinout() so BCLK (the codec's clock source) is already assigned.
    ES8311::begin();

    // NOTE: GPIO 38 is the display backlight PWM pin (LEDC ch7, set up by
    // M5GFX during M5.begin). Do NOT call pinMode(38, ...) here — that would
    // reconfigure the GPIO mux away from LEDC and break setBrightness().
    // Headphone-detect is therefore not available on this board variant.

    DSP::init(48000.0f);
}

void AudioEngine::loop() {
    if (!s_audio) return;
    s_audio->loop();
    if (s_state == PlaybackState::PLAYING) {
        if (!s_audio->isRunning()) {
            // Library stopped internally (codec error, early stopSong, or normal
            // EOF on a path that doesn't call audio_eof_mp3). Treat as EOF so
            // the audioTask handler can update state cleanly.
            s_eof   = true;
            s_state = PlaybackState::STOPPED;
        } else {
            s_position_ms = s_audio->getAudioCurrentTime() * 1000UL;
        }
    }
}

uint32_t AudioEngine::durationMs() {
    if (!s_audio) return 0;
    if (s_computed_duration_ms) return s_computed_duration_ms;
    return s_audio->getAudioFileDuration() * 1000UL;
}

bool AudioEngine::probeFile(const char* path, float* out_sr, uint32_t* out_dur_ms) {
    if (out_sr)      *out_sr      = 0.0f;
    if (out_dur_ms)  *out_dur_ms  = 0;

    const char* ext = strrchr(path, '.');
    if (!ext) return false;
    ext++;

    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    bool found = false;

    if (strcasecmp(ext, "wav") == 0) {
        uint8_t h[44];
        if (f.read(h, 44) == 44 &&
            h[0]=='R' && h[1]=='I' && h[2]=='F' && h[3]=='F' &&
            h[8]=='W' && h[9]=='A' && h[10]=='V' && h[11]=='E' &&
            h[12]=='f' && h[13]=='m' && h[14]=='t') {
            uint32_t sr  = (uint32_t)h[24] | ((uint32_t)h[25]<<8) |
                           ((uint32_t)h[26]<<16) | ((uint32_t)h[27]<<24);
            uint16_t ch  = (uint16_t)h[22] | ((uint16_t)h[23]<<8);
            uint16_t bps = (uint16_t)h[34] | ((uint16_t)h[35]<<8);
            if (sr && out_sr) *out_sr = (float)sr;
            // Standard 44-byte WAV: 'data' chunk immediately follows fmt
            if (h[36]=='d' && h[37]=='a' && h[38]=='t' && h[39]=='a' &&
                sr && ch && bps && out_dur_ms) {
                uint32_t data_sz = (uint32_t)h[40] | ((uint32_t)h[41]<<8) |
                                   ((uint32_t)h[42]<<16) | ((uint32_t)h[43]<<24);
                uint32_t bps_rate = sr * ch * bps / 8;
                if (bps_rate) *out_dur_ms = (uint32_t)((uint64_t)data_sz * 1000 / bps_rate);
            }
            found = (sr > 0);
        }
    } else if (strcasecmp(ext, "mp3") == 0) {
        // Read first 10 bytes to detect and skip ID3v2 tag
        uint8_t buf[220];
        if (f.read(buf, 10) == 10 &&
            buf[0]=='I' && buf[1]=='D' && buf[2]=='3') {
            uint32_t id3sz = ((uint32_t)(buf[6] & 0x7f) << 21) |
                             ((uint32_t)(buf[7] & 0x7f) << 14) |
                             ((uint32_t)(buf[8] & 0x7f) <<  7) |
                              (uint32_t)(buf[9] & 0x7f);
            id3sz += 10; // include the 10-byte ID3 header itself
            f.seek(id3sz);
        } else {
            f.seek(0);
        }

        int n = f.read(buf, sizeof(buf));
        // Find MP3 sync word then look for Xing/Info VBR header
        for (int i = 0; i < n - 4; i++) {
            if (buf[i] != 0xFF || (buf[i+1] & 0xE0) != 0xE0) continue;

            uint8_t b1 = buf[i+1], b2 = buf[i+2], b3 = buf[i+3];
            int mpeg   = (b1 >> 3) & 3; // 3=MPEG1, 2=MPEG2, 0=MPEG2.5
            int sr_idx = (b2 >> 2) & 3;
            int ch_m   = (b3 >> 6) & 3; // 3=mono

            static const uint32_t sr_tab[3][4] = {
                {44100, 48000, 32000, 0},
                {22050, 24000, 16000, 0},
                {11025, 12000,  8000, 0},
            };
            int vi = (mpeg == 3) ? 0 : (mpeg == 2) ? 1 : 2;
            uint32_t sr = sr_tab[vi][sr_idx];
            if (!sr) break;

            if (out_sr) *out_sr = (float)sr;
            found = true;

            // Side-information size determines where Xing header lives in the frame
            int si   = (mpeg == 3) ? (ch_m == 3 ? 17 : 32) : (ch_m == 3 ? 9 : 17);
            int xoff = i + 4 + si;
            uint32_t spf = (mpeg == 3) ? 1152 : 576;

            if (out_dur_ms && xoff + 12 < n) {
                bool is_xing = (buf[xoff]=='X' && buf[xoff+1]=='i' &&
                                buf[xoff+2]=='n' && buf[xoff+3]=='g') ||
                               (buf[xoff]=='I' && buf[xoff+1]=='n' &&
                                buf[xoff+2]=='f' && buf[xoff+3]=='o');
                if (is_xing) {
                    uint32_t flags = ((uint32_t)buf[xoff+4]<<24) | ((uint32_t)buf[xoff+5]<<16) |
                                     ((uint32_t)buf[xoff+6]<< 8) |  (uint32_t)buf[xoff+7];
                    if (flags & 1) { // num_frames field present
                        uint32_t nf = ((uint32_t)buf[xoff+ 8]<<24) | ((uint32_t)buf[xoff+ 9]<<16) |
                                      ((uint32_t)buf[xoff+10]<< 8) |  (uint32_t)buf[xoff+11];
                        if (nf && sr)
                            *out_dur_ms = (uint32_t)((uint64_t)nf * spf * 1000 / sr);
                    }
                }
            }
            break;
        }
    }

    f.close();
    return found;
}

bool AudioEngine::play(const char* path) {
    if (!s_audio) return false;
    strncpy(s_current_path, path, sizeof(s_current_path) - 1);
    s_eof = false;

    // Probe the file for its actual sample rate and duration before handing
    // it to the library. This ensures DSP filter coefficients are computed for
    // the right rate (important for non-48 kHz WAV) and we display an accurate
    // duration without relying on the library's VBR bitrate estimation.
    float    probe_sr  = 0.0f;
    uint32_t probe_dur = 0;
    probeFile(path, &probe_sr, &probe_dur);
    if (probe_sr > 0) DSP::init(probe_sr);
    s_computed_duration_ms = probe_dur;

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
    return false;  // GPIO 38 is the backlight PWM pin; HP-detect unavailable
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

#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

#include "config.h"
#include "types.h"
#include "audio/AudioEngine.h"
#include "audio/DSP.h"
#include "keyboard/TCA8418.h"
#include "storage/Library.h"
#include "storage/NVSConfig.h"
#include "storage/PlaylistManager.h"
#include "ui/UIManager.h"
#include "recorder/VoiceRecorder.h"
#include "battery/BatteryMonitor.h"

// ── Global state ───────────────────────────────────────────────────────────
static AppState       g_state;
static Library        g_lib;
static PlaylistManager g_playlist;
static SemaphoreHandle_t g_state_mutex;
static QueueHandle_t     g_key_queue;

// Shuffle order for current library view
static int g_shuffle_order[LIBRARY_MAX_TRACKS];
static int g_shuffle_pos = 0;
static int g_queue[LIBRARY_MAX_TRACKS];
static int g_queue_len = 0;

// ── Helper: build playback queue from library ──────────────────────────────
static void buildQueue(bool shuffle) {
    g_queue_len = g_lib.count();
    for (int i = 0; i < g_queue_len; i++) g_queue[i] = i;
    if (shuffle && g_queue_len > 0) {
        g_lib.buildShuffleOrder(g_shuffle_order, g_queue_len);
    }
}

// ── Helper: get next track index ──────────────────────────────────────────
static int nextTrack(bool forward, const AppState& state) {
    int count = g_queue_len;
    if (count == 0) return 0;

    if (state.repeat == RepeatMode::ONE) return state.current_track_idx;

    int pos = state.current_track_idx + (forward ? 1 : -1);
    if (pos < 0) {
        pos = (state.repeat == RepeatMode::ALL) ? count - 1 : 0;
    } else if (pos >= count) {
        if (state.repeat == RepeatMode::ALL) {
            pos = 0;
            if (state.shuffle) g_lib.buildShuffleOrder(g_shuffle_order, count);
        } else {
            pos = count - 1;
        }
    }
    return state.shuffle ? g_shuffle_order[pos] : g_queue[pos];
}

// ── Helper: start playing a track by index ────────────────────────────────
static void playTrack(int idx) {
    const TrackInfo& t = g_lib.track(idx);
    if (t.path[0] == '\0') return;

    AudioEngine::play(t.path);
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        g_state.current_track_idx = idx;
        g_state.track_pos_ms      = 0;
        g_state.playback          = PlaybackState::PLAYING;
        xSemaphoreGive(g_state_mutex);
    }
    UIManager::loadAlbumArt(t.path, t.has_art);
}

// ── Helper: apply headphone routing ───────────────────────────────────────
static void updateAudioRouting(AppState& state) {
    bool hp = AudioEngine::headphonesIn();
    if (hp != state.headphones_in) {
        state.headphones_in = hp;
        // ES8311 output routing change would go here via I2C register writes
        // For now we note the state change; M5Unified handles speaker/HP switching
    }
}

// ── Helper: sleep timer check ─────────────────────────────────────────────
static void checkSleepTimer(AppState& state) {
    if (state.sleep_deadline == 0) return;
    if (millis() >= state.sleep_deadline) {
        // Finish current track, then sleep
        if (state.playback == PlaybackState::STOPPED) {
            NVSConfig::save(state, g_lib.track(state.current_track_idx).path, state.track_pos_ms);
            esp_deep_sleep_start();
        }
        // else: wait for EOF which will trigger STOPPED state on next loop
    }
}

// ── Key event handler (runs in UITask) ────────────────────────────────────
static void handleKey(const KeyEvent& ev, AppState& state) {
    if (!ev.pressed) return;  // only act on key-down

    switch (ev.code) {
        // ── Navigation ───────────────────────────────────────────────────
        case KeyCode::UP:
            if (state.current_screen == Screen::LIBRARY) {
                if (state.lib_cursor > 0) state.lib_cursor--;
            } else if (state.current_screen == Screen::SLEEP_TIMER) {
                if (state.sleep_timer_idx > 0) state.sleep_timer_idx--;
            }
            break;

        case KeyCode::DOWN:
            if (state.current_screen == Screen::LIBRARY) {
                if (state.lib_cursor < g_lib.count() - 1) state.lib_cursor++;
            } else if (state.current_screen == Screen::SLEEP_TIMER) {
                if (state.sleep_timer_idx < SLEEP_TIMER_COUNT - 1) state.sleep_timer_idx++;
            }
            break;

        case KeyCode::LEFT:
            // Previous track
            {
                int idx = nextTrack(false, state);
                playTrack(idx);
            }
            break;

        case KeyCode::RIGHT:
            // Next track
            {
                int idx = nextTrack(true, state);
                playTrack(idx);
            }
            break;

        // ── Seek ─────────────────────────────────────────────────────────
        case KeyCode::FN_LEFT:
            if (state.track_pos_ms > 5000) {
                AudioEngine::seekMs(state.track_pos_ms - 5000);
            }
            break;

        case KeyCode::FN_RIGHT:
            AudioEngine::seekMs(state.track_pos_ms + 5000);
            break;

        // ── Play / Pause / Select ─────────────────────────────────────────
        case KeyCode::ENTER:
            if (state.current_screen == Screen::LIBRARY) {
                // Play selected track
                playTrack(state.lib_cursor);
                state.current_screen = Screen::NOW_PLAYING;
            } else if (state.current_screen == Screen::NOW_PLAYING) {
                if (state.playback == PlaybackState::PLAYING) {
                    AudioEngine::pause();
                    state.playback = PlaybackState::PAUSED;
                } else if (state.playback == PlaybackState::PAUSED) {
                    AudioEngine::resume();
                    state.playback = PlaybackState::PLAYING;
                } else {
                    playTrack(state.current_track_idx);
                }
            } else if (state.current_screen == Screen::SLEEP_TIMER) {
                // Confirm timer selection
                uint16_t mins = SLEEP_TIMER_OPTIONS[state.sleep_timer_idx];
                state.sleep_deadline = (mins > 0) ? millis() + (uint32_t)mins * 60000UL : 0;
                NVSConfig::saveSleepTimer(state.sleep_timer_idx);
                state.current_screen = Screen::NOW_PLAYING;
                UIManager::showNotif(mins > 0 ? "Timer set" : "Timer off");
            }
            break;

        // ── Volume ────────────────────────────────────────────────────────
        case KeyCode::PLUS:
            if (state.volume < VOLUME_MAX) {
                state.volume++;
                AudioEngine::setVolume(state.volume);
                NVSConfig::saveVolume(state.volume);
            }
            break;

        case KeyCode::MINUS:
            if (state.volume > 0) {
                state.volume--;
                AudioEngine::setVolume(state.volume);
                NVSConfig::saveVolume(state.volume);
            }
            break;

        // ── Shuffle toggle ────────────────────────────────────────────────
        case KeyCode::FN_S:
            state.shuffle = !state.shuffle;
            if (state.shuffle) buildQueue(true);
            NVSConfig::savePlaybackMode(state.shuffle, state.repeat);
            UIManager::showNotif(state.shuffle ? "Shuffle ON" : "Shuffle OFF");
            break;

        // ── Repeat cycle ──────────────────────────────────────────────────
        case KeyCode::FN_R:
            state.repeat = (RepeatMode)(((uint8_t)state.repeat + 1) % 3);
            NVSConfig::savePlaybackMode(state.shuffle, state.repeat);
            {
                const char* rm = (state.repeat == RepeatMode::OFF) ? "Repeat OFF" :
                                 (state.repeat == RepeatMode::ONE) ? "Repeat ONE" : "Repeat ALL";
                UIManager::showNotif(rm);
            }
            break;

        // ── EQ cycle ──────────────────────────────────────────────────────
        case KeyCode::FN_E:
            {
                uint8_t next = ((uint8_t)state.eq_preset + 1) % (uint8_t)EQPreset::EQ_COUNT;
                state.eq_preset = (EQPreset)next;
                AudioEngine::setEQPreset(state.eq_preset, state.eq_custom);
                NVSConfig::saveEQPreset(state.eq_preset, state.eq_custom);
                UIManager::showNotif(EQ_PRESET_NAMES[next]);
            }
            break;

        // ── FullSound toggle ──────────────────────────────────────────────
        case KeyCode::FN_F:
            state.fullsound = !state.fullsound;
            AudioEngine::setFullSound(state.fullsound);
            NVSConfig::saveFullSound(state.fullsound);
            UIManager::showNotif(state.fullsound ? "FullSound ON" : "FullSound OFF");
            break;

        // ── Mono / stereo toggle ──────────────────────────────────────────
        case KeyCode::FN_O:
            state.mono = !state.mono;
            AudioEngine::setMono(state.mono);
            NVSConfig::saveMono(state.mono);
            UIManager::showNotif(state.mono ? "Mono" : "Stereo");
            break;

        // ── Sleep timer menu ──────────────────────────────────────────────
        case KeyCode::FN_T:
            state.current_screen = Screen::SLEEP_TIMER;
            break;

        // ── Mute ──────────────────────────────────────────────────────────
        case KeyCode::FN_M:
            state.muted = !state.muted;
            AudioEngine::setMute(state.muted);
            UIManager::showNotif(state.muted ? "Muted" : "Unmuted");
            break;

        // ── Voice recorder ────────────────────────────────────────────────
        case KeyCode::FN_REC:
            if (state.current_screen != Screen::VOICE_RECORDER) {
                AudioEngine::stop();
                state.playback       = PlaybackState::STOPPED;
                state.current_screen = Screen::VOICE_RECORDER;
                VoiceRecorder::startRecording();
            } else {
                VoiceRecorder::stopRecording();
                state.current_screen = Screen::NOW_PLAYING;
                UIManager::showNotif("Recording saved");
            }
            break;

        // ── Back / ESC ────────────────────────────────────────────────────
        case KeyCode::ESC:
            if (state.current_screen != Screen::NOW_PLAYING) {
                state.current_screen = Screen::NOW_PLAYING;
            } else {
                state.current_screen = Screen::LIBRARY;
            }
            break;

        // ── Add to favorites (long OK handled as OK_LONG) ─────────────────
        case KeyCode::OK_LONG:
            if (state.current_screen == Screen::NOW_PLAYING) {
                g_playlist.addFavorite(g_lib.track(state.current_track_idx).path);
                UIManager::showNotif("Added to Favorites");
            }
            break;

        default:
            break;
    }
}

// ══════════════════════════════════════════════════════════════════════════
// FreeRTOS Tasks
// ══════════════════════════════════════════════════════════════════════════

// ── Audio task (Core 1, priority 5) ───────────────────────────────────────
static void audioTask(void* arg) {
    AudioEngine::begin();

    for (;;) {
        AudioEngine::loop();

        // Auto-advance on EOF
        if (AudioEngine::isEOF()) {
            AudioEngine::clearEOF();
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                AppState& s = g_state;
                if (s.repeat == RepeatMode::ONE) {
                    playTrack(s.current_track_idx);
                } else {
                    int next = nextTrack(true, s);
                    // Check if we wrapped and repeat is off
                    bool at_end = (!s.shuffle && next <= s.current_track_idx && s.repeat == RepeatMode::OFF);
                    if (at_end) {
                        s.playback = PlaybackState::STOPPED;
                    } else {
                        playTrack(next);
                    }
                }
                // Sleep timer: if stopped and deadline passed, sleep
                checkSleepTimer(s);
                xSemaphoreGive(g_state_mutex);
            }
        }

        // Save position every 5s
        static uint32_t last_save = 0;
        if (millis() - last_save > 5000) {
            last_save = millis();
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                g_state.track_pos_ms = AudioEngine::positionMs();
                g_state.playback     = AudioEngine::state();
                xSemaphoreGive(g_state_mutex);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── Keyboard task (Core 0, priority 4) ────────────────────────────────────
static void keyboardTask(void* arg) {
    TCA8418::begin();

    for (;;) {
        if (TCA8418::available()) {
            KeyEvent ev = TCA8418::nextKey();
            if (ev.code != KeyCode::NONE || ev.ch != 0) {
                xQueueSend(g_key_queue, &ev, 0);
            }
            // Drain FIFO
            while (TCA8418::available()) {
                ev = TCA8418::nextKey();
                if (ev.code != KeyCode::NONE || ev.ch != 0) {
                    xQueueSend(g_key_queue, &ev, 0);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ── UI task (Core 0, priority 3) ──────────────────────────────────────────
static void uiTask(void* arg) {
    UIManager::begin();

    for (;;) {
        // Process key events
        KeyEvent ev;
        while (xQueueReceive(g_key_queue, &ev, 0) == pdTRUE) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                handleKey(ev, g_state);
                updateAudioRouting(g_state);
                xSemaphoreGive(g_state_mutex);
            }
        }

        // Sync audio state into app state
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_state.track_pos_ms = AudioEngine::positionMs();
            g_state.playback     = AudioEngine::state();
            g_state.battery_pct  = BatteryMonitor::percent();
            g_state.charging     = BatteryMonitor::isCharging();
            xSemaphoreGive(g_state_mutex);
        }

        // Draw recorder screen if active
        if (g_state.current_screen == Screen::VOICE_RECORDER) {
            UIManager::drawRecorder(g_state, VoiceRecorder::elapsedMs(), VoiceRecorder::peakLevel());
        } else {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                AppState snap = g_state;
                xSemaphoreGive(g_state_mutex);
                UIManager::draw(snap, g_lib, g_playlist);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33));  // ~30 fps
    }
}

// ── Battery task (Core 0, priority 1) ─────────────────────────────────────
static void batteryTask(void* arg) {
    BatteryMonitor::begin();
    BatteryMonitor::task(arg);  // loops internally
}

// ── Recorder task (Core 1, priority 4) ────────────────────────────────────
static void recorderTask(void* arg) {
    VoiceRecorder::begin();
    VoiceRecorder::task(arg);   // loops internally
}

// ══════════════════════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════════════════════

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);

    // Mount SD card. The SPI bus pins must be set explicitly — the Arduino
    // defaults are wrong for this board, which presents as a mount failure
    // even with a correctly formatted card. Retry a few times and fall back
    // to a slower clock, which some cards need to enumerate reliably.
    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    bool sd_ok = false;
    for (int attempt = 0; attempt < 3 && !sd_ok; attempt++) {
        uint32_t freq = (attempt == 0) ? SD_SPI_FREQ_HZ : 4000000;  // back off to 4 MHz
        sd_ok = SD.begin(PIN_SD_CS, SPI, freq);
        if (!sd_ok) {
            SD.end();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    if (!sd_ok) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("SD CARD ERROR", 10, 30);
        M5.Display.drawString("Check card seated / FAT32", 10, 50);
        while (true) vTaskDelay(1000);
    }

    // Create synchronisation primitives
    g_state_mutex = xSemaphoreCreateMutex();
    g_key_queue   = xQueueCreate(KEY_QUEUE_LEN, sizeof(KeyEvent));
    VoiceRecorder::sd_mutex = xSemaphoreCreateMutex();

    // Load persisted settings
    char last_track[256] = {0};
    uint32_t last_pos_ms = 0;
    NVSConfig::load(g_state, last_track, &last_pos_ms);

    // Defaults
    g_state.playback        = PlaybackState::STOPPED;
    g_state.headphones_in   = false;
    g_state.battery_pct     = 0;
    g_state.charging        = false;
    g_state.current_screen  = Screen::LIBRARY;
    g_state.lib_cursor      = 0;
    g_state.sleep_deadline  = 0;
    g_state.muted           = false;

    // Scan library
    g_lib.scan();
    buildQueue(g_state.shuffle);

    // Apply persisted EQ/DSP before audio starts
    DSP::init(48000.0f);
    DSP::setEQPreset(g_state.eq_preset, g_state.eq_custom);
    DSP::setFullSound(g_state.fullsound);
    DSP::setMono(g_state.mono);

    // Resume last track if available
    if (last_track[0] != '\0' && g_lib.count() > 0) {
        int idx = g_lib.findByPath(last_track);
        if (idx >= 0) {
            g_state.current_track_idx = idx;
            g_state.current_screen    = Screen::NOW_PLAYING;
            // AudioEngine will be started in audioTask; we flag a deferred play
            // by setting playback to PAUSED — user presses ENTER to resume.
            g_state.playback = PlaybackState::PAUSED;
        }
    }

    // Spawn tasks
    xTaskCreatePinnedToCore(audioTask,    "AudioTask", AUDIO_TASK_STACK, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(keyboardTask, "KbdTask",   KBD_TASK_STACK,   nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,       "UITask",    UI_TASK_STACK,    nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(recorderTask, "RecTask",   REC_TASK_STACK,   nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(batteryTask,  "BatTask",   BAT_TASK_STACK,   nullptr, 1, nullptr, 0);
}

void loop() {
    // All logic is in FreeRTOS tasks; main loop is idle.
    vTaskDelay(pdMS_TO_TICKS(1000));
}

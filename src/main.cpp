#include <Arduino.h>
#include <M5Unified.h>
#include <SD.h>
#include <SPI.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <esp_system.h>

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
static AppState        g_state;
static Library         g_lib;
static PlaylistManager g_playlist;
static SemaphoreHandle_t g_state_mutex;
static QueueHandle_t     g_key_queue;

// ── Boot diagnostics ───────────────────────────────────────────────────────
#define BOOT_MAGIC 0xB007C0DEu
RTC_NOINIT_ATTR static uint32_t g_boot_magic;
RTC_NOINIT_ATTR static char     g_boot_last_stage[40];
static int g_boot_line = 0;

static void markStage(const char* stage) {
    strncpy(g_boot_last_stage, stage, sizeof(g_boot_last_stage) - 1);
    g_boot_last_stage[sizeof(g_boot_last_stage) - 1] = '\0';
    g_boot_magic = BOOT_MAGIC;
    Serial.printf("[BOOT] %s\n", stage);
}

static void bootStage(const char* stage) {
    markStage(stage);
    M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
    M5.Display.setCursor(4, 4 + g_boot_line * 12);
    M5.Display.print(stage);
    g_boot_line++;
}

// ── Helper: set track name display string from a full path ─────────────────
static void setTrackName(AppState& state, const char* path) {
    const char* fname = strrchr(path, '/');
    fname = fname ? fname + 1 : path;
    strncpy(state.current_track_name, fname, sizeof(state.current_track_name) - 1);
    state.current_track_name[sizeof(state.current_track_name) - 1] = '\0';
    // Strip extension for cleaner display
    char* dot = strrchr(state.current_track_name, '.');
    if (dot) *dot = '\0';
}

// ── Helper: start playing a file by path ──────────────────────────────────
static void playPath(const char* path) {
    AudioEngine::play(path);
    if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        strncpy(g_state.current_track_path, path,
                sizeof(g_state.current_track_path) - 1);
        g_state.current_track_path[sizeof(g_state.current_track_path)-1] = '\0';
        setTrackName(g_state, path);
        g_state.track_pos_ms = 0;
        g_state.playback     = PlaybackState::PLAYING;
        xSemaphoreGive(g_state_mutex);
    }
    UIManager::loadAlbumArt(path, false);
}

// ── Helper: apply headphone routing ───────────────────────────────────────
static void updateAudioRouting(AppState& state) {
    bool hp = AudioEngine::headphonesIn();
    if (hp != state.headphones_in) state.headphones_in = hp;
}

// ── Helper: sleep timer check ─────────────────────────────────────────────
static void checkSleepTimer(AppState& state) {
    if (state.sleep_deadline == 0) return;
    if (millis() >= state.sleep_deadline &&
        state.playback == PlaybackState::STOPPED) {
        NVSConfig::save(state, state.current_track_path, state.track_pos_ms);
        esp_deep_sleep_start();
    }
}

// ── Key event handler (runs in UITask) ────────────────────────────────────
static void handleKey(const KeyEvent& ev, AppState& state) {
    if (!ev.pressed) return;

    switch (ev.code) {

        // ── Browser navigation ────────────────────────────────────────────
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
                if (state.sleep_timer_idx < SLEEP_TIMER_COUNT - 1)
                    state.sleep_timer_idx++;
            }
            break;

        case KeyCode::ENTER:
            if (state.current_screen == Screen::LIBRARY) {
                int idx = state.lib_cursor;
                if (g_lib.isAudioFile(idx)) {
                    char path[128];
                    if (g_lib.getFullPath(idx, path, sizeof(path))) {
                        playPath(path);
                        state.current_screen = Screen::NOW_PLAYING;
                    }
                } else {
                    // Enter directory
                    if (g_lib.enterDir(idx)) {
                        state.lib_cursor = 0;
                    }
                }
            } else if (state.current_screen == Screen::NOW_PLAYING) {
                if (state.playback == PlaybackState::PLAYING) {
                    AudioEngine::pause();
                    state.playback = PlaybackState::PAUSED;
                } else if (state.playback == PlaybackState::PAUSED) {
                    AudioEngine::resume();
                    state.playback = PlaybackState::PLAYING;
                } else if (state.current_track_path[0]) {
                    playPath(state.current_track_path);
                }
            } else if (state.current_screen == Screen::SLEEP_TIMER) {
                uint16_t mins = SLEEP_TIMER_OPTIONS[state.sleep_timer_idx];
                state.sleep_deadline = mins > 0 ? millis() + (uint32_t)mins * 60000UL : 0;
                NVSConfig::saveSleepTimer(state.sleep_timer_idx);
                state.current_screen = Screen::NOW_PLAYING;
                UIManager::showNotif(mins > 0 ? "Timer set" : "Timer off");
            }
            break;

        case KeyCode::ESC:
            if (state.current_screen == Screen::LIBRARY) {
                // Go up one directory level
                g_lib.goUp();
                state.lib_cursor = 0;
            } else {
                state.current_screen =
                    (state.current_screen == Screen::NOW_PLAYING)
                    ? Screen::LIBRARY : Screen::NOW_PLAYING;
            }
            break;

        // ── Prev / next track ─────────────────────────────────────────────
        case KeyCode::LEFT:
            {
                char adj[128];
                if (g_lib.getAdjacentTrack(state.current_track_path, -1,
                                            adj, sizeof(adj))) {
                    playPath(adj);
                }
            }
            break;

        case KeyCode::RIGHT:
            {
                char adj[128];
                if (g_lib.getAdjacentTrack(state.current_track_path, 1,
                                            adj, sizeof(adj))) {
                    playPath(adj);
                }
            }
            break;

        // ── Seek ──────────────────────────────────────────────────────────
        case KeyCode::FN_LEFT:
            if (state.track_pos_ms > 5000)
                AudioEngine::seekMs(state.track_pos_ms - 5000);
            break;

        case KeyCode::FN_RIGHT:
            AudioEngine::seekMs(state.track_pos_ms + 5000);
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
            NVSConfig::savePlaybackMode(state.shuffle, state.repeat);
            UIManager::showNotif(state.shuffle ? "Shuffle ON" : "Shuffle OFF");
            break;

        // ── Repeat cycle ──────────────────────────────────────────────────
        case KeyCode::FN_R:
            state.repeat = (RepeatMode)(((uint8_t)state.repeat + 1) % 3);
            NVSConfig::savePlaybackMode(state.shuffle, state.repeat);
            {
                const char* rm =
                    (state.repeat == RepeatMode::OFF) ? "Repeat OFF" :
                    (state.repeat == RepeatMode::ONE) ? "Repeat ONE" : "Repeat ALL";
                UIManager::showNotif(rm);
            }
            break;

        // ── EQ cycle ──────────────────────────────────────────────────────
        case KeyCode::FN_E:
            {
                uint8_t next = ((uint8_t)state.eq_preset + 1) %
                               (uint8_t)EQPreset::EQ_COUNT;
                state.eq_preset = (EQPreset)next;
                AudioEngine::setEQPreset(state.eq_preset, state.eq_custom);
                NVSConfig::saveEQPreset(state.eq_preset, state.eq_custom);
                UIManager::showNotif(EQ_PRESET_NAMES[next]);
            }
            break;

        // ── FullSound ─────────────────────────────────────────────────────
        case KeyCode::FN_F:
            state.fullsound = !state.fullsound;
            AudioEngine::setFullSound(state.fullsound);
            NVSConfig::saveFullSound(state.fullsound);
            UIManager::showNotif(state.fullsound ? "FullSound ON" : "FullSound OFF");
            break;

        // ── Mono toggle ───────────────────────────────────────────────────
        case KeyCode::FN_O:
            state.mono = !state.mono;
            AudioEngine::setMono(state.mono);
            NVSConfig::saveMono(state.mono);
            UIManager::showNotif(state.mono ? "Mono" : "Stereo");
            break;

        // ── Sleep timer ───────────────────────────────────────────────────
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

        // ── Add to favorites ──────────────────────────────────────────────
        case KeyCode::OK_LONG:
            if (state.current_track_path[0]) {
                g_playlist.addFavorite(state.current_track_path);
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

static void audioTask(void* arg) {
    markStage("audio: begin");
    AudioEngine::begin();
    markStage("audio: ready");

    for (;;) {
        AudioEngine::loop();

        if (AudioEngine::isEOF()) {
            AudioEngine::clearEOF();
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                AppState& s = g_state;

                if (s.repeat == RepeatMode::ONE && s.current_track_path[0]) {
                    playPath(s.current_track_path);
                } else {
                    char next[128];
                    if (g_lib.getAdjacentTrack(s.current_track_path, 1,
                                                next, sizeof(next))) {
                        playPath(next);
                    } else if (s.repeat == RepeatMode::ALL) {
                        // Wrap around: find first audio file in current dir
                        for (int i = 0; i < g_lib.count(); i++) {
                            if (g_lib.isAudioFile(i)) {
                                char first[128];
                                g_lib.getFullPath(i, first, sizeof(first));
                                playPath(first);
                                break;
                            }
                        }
                    } else {
                        s.playback = PlaybackState::STOPPED;
                    }
                }
                checkSleepTimer(s);
                xSemaphoreGive(g_state_mutex);
            }
        }

        // Save position every 5 s
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

static void keyboardTask(void* arg) {
    markStage("kbd: begin");
    TCA8418::begin();
    markStage("kbd: ready");

    for (;;) {
        if (TCA8418::available()) {
            KeyEvent ev = TCA8418::nextKey();
            if (ev.code != KeyCode::NONE || ev.ch != 0)
                xQueueSend(g_key_queue, &ev, 0);
            while (TCA8418::available()) {
                ev = TCA8418::nextKey();
                if (ev.code != KeyCode::NONE || ev.ch != 0)
                    xQueueSend(g_key_queue, &ev, 0);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void uiTask(void* arg) {
    markStage("ui: begin");
    UIManager::begin();
    markStage("ui: ready");

    for (;;) {
        KeyEvent ev;
        while (xQueueReceive(g_key_queue, &ev, 0) == pdTRUE) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                handleKey(ev, g_state);
                updateAudioRouting(g_state);
                xSemaphoreGive(g_state_mutex);
            }
        }

        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            g_state.track_pos_ms = AudioEngine::positionMs();
            g_state.playback     = AudioEngine::state();
            g_state.battery_pct  = BatteryMonitor::percent();
            g_state.charging     = BatteryMonitor::isCharging();
            xSemaphoreGive(g_state_mutex);
        }

        if (g_state.current_screen == Screen::VOICE_RECORDER) {
            UIManager::drawRecorder(g_state,
                                    VoiceRecorder::elapsedMs(),
                                    VoiceRecorder::peakLevel());
        } else {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
                AppState snap = g_state;
                xSemaphoreGive(g_state_mutex);
                UIManager::draw(snap, g_lib, g_playlist);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

static void batteryTask(void* arg) {
    BatteryMonitor::begin();
    BatteryMonitor::task(arg);
}

static void recorderTask(void* arg) {
    VoiceRecorder::begin();
    VoiceRecorder::task(arg);
}

// ══════════════════════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════════════════════

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    Serial.begin(115200);
    delay(200);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    M5.Display.setTextSize(1);

    esp_reset_reason_t rr = esp_reset_reason();
    bool crashed = (g_boot_magic == BOOT_MAGIC) &&
                   (rr == ESP_RST_PANIC     || rr == ESP_RST_TASK_WDT ||
                    rr == ESP_RST_INT_WDT   || rr == ESP_RST_WDT      ||
                    rr == ESP_RST_BROWNOUT);
    if (crashed) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.setCursor(4, 4);
        M5.Display.println("CRASH after stage:");
        M5.Display.setTextSize(2);
        M5.Display.println(g_boot_last_stage);
        M5.Display.setTextSize(1);
        M5.Display.printf("\nreset reason = %d\n", (int)rr);
        M5.Display.println("\n(holding 6s)");
        delay(6000);
        M5.Display.fillScreen(TFT_BLACK);
    }
    g_boot_magic = 0;
    g_boot_line  = 0;

    bootStage("M5 init OK");

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, I2C_FREQ_HZ);
    bootStage("I2C init OK");

    SPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);

    bool sd_ok = false;
    for (int attempt = 0; attempt < 3 && !sd_ok; attempt++) {
        uint32_t freq = (attempt == 0) ? SD_SPI_FREQ_HZ : 4000000;
        sd_ok = SD.begin(PIN_SD_CS, SPI, freq, "/sd", 10);
        if (!sd_ok) { SD.end(); vTaskDelay(pdMS_TO_TICKS(100)); }
    }
    if (!sd_ok) {
        M5.Display.fillScreen(TFT_RED);
        M5.Display.setTextColor(TFT_WHITE, TFT_RED);
        M5.Display.drawString("SD CARD ERROR", 10, 30);
        M5.Display.drawString("Check card / FAT32", 10, 50);
        while (true) vTaskDelay(1000);
    }
    bootStage("SD mount OK");

    g_state_mutex            = xSemaphoreCreateMutex();
    g_key_queue              = xQueueCreate(KEY_QUEUE_LEN, sizeof(KeyEvent));
    VoiceRecorder::sd_mutex  = xSemaphoreCreateMutex();

    // Initialise the file browser at SD root
    g_lib.begin("/");
    bootStage("browser ready");

    // Load persisted settings
    char last_track[128] = {0};
    uint32_t last_pos_ms = 0;
    NVSConfig::load(g_state, last_track, &last_pos_ms);

    // Defaults
    g_state.playback       = PlaybackState::STOPPED;
    g_state.headphones_in  = false;
    g_state.battery_pct    = 0;
    g_state.charging       = false;
    g_state.current_screen = Screen::LIBRARY;
    g_state.lib_cursor     = 0;
    g_state.sleep_deadline = 0;
    g_state.muted          = false;

    // Resume last track if one was saved
    if (last_track[0] != '\0') {
        strncpy(g_state.current_track_path, last_track,
                sizeof(g_state.current_track_path) - 1);
        setTrackName(g_state, last_track);
        g_state.track_pos_ms   = last_pos_ms;
        g_state.current_screen = Screen::NOW_PLAYING;
        g_state.playback       = PlaybackState::PAUSED;
    }

    DSP::init(48000.0f);
    DSP::setEQPreset(g_state.eq_preset, g_state.eq_custom);
    DSP::setFullSound(g_state.fullsound);
    DSP::setMono(g_state.mono);

    bootStage("starting tasks");
    xTaskCreatePinnedToCore(audioTask,    "AudioTask", AUDIO_TASK_STACK, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(keyboardTask, "KbdTask",   KBD_TASK_STACK,   nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,       "UITask",    UI_TASK_STACK,    nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(recorderTask, "RecTask",   REC_TASK_STACK,   nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(batteryTask,  "BatTask",   BAT_TASK_STACK,   nullptr, 1, nullptr, 0);

    delay(50);
    markStage("running");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

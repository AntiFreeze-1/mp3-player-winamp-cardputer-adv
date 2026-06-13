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
// Per-task breadcrumb slots. With several tasks running concurrently a single
// shared "last stage" string would race, so each task (and setup) gets its own
// slot. On a crash we print every slot, which shows exactly how far each task
// got — the one stuck mid-init, or the one that advanced just before the panic.
#define BOOT_MAGIC 0xB007C0DEu
enum BootSlot { SLOT_SETUP = 0, SLOT_AUDIO, SLOT_KBD, SLOT_UI, SLOT_REC, SLOT_BAT, SLOT_COUNT };
static const char* SLOT_NAMES[SLOT_COUNT] = { "setup", "audio", "kbd", "ui", "rec", "bat" };

RTC_NOINIT_ATTR static uint32_t g_boot_magic;
RTC_NOINIT_ATTR static char     g_boot_stages[SLOT_COUNT][28];
static int g_boot_line = 0;

static void markSlot(BootSlot slot, const char* stage) {
    strncpy(g_boot_stages[slot], stage, sizeof(g_boot_stages[slot]) - 1);
    g_boot_stages[slot][sizeof(g_boot_stages[slot]) - 1] = '\0';
    g_boot_magic = BOOT_MAGIC;
    Serial.printf("[BOOT][%s] %s\n", SLOT_NAMES[slot], stage);
}

static void bootStage(const char* stage) {
    markSlot(SLOT_SETUP, stage);
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
// PRECONDITION: the caller must already hold g_state_mutex. Every call site
// (handleKey in the UI task, and the EOF auto-advance in the audio task) runs
// inside the mutex, so this function must NOT re-acquire it — g_state_mutex is
// a plain (non-recursive) mutex, and a nested take would simply time out and
// silently skip the state update, leaving the UI showing the wrong track.
static void playPath(const char* path) {
    AudioEngine::play(path);
    strncpy(g_state.current_track_path, path,
            sizeof(g_state.current_track_path) - 1);
    g_state.current_track_path[sizeof(g_state.current_track_path)-1] = '\0';
    setTrackName(g_state, path);
    g_state.track_pos_ms = 0;
    g_state.playback     = PlaybackState::PLAYING;
    UIManager::loadAlbumArt(path, false);
}

// ── Helper: apply headphone routing ───────────────────────────────────────
static void updateAudioRouting(AppState& state) {
    bool hp = AudioEngine::headphonesIn();
    if (hp != state.headphones_in) state.headphones_in = hp;
}

// ── Helper: sleep timer check ─────────────────────────────────────────────
// PRECONDITION: caller must hold g_state_mutex (or be in setup, single-threaded).
static void checkSleepTimer(AppState& state) {
    if (state.sleep_deadline == 0) return;
    if (millis() < state.sleep_deadline) return;

    // Deadline passed — stop any active playback then enter deep sleep.
    if (state.playback == PlaybackState::PLAYING ||
        state.playback == PlaybackState::PAUSED) {
        AudioEngine::stop();
        state.playback = PlaybackState::STOPPED;
    }
    state.sleep_deadline = 0;
    NVSConfig::save(state, state.current_track_path, state.track_pos_ms);
    esp_deep_sleep_start();
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
                    // Resuming a track restored from NVS at boot. Capture the
                    // saved position before playPath() zeroes it, then seek
                    // once the decoder has the file open.
                    uint32_t resume_ms = state.track_pos_ms;
                    playPath(state.current_track_path);
                    if (resume_ms > 3000) {
                        AudioEngine::seekMs(resume_ms);
                        state.track_pos_ms = resume_ms;
                    }
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
    // All hardware init happens in setup(); tasks only run their loops.
    uint32_t beat = 0;
    for (;;) {
        if ((beat++ & 0xFF) == 0) {
            char m[28]; snprintf(m, sizeof(m), "loop %lu", (unsigned long)beat);
            markSlot(SLOT_AUDIO, m);
        }
        AudioEngine::loop();

        if (AudioEngine::isEOF()) {
            AudioEngine::clearEOF();
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                AppState& s = g_state;

                if (s.repeat == RepeatMode::ONE && s.current_track_path[0]) {
                    playPath(s.current_track_path);
                } else {
                    char next[128];
                    bool found = s.shuffle
                        ? g_lib.getRandomTrack(s.current_track_path, next, sizeof(next))
                        : g_lib.getAdjacentTrack(s.current_track_path, 1, next, sizeof(next));

                    if (found) {
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

        // Periodic 5s housekeeping: sync position, check sleep timer, persist position
        static uint32_t last_save = 0;
        if (millis() - last_save > 5000) {
            last_save = millis();
            char snap_path[128] = {0};
            uint32_t snap_pos = 0;
            bool should_save = false;
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                PlaybackState est = AudioEngine::state();
                g_state.playback  = est;
                // Don't clobber a boot-restored resume position while STOPPED:
                // positionMs() reads 0 until playback actually starts.
                if (est != PlaybackState::STOPPED)
                    g_state.track_pos_ms = AudioEngine::positionMs();
                checkSleepTimer(g_state);  // fires esp_deep_sleep_start() if deadline passed
                if (g_state.playback == PlaybackState::PLAYING &&
                    g_state.current_track_path[0]) {
                    strncpy(snap_path, g_state.current_track_path, sizeof(snap_path) - 1);
                    snap_pos   = g_state.track_pos_ms;
                    should_save = true;
                }
                xSemaphoreGive(g_state_mutex);
            }
            // Persist resume position outside the mutex to avoid holding it during NVS I/O
            if (should_save) NVSConfig::saveTrackPosition(snap_path, snap_pos);
        }

        // While playing, feed the decoder as fast as it needs: Audio::loop()
        // blocks on i2s_write until the DMA buffer has room, so it already
        // paces itself to realtime AND yields the CPU to other tasks. Adding a
        // fixed 10ms idle here would cap throughput well below realtime (one
        // ~26ms MP3 frame per call + 10ms idle ≈ 72% realtime) and cause
        // constant underruns — glitchy, dragging audio. Only sleep longer when
        // stopped, where loop() returns immediately and would otherwise spin.
        vTaskDelay(AudioEngine::state() == PlaybackState::PLAYING
                   ? 1 : pdMS_TO_TICKS(10));
    }
}

static void keyboardTask(void* arg) {
    uint32_t beat = 0;
    for (;;) {
        if ((beat++ & 0xFF) == 0) {
            char m[28]; snprintf(m, sizeof(m), "loop %lu", (unsigned long)beat);
            markSlot(SLOT_KBD, m);
        }
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
    uint32_t beat = 0;
    for (;;) {
        if ((beat++ & 0x3F) == 0) {
            char m[28]; snprintf(m, sizeof(m), "loop %lu", (unsigned long)beat);
            markSlot(SLOT_UI, m);
        }
        KeyEvent ev;
        while (xQueueReceive(g_key_queue, &ev, 0) == pdTRUE) {
            if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
                handleKey(ev, g_state);
                updateAudioRouting(g_state);
                xSemaphoreGive(g_state_mutex);
            }
        }

        // Snapshot state once (with mutex) so both draw paths see the same data.
        AppState snap;
        if (xSemaphoreTake(g_state_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            PlaybackState est = AudioEngine::state();
            g_state.playback  = est;
            // Preserve a boot-restored resume position until playback begins.
            if (est != PlaybackState::STOPPED)
                g_state.track_pos_ms = AudioEngine::positionMs();
            g_state.battery_pct  = BatteryMonitor::percent();
            g_state.charging     = BatteryMonitor::isCharging();
            snap = g_state;
            xSemaphoreGive(g_state_mutex);
        }

        if (snap.current_screen == Screen::VOICE_RECORDER) {
            UIManager::drawRecorder(snap,
                                    VoiceRecorder::elapsedMs(),
                                    VoiceRecorder::peakLevel());
        } else {
            UIManager::draw(snap, g_lib, g_playlist);
        }

        vTaskDelay(pdMS_TO_TICKS(33));
    }
}

static void batteryTask(void* arg) {
    markSlot(SLOT_BAT, "task");
    BatteryMonitor::task(arg);
}

static void recorderTask(void* arg) {
    markSlot(SLOT_REC, "task");
    VoiceRecorder::task(arg);
}

// ══════════════════════════════════════════════════════════════════════════
// Setup & Loop
// ══════════════════════════════════════════════════════════════════════════

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);
    // Release M5Unified's I2S driver claim so AudioEngine can install its own.
    // Without this, the Audio constructor's i2s_driver_install(I2S_NUM_0) would
    // conflict with the speaker driver M5Unified just installed.
    M5.Speaker.end();
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
        M5.Display.setTextSize(1);
        M5.Display.setCursor(2, 2);
        M5.Display.printf("CRASH  reset reason = %d\n", (int)rr);
        M5.Display.println("Last stage per task:");
        for (int i = 0; i < SLOT_COUNT; i++) {
            M5.Display.printf("  %-6s %s\n", SLOT_NAMES[i],
                              g_boot_stages[i][0] ? g_boot_stages[i] : "(none)");
        }
        M5.Display.println("\n(holding 8s)");
        delay(8000);
        M5.Display.fillScreen(TFT_BLACK);
    }
    g_boot_magic = 0;
    g_boot_line  = 0;
    for (int i = 0; i < SLOT_COUNT; i++) g_boot_stages[i][0] = '\0';

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
        // The decoder has nothing loaded yet, so the engine is really STOPPED.
        // Marking it PAUSED would send the next Enter into the resume() branch,
        // which no-ops (engine isn't paused) and the track would never start.
        // STOPPED makes Enter call playPath() and actually begin playback.
        g_state.playback       = PlaybackState::STOPPED;
    }

    // ── Initialise all hardware subsystems sequentially ───────────────────
    // The FreeRTOS scheduler is already running here (setup() is itself a
    // task), so there is no benefit to deferring init into the worker tasks —
    // and doing so caused init-time races across both cores. Initialising in
    // a defined order, single-threaded, also means the green boot log above
    // pinpoints exactly which subsystem fails if one ever does.
    bootStage("battery init");
    BatteryMonitor::begin();

    bootStage("recorder init");
    VoiceRecorder::begin();

    bootStage("keyboard init");
    TCA8418::begin();

    bootStage("audio init");
    AudioEngine::begin();

    bootStage("display init");
    UIManager::begin();

    // Apply persisted audio settings now that the engine exists
    AudioEngine::setVolume(g_state.volume);
    AudioEngine::setEQPreset(g_state.eq_preset, g_state.eq_custom);
    AudioEngine::setFullSound(g_state.fullsound);
    AudioEngine::setMono(g_state.mono);

    bootStage("starting tasks");
    xTaskCreatePinnedToCore(audioTask,    "AudioTask", AUDIO_TASK_STACK, nullptr, 5, nullptr, 1);
    xTaskCreatePinnedToCore(keyboardTask, "KbdTask",   KBD_TASK_STACK,   nullptr, 4, nullptr, 0);
    xTaskCreatePinnedToCore(uiTask,       "UITask",    UI_TASK_STACK,    nullptr, 3, nullptr, 0);
    xTaskCreatePinnedToCore(recorderTask, "RecTask",   REC_TASK_STACK,   nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(batteryTask,  "BatTask",   BAT_TASK_STACK,   nullptr, 1, nullptr, 0);

    markSlot(SLOT_SETUP, "running");
}

void loop() {
    vTaskDelay(pdMS_TO_TICKS(1000));
}

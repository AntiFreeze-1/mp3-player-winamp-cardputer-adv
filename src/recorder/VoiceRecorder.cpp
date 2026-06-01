#include "VoiceRecorder.h"
#include <Arduino.h>
#include <driver/i2s.h>
#include <string.h>
#include <stdio.h>
#include "../config.h"

SemaphoreHandle_t VoiceRecorder::sd_mutex = nullptr;
RecordState  VoiceRecorder::s_state    = RecordState::IDLE;
File         VoiceRecorder::s_file;
uint32_t     VoiceRecorder::s_start_ms = 0;
uint8_t      VoiceRecorder::s_peak     = 0;
RecordQuality VoiceRecorder::s_quality = RecordQuality::VOICE;
char         VoiceRecorder::s_filepath[256] = {0};

static const i2s_port_t REC_I2S_PORT = I2S_NUM_1;

void VoiceRecorder::begin() {
    if (!sd_mutex) sd_mutex = xSemaphoreCreateMutex();
}

void VoiceRecorder::buildFilePath(char* out, size_t max) {
    // Use millis() as pseudo-timestamp (no RTC)
    uint32_t t = millis();
    uint32_t ss = (t / 1000) % 60;
    uint32_t mm = (t / 60000) % 60;
    uint32_t hh = (t / 3600000) % 24;
    snprintf(out, max, "/Recordings/REC_%02lu%02lu%02lu.wav", (unsigned long)hh, (unsigned long)mm, (unsigned long)ss);
}

static void writeLE32(File& f, uint32_t v) {
    f.write((uint8_t)(v & 0xFF));
    f.write((uint8_t)((v >> 8)  & 0xFF));
    f.write((uint8_t)((v >> 16) & 0xFF));
    f.write((uint8_t)((v >> 24) & 0xFF));
}

static void writeLE16(File& f, uint16_t v) {
    f.write((uint8_t)(v & 0xFF));
    f.write((uint8_t)((v >> 8) & 0xFF));
}

void VoiceRecorder::writeWAVHeader(File& f, uint32_t sr, uint8_t ch, uint32_t num_samples) {
    uint32_t byte_rate    = sr * ch * 2;
    uint16_t block_align  = ch * 2;
    uint32_t data_bytes   = num_samples * ch * 2;

    f.write((const uint8_t*)"RIFF", 4);
    writeLE32(f, 36 + data_bytes);
    f.write((const uint8_t*)"WAVE", 4);
    f.write((const uint8_t*)"fmt ", 4);
    writeLE32(f, 16);               // PCM chunk size
    writeLE16(f, 1);                // PCM format
    writeLE16(f, ch);
    writeLE32(f, sr);
    writeLE32(f, byte_rate);
    writeLE16(f, block_align);
    writeLE16(f, 16);               // bits per sample
    f.write((const uint8_t*)"data", 4);
    writeLE32(f, data_bytes);
}

void VoiceRecorder::updateWAVHeader(File& f, uint32_t num_samples) {
    uint8_t ch = (s_quality == RecordQuality::HQ) ? 2 : 1;
    uint32_t data_bytes = num_samples * ch * 2;
    // Patch RIFF chunk size at offset 4
    f.seek(4);
    writeLE32(f, 36 + data_bytes);
    // Patch data chunk size at offset 40
    f.seek(40);
    writeLE32(f, data_bytes);
}

bool VoiceRecorder::startRecording(RecordQuality quality) {
    if (s_state != RecordState::IDLE) return false;
    s_quality = quality;

    uint32_t sr  = (quality == RecordQuality::HQ) ? 44100 : 16000;
    uint8_t  ch  = (quality == RecordQuality::HQ) ? 2 : 1;

    // Configure I2S for microphone input
    i2s_config_t i2s_cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate          = sr,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = (ch == 2) ? I2S_CHANNEL_FMT_RIGHT_LEFT : I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = 8,
        .dma_buf_len          = 512,
        .use_apll             = false,
        .tx_desc_auto_clear   = false,
        .fixed_mclk           = 0,
    };

    i2s_pin_config_t pin_cfg = {
        .bck_io_num   = PIN_I2S_BCLK,
        .ws_io_num    = PIN_I2S_LRCLK,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num  = PIN_I2S_DIN,
    };

    i2s_driver_install(REC_I2S_PORT, &i2s_cfg, 0, nullptr);
    i2s_set_pin(REC_I2S_PORT, &pin_cfg);

    // Acquire SD mutex and open file
    if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        i2s_driver_uninstall(REC_I2S_PORT);
        return false;
    }

    if (!SD.exists("/Recordings")) SD.mkdir("/Recordings");
    buildFilePath(s_filepath, sizeof(s_filepath));
    s_file = SD.open(s_filepath, FILE_WRITE);
    if (!s_file) {
        xSemaphoreGive(sd_mutex);
        i2s_driver_uninstall(REC_I2S_PORT);
        return false;
    }

    // Write placeholder WAV header (will be patched on stop)
    writeWAVHeader(s_file, sr, ch, 0);
    xSemaphoreGive(sd_mutex);

    s_start_ms = millis();
    s_state    = RecordState::RECORDING;
    return true;
}

void VoiceRecorder::stopRecording() {
    if (s_state != RecordState::RECORDING) return;
    s_state = RecordState::STOPPING;
}

uint32_t VoiceRecorder::elapsedMs() {
    if (s_state == RecordState::IDLE) return 0;
    return millis() - s_start_ms;
}

void VoiceRecorder::task(void* arg) {
    static int16_t buf[512];
    uint32_t total_samples = 0;

    for (;;) {
        if (s_state == RecordState::RECORDING) {
            size_t bytes_read = 0;
            i2s_read(REC_I2S_PORT, buf, sizeof(buf), &bytes_read, pdMS_TO_TICKS(100));

            if (bytes_read > 0) {
                // Compute peak level
                uint8_t peak = 0;
                int n = bytes_read / 2;
                for (int i = 0; i < n; i++) {
                    int16_t abs_v = buf[i] < 0 ? -buf[i] : buf[i];
                    if (abs_v > peak * 327) peak = (uint8_t)(abs_v / 327);
                }
                s_peak = peak > 100 ? 100 : peak;

                // Write to SD
                if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                    s_file.write((uint8_t*)buf, bytes_read);
                    xSemaphoreGive(sd_mutex);
                    total_samples += bytes_read / 2;
                }
            }
        } else if (s_state == RecordState::STOPPING) {
            // Patch WAV header
            if (xSemaphoreTake(sd_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
                updateWAVHeader(s_file, total_samples);
                s_file.close();
                xSemaphoreGive(sd_mutex);
            }
            i2s_driver_uninstall(REC_I2S_PORT);
            total_samples = 0;
            s_state = RecordState::IDLE;
        } else {
            vTaskDelay(pdMS_TO_TICKS(50));
        }
    }
}

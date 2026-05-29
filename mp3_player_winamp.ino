/*
 * M5Mp3 Winamp-style MP3 Player for M5Stack Cardputer-Adv
 * 
 * ============================================================================
 * CREDITS
 * ============================================================================
 * 
 * This project is adapted from the original M5Mp3 by VolosR:
 * - Original Project: https://github.com/VolosR/M5Mp3
 * - Original Author: VolosR
 * - License: Same as original M5Mp3 project
 * 
 * Many thanks to VolosR for creating the original Winamp-style interface
 * and audio playback implementation!
 * 
 * ============================================================================
 * CHANGES FROM ORIGINAL
 * ============================================================================
 * 
 * - Replaced ESP32-audioI2S with ESP8266Audio
 * - Uses M5Cardputer.Speaker (ES8311) instead of I2S
 * - Custom AudioOutput class for M5Cardputer
 * - Removed ESP32Time dependency (uses millis() instead)
 * - Added ES8311 audio codec support
 * 
 * ============================================================================
 * LIBRARIES REQUIRED
 * ============================================================================
 * 
 * - ESP8266Audio (https://github.com/earlephilhower/ESP8266Audio)
 * - M5Cardputer (included with M5Stack board support)
 */

#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <M5Cardputer.h>

// ESP8266Audio libraries
#include <AudioOutput.h>
#include <AudioFileSourceSD.h>
#include <AudioFileSourceID3.h>
#include <AudioGeneratorMP3.h>

#include "font.h"

M5Canvas sprite(&M5Cardputer.Display);
M5Canvas spr(&M5Cardputer.Display);

// microSD card pins
#define SD_SCK 40
#define SD_MISO 39
#define SD_MOSI 14
#define SD_CS 12

// Custom AudioOutput for M5Cardputer.Speaker
class AudioOutputM5CardputerSpeaker : public AudioOutput {
public:
    AudioOutputM5CardputerSpeaker(m5::Speaker_Class* m5sound) {
        _m5sound = m5sound;
    }
    
    virtual ~AudioOutputM5CardputerSpeaker(void) {};
    
    virtual bool begin(void) override { 
        return true; 
    }
    
    virtual bool ConsumeSample(int16_t sample[2]) override {
        if (_tri_buffer_index < tri_buf_size) {
            // Convert stereo to mono (average L+R) to prevent overflow
            int32_t sum = (int32_t)sample[0] + (int32_t)sample[1];
            int16_t mono = (int16_t)(sum / 2);
            _tri_buffer[_tri_index][_tri_buffer_index] = mono;
            _tri_buffer_index++;
            return true;
        }
        flush();
        return false;
    }
    
    virtual void flush(void) override {
        if (_tri_buffer_index > 0) {
            // Wait for previous playback to finish (non-blocking check)
            uint32_t waitCount = 0;
            while (_m5sound->isPlaying() && waitCount < 1000) {
                vTaskDelay(pdMS_TO_TICKS(1));
                waitCount++;
            }
            // playRaw(buffer, size, sampleRate, stereo=false for mono)
            if (_tri_buffer_index > 0) {
                _m5sound->playRaw(_tri_buffer[_tri_index], _tri_buffer_index, hertz, false);
            }
            _tri_index = _tri_index < 2 ? _tri_index + 1 : 0;
            _tri_buffer_index = 0;
        }
    }
    
    virtual bool stop(void) override {
        flush();
        // M5Cardputer.Speaker doesn't have stop() method, just wait for playback to finish
        while (_m5sound->isPlaying()) {
            vTaskDelay(1 / portTICK_PERIOD_MS);
        }
        return true;
    }
    
    const int16_t* getBuffer(void) const { 
        return _tri_buffer[(_tri_index + 2) % 3]; 
    }

protected:
    m5::Speaker_Class* _m5sound;
    static constexpr size_t tri_buf_size = 1536;
    int16_t _tri_buffer[3][tri_buf_size];
    size_t _tri_buffer_index = 0;
    size_t _tri_index = 0;
};

// Global variables
unsigned short grays[18];
unsigned short gray;
unsigned short light;
int n = 0;
int m = 0;
int volume = 10;  // 0-21 (mapped to 0-255 for M5.Speaker)
int bri = 0;
int brightness[5] = {50, 100, 150, 200, 250};
bool isPlaying = true;
bool stoped = false;
bool nextS = 0;
bool volUp = 0;
int g[14] = {0}; 
int graphSpeed = 0;
int textPos = 60;
int sliderPos = 0;

// Task handle for audio task
TaskHandle_t handleAudioTask = NULL;

// Simple time tracking (replaces ESP32Time)
unsigned long trackStartTime = 0;
unsigned long trackElapsedSeconds = 0;

#define MAX_FILES 100

// Array to store file names
String audioFiles[MAX_FILES];
int fileCount = 0;

// ESP8266Audio objects
AudioFileSourceSD* audioFile = nullptr;
AudioFileSourceID3* audioId3 = nullptr;
AudioGeneratorMP3* audioMp3 = nullptr;
AudioOutputM5CardputerSpeaker* audioOut = nullptr;

void resetClock() {
    trackStartTime = millis();
    trackElapsedSeconds = 0;
}

String getTimeString() {
    unsigned long elapsed = trackElapsedSeconds;
    if (trackStartTime > 0 && !stoped) {
        elapsed = (millis() - trackStartTime) / 1000;
    }
    int minutes = elapsed / 60;
    int seconds = elapsed % 60;
    char timeStr[6];
    snprintf(timeStr, sizeof(timeStr), "%02d:%02d", minutes, seconds);
    return String(timeStr);
}

void setup() {
    Serial.begin(115200);
    Serial.println("M5Mp3 Winamp Player for Cardputer-Adv");
    delay(500);
    
    resetClock();

    // Initialize M5Cardputer
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(brightness[bri]);
    
    sprite.createSprite(240, 135);
    spr.createSprite(86, 16);

    // Initialize SD card
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI);
    if (!SD.begin(SD_CS)) {
        Serial.println(F("ERROR: SD Mount Failed!"));
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(RED);
        M5Cardputer.Display.setCursor(10, 60);
        M5Cardputer.Display.println("SD Card Error!");
        while(1) delay(1000);
    }
    
    listFiles(SD, "/", MAX_FILES);
    
    if (fileCount == 0) {
        Serial.println("No MP3 files found!");
        M5Cardputer.Display.fillScreen(BLACK);
        M5Cardputer.Display.setTextColor(YELLOW);
        M5Cardputer.Display.setCursor(10, 60);
        M5Cardputer.Display.println("No MP3 files!");
        while(1) delay(1000);
    }

    // Initialize audio output
    M5Cardputer.Speaker.begin();
    uint8_t m5Volume = map(volume, 0, 21, 0, 255);
    M5Cardputer.Speaker.setVolume(m5Volume);
    audioOut = new AudioOutputM5CardputerSpeaker(&M5Cardputer.Speaker);
    audioOut->SetGain((float)volume / 21.0f);  // 0-21 -> 0.0-1.0
    
    // Initialize first file
    if (fileCount > 0) {
        audioFile = new AudioFileSourceSD(audioFiles[n].c_str());
        audioId3 = new AudioFileSourceID3(audioFile);
        audioMp3 = new AudioGeneratorMP3();
        audioMp3->begin(audioId3, audioOut);
    }

    // Initialize grayscale colors
    int co = 214;
    for (int i = 0; i < 18; i++) {
        grays[i] = M5Cardputer.Display.color565(co, co, co + 40);
        co = co - 13;
    }

    // Create tasks
    xTaskCreatePinnedToCore(Task_TFT, "Task_TFT", 20480, NULL, 2, NULL, 0);            // Core 0
    xTaskCreatePinnedToCore(Task_Audio, "Task_Audio", 10240, NULL, 3, &handleAudioTask, 1); // Core 1
    
    Serial.println("Setup complete!");
}

void loop() {
    // Empty - everything runs in tasks
}

void draw() {
    int drawN = n;  // snapshot: Task_Audio can modify n concurrently on Core 1
    if (graphSpeed == 0) {
        gray = grays[15];
        light = grays[11];
        sprite.fillRect(0, 0, 240, 135, gray);
        sprite.fillRect(4, 8, 130, 122, BLACK);

        sprite.fillRect(129, 8, 5, 122, 0x0841);

        sliderPos = map(drawN, 0, fileCount, 8, 110);
        sprite.fillRect(129, sliderPos, 5, 20, grays[2]);
        sprite.fillRect(131, sliderPos + 4, 1, 12, grays[16]);

        sprite.fillRect(4, 2, 50, 2, ORANGE);
        sprite.fillRect(84, 2, 50, 2, ORANGE);
        sprite.fillRect(190, 2, 45, 2, ORANGE);
        sprite.fillRect(190, 6, 45, 3, grays[4]);
        sprite.drawFastVLine(3, 9, 120, light);
        sprite.drawFastVLine(134, 9, 120, light);
        sprite.drawFastHLine(3, 129, 130, light);
       
        sprite.drawFastHLine(0, 0, 240, light);
        sprite.drawFastHLine(0, 134, 240, light);

        sprite.fillRect(139, 0, 3, 135, BLACK);
        sprite.fillRect(148, 14, 86, 42, BLACK);
        sprite.fillRect(148, 59, 86, 16, BLACK);

        sprite.fillTriangle(162, 18, 162, 26, 168, 22, GREEN);
        sprite.fillRect(162, 30, 6, 6, RED);
        
        sprite.drawFastVLine(143, 0, 135, light);
        sprite.drawFastVLine(238, 0, 135, light);
        sprite.drawFastVLine(138, 0, 135, light);
        sprite.drawFastVLine(148, 14, 42, light);
        sprite.drawFastHLine(148, 14, 86, light);

        //buttons
        for (int i = 0; i < 4; i++)
            sprite.fillRoundRect(148 + (i * 22), 94, 18, 18, 3, grays[4]);

        //button icons
        sprite.fillRect(220, 104, 8, 2, grays[13]);
        sprite.fillRect(220, 108, 8, 2, grays[13]);
        sprite.fillTriangle(228, 102, 228, 106, 231, 105, grays[13]);
        sprite.fillTriangle(220, 106, 220, 110, 217, 109, grays[13]);

        if (!stoped) {
            sprite.fillRect(152, 104, 3, 6, grays[13]);
            sprite.fillRect(157, 104, 3, 6, grays[13]);
        } else {
            sprite.fillTriangle(156, 102, 156, 110, 160, 106, grays[13]);
        }
        
        //volume bar
        sprite.fillRoundRect(172, 82, 60, 3, 2, YELLOW);
        sprite.fillRoundRect(155 + ((volume / 5) * 17), 80, 10, 8, 2, grays[2]);
        sprite.fillRoundRect(157 + ((volume / 5) * 17), 82, 6, 4, 2, grays[10]);
       
        // brightness
        sprite.fillRoundRect(172, 124, 30, 3, 2, MAGENTA);
        sprite.fillRoundRect(172 + (bri * 5), 122, 10, 8, 2, grays[2]);
        sprite.fillRoundRect(174 + (bri * 5), 124, 6, 4, 2, grays[10]);

        //BATTERY
        sprite.drawRect(206, 119, 28, 12, GREEN);
        sprite.fillRect(234, 122, 3, 6, GREEN);

        //graph
        for (int i = 0; i < 14; i++) { 
            if (!stoped)  
                g[i] = random(1, 5);
            for (int j = 0; j < g[i]; j++)
                sprite.fillRect(172 + (i * 4), 50 - j * 3, 3, 2, grays[4]);
        }
        
        sprite.setTextFont(0);
        sprite.setTextDatum(0);

        if (drawN < 5) {
            for (int i = 0; i < 10; i++) {
                if (i == drawN) sprite.setTextColor(WHITE, BLACK);
                else sprite.setTextColor(GREEN, BLACK);
                if (i < fileCount)
                    sprite.drawString(audioFiles[i].substring(1, 20), 8, 10 + (i * 12));
            }
        }

        int yos = 0;
        if (drawN >= 5) {
            for (int i = drawN - 5; i < drawN - 5 + 10; i++) {
                if (i == drawN) sprite.setTextColor(WHITE, BLACK);
                else sprite.setTextColor(GREEN, BLACK);
                if (i >= 0 && i < fileCount)
                    sprite.drawString(audioFiles[i].substring(1, 20), 8, 10 + (yos * 12));
                yos++;
            }
        }

        sprite.setTextColor(grays[1], gray);
        sprite.drawString("WINAMP", 150, 4);
        sprite.setTextColor(grays[2], gray);
        sprite.drawString("LIST", 58, 0);
        sprite.setTextColor(grays[4], gray);
        sprite.drawString("VOL", 150, 80);
        sprite.drawString("LIG", 150, 122);
       
        if (isPlaying) {
            sprite.setTextColor(grays[8], BLACK);
            sprite.drawString("P", 152, 18);
            sprite.drawString("L", 152, 27);
            sprite.drawString("A", 152, 36);
            sprite.drawString("Y", 152, 45);
        } else {
            sprite.setTextColor(grays[8], BLACK);
            sprite.drawString("S", 152, 18);
            sprite.drawString("T", 152, 27);
            sprite.drawString("O", 152, 36);
            sprite.drawString("P", 152, 45);
        }

        sprite.setTextColor(GREEN, BLACK); 
        sprite.setFont(&DSEG7_Classic_Mini_Regular_16);
        if (!stoped) {
            // Update elapsed time
            if (trackStartTime > 0) {
                trackElapsedSeconds = (millis() - trackStartTime) / 1000;
            }
            sprite.drawString(getTimeString(), 172, 18);
        }
        sprite.setTextFont(0);

        // Battery percentage
        int percent = 0;
        float batteryVoltage = M5Cardputer.Power.getBatteryVoltage();
        if (batteryVoltage > 4.2)
            percent = 100;
        else if (batteryVoltage < 3.0)
            percent = 1;
        else
            percent = map((int)(batteryVoltage * 100), 300, 420, 1, 100);
       
        sprite.setTextDatum(3);
        sprite.drawString(String(percent) + "%", 220, 121);

        sprite.setTextColor(BLACK, grays[4]);
        sprite.drawString("B", 220, 96); 
        sprite.drawString("N", 198, 96); 
        sprite.drawString("P", 176, 96); 
        sprite.drawString("A", 154, 96); 

        sprite.setTextColor(BLACK, grays[5]);
        sprite.drawString(">>", 202, 103); 
        sprite.drawString("<<", 180, 103); 
        
        spr.fillSprite(BLACK);
        spr.setTextColor(GREEN, BLACK);
        if (!stoped && drawN < fileCount)
            spr.drawString(audioFiles[drawN].substring(1, audioFiles[drawN].length()), textPos, 4);
        textPos = textPos - 2; 
        if (textPos < -300) textPos = 90;
        spr.pushSprite(&sprite, 148, 59);
        
        sprite.pushSprite(0, 0);
    }
    graphSpeed++; 
    if (graphSpeed == 4) graphSpeed = 0;
}

void Task_TFT(void *pvParameters) {
    while (1) {
        M5Cardputer.update();
        
        // Check for key press events
        if (M5Cardputer.Keyboard.isChange()) {
            if (M5Cardputer.Keyboard.isKeyPressed('a')) {
                isPlaying = !isPlaying;
                stoped = !stoped;
            }

            if (M5Cardputer.Keyboard.isKeyPressed('v')) {
                volUp = true;
                volume = volume + 5;
                if (volume > 20) volume = 5;
            }

            if (M5Cardputer.Keyboard.isKeyPressed('l')) {
                bri++; 
                if (bri == 5) bri = 0;
                M5Cardputer.Display.setBrightness(brightness[bri]);
            }
            
            if (M5Cardputer.Keyboard.isKeyPressed('n')) {
                resetClock();
                stoped = false;
                isPlaying = true;
                textPos = 90;
                n++;
                if (n >= fileCount) n = 0;
                nextS = 1;
            }

            if (M5Cardputer.Keyboard.isKeyPressed('p')) {
                resetClock();
                stoped = false;
                isPlaying = true;
                textPos = 90;
                n--;
                if (n < 0) n = fileCount - 1;
                nextS = 1;
            }

            if (M5Cardputer.Keyboard.isKeyPressed(';')) {
                n--;
                if (n < 0)
                    n = fileCount - 1;
            }

            if (M5Cardputer.Keyboard.isKeyPressed('.')) {
                n++;
                if (n >= fileCount)
                    n = 0;
            }

            if (M5Cardputer.Keyboard.isKeyPressed(KEY_ENTER)) {
                resetClock();
                stoped = false;
                isPlaying = false;
                textPos = 90;
                nextS = 1;
            } 

            if (M5Cardputer.Keyboard.isKeyPressed('b')) {
                resetClock();
                stoped = false;
                isPlaying = true;
                textPos = 90;
                n = random(0, fileCount);
                nextS = 1;
            }
        }
        draw();  
        vTaskDelay(40 / portTICK_PERIOD_MS);
    }
}

void Task_Audio(void *pvParameters) {
    while (1) {
        if (volUp) {
            if (audioOut) {
                audioOut->SetGain((float)volume / 21.0f);
            }
            uint8_t m5Volume = map(volume, 0, 21, 0, 255);
            M5Cardputer.Speaker.setVolume(m5Volume);
            volUp = 0;
        }

        if (nextS) {
            // Stop current playback
            if (audioMp3 && audioMp3->isRunning()) {
                audioMp3->stop();
            }
            if (audioMp3) {
                delete audioMp3;
                audioMp3 = nullptr;
            }
            if (audioId3) {
                delete audioId3;
                audioId3 = nullptr;
            }
            if (audioFile) {
                delete audioFile;
                audioFile = nullptr;
            }
            
            // Load new file
            if (n < fileCount) {
                audioFile = new AudioFileSourceSD(audioFiles[n].c_str());
                audioId3 = new AudioFileSourceID3(audioFile);
                audioMp3 = new AudioGeneratorMP3();
                audioMp3->begin(audioId3, audioOut);
            }
            
            isPlaying = 1;
            nextS = 0;
        }

        if (isPlaying && audioMp3) {
            if (!stoped) {
                if (audioMp3->isRunning()) {
                    if (!audioMp3->loop()) {
                        // End of file - play next
                        resetClock();
                        n++;
                        if (n >= fileCount) n = 0;
                        nextS = 1;
                    }
                } else {
                    // Not running - start next
                    resetClock();
                    n++;
                    if (n >= fileCount) n = 0;
                    nextS = 1;
                }
            }
        }
        
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void listFiles(fs::FS &fs, const char *dirname, uint8_t levels) {
    Serial.printf("Listing directory: %s\n", dirname);

    File root = fs.open(dirname);
    if (!root) {
        Serial.println("Failed to open directory");
        return;
    }
    if (!root.isDirectory()) {
        Serial.println("Not a directory");
        return;
    }

    File file = root.openNextFile();
    while (file && fileCount < MAX_FILES) {
        if (file.isDirectory()) {
            Serial.print("DIR : ");
            Serial.println(file.name());
            if (levels) {
                String subdirPath = String(dirname);
                if (!subdirPath.endsWith("/")) subdirPath += "/";
                subdirPath += file.name();
                listFiles(fs, subdirPath.c_str(), levels - 1);
            }
        } else {
            String filename = String(file.name());
            filename.toLowerCase();
            if (filename.endsWith(".mp3")) {
                String fullPath = String(dirname);
                if (!fullPath.endsWith("/")) fullPath += "/";
                fullPath += file.name();
                if (!fullPath.startsWith("/")) fullPath = "/" + fullPath;
                Serial.print("FILE: ");
                Serial.println(fullPath);
                audioFiles[fileCount] = fullPath;
                fileCount++;
            }
        }
        file = root.openNextFile();
    }
    
    Serial.printf("Found %d MP3 files\n", fileCount);
}


#include <Arduino.h>

#include "Config.h"
#include "VideoPlayer.h"

// ─────────────────────────────────────────────────────────────────────────────
// ESP32-TFT-Video-Player
//
// Plays a TVID binary video from LittleFS on a 1.8-inch ST7735 TFT LCD.
//
// Quick start:
//   1. Convert your MP4:
//        python convert.py -i video.mp4 --width 160 --height 128
//   2. Upload output/video.bin to LittleFS using the LittleFS upload tool.
//   3. Flash this sketch.
//
// See Config.h to adjust resolution, loop behavior, and buffer size.
// See TFT_Config.h for pin assignments.
// ─────────────────────────────────────────────────────────────────────────────

VideoPlayer player;

void setup()
{
    Serial.begin(SERIAL_BAUD);
    delay(150); // Allow USB serial to enumerate before printing

    Serial.println("\n[ESP32-TFT-Video-Player] Starting...");

    if (!player.begin())
    {
        Serial.println("[Main] Initialization failed.");
        Serial.println("[Main] Check:");
        Serial.println("[Main]   1. TFT wiring matches TFT_Config.h");
        Serial.println("[Main]   2. video.bin is present in LittleFS (/video.bin)");
        Serial.println("[Main]   3. video.bin was converted at 160x128 (landscape)");
        // Halt — nothing useful can happen without a working player.
        while (true)
        {
            delay(1000);
        }
    }
}

void loop()
{
    // update() is non-blocking. It returns immediately if it is not yet time
    // for the next frame, keeping the loop free for future features (buttons,
    // OTA, audio, etc.).
    player.update();
}

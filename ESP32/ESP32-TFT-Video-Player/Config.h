#ifndef CONFIG_H
#define CONFIG_H

// ─────────────────────────────────────────────────────────────────────────────
// Serial
// ─────────────────────────────────────────────────────────────────────────────

// Serial output speed.
#define SERIAL_BAUD 115200

// ─────────────────────────────────────────────────────────────────────────────
// Display Resolution
//
// TFT_WIDTH  = logical width  after rotation (landscape = 160)
// TFT_HEIGHT = logical height after rotation (landscape = 128)
//
// The ST7735 1.8-inch panel is physically 128 × 160 pins, but with
// TFT_ROTATION = 1 (landscape) the logical framebuffer is 160 × 128.
// The Python converter must use the same values:
//   python convert.py --width 160 --height 128
// ─────────────────────────────────────────────────────────────────────────────
#define TFT_WIDTH  160
#define TFT_HEIGHT 128

// ─────────────────────────────────────────────────────────────────────────────
// Video Format
// ─────────────────────────────────────────────────────────────────────────────

// Binary format version number written by the Python converter.
// Increment this whenever the header layout changes.
#define VIDEO_FORMAT_VERSION 1

// Path inside LittleFS where the binary video is stored.
#define VIDEO_FILE_PATH "/video.bin"

// ─────────────────────────────────────────────────────────────────────────────
// Playback
// ─────────────────────────────────────────────────────────────────────────────

// true  → restart from frame 0 when the last frame is reached.
// false → stop after the last frame and print a message.
#define LOOP_PLAYBACK true

// Maximum number of consecutive JPEG decode failures before playback stops.
// Prevents an infinite loop when the file is corrupt.
#define MAX_DECODE_FAILURES 10

// ─────────────────────────────────────────────────────────────────────────────
// LittleFS
// ─────────────────────────────────────────────────────────────────────────────

// Set to true only during initial development when you need LittleFS to
// auto-format on mount failure. Leave false in production to avoid
// accidentally wiping the filesystem.
#define LITTLEFS_AUTO_FORMAT_IF_MOUNT_FAILED false

// ─────────────────────────────────────────────────────────────────────────────
// JPEG Frame Buffer
// ─────────────────────────────────────────────────────────────────────────────

// Size in bytes of the heap buffer used to hold one compressed JPEG frame.
// A 160×128 frame at quality=75 is typically 4–12 KB.
// 32 KB is generous headroom for higher quality settings.
// Do not increase beyond ~50 KB on ESP32 without PSRAM.
#define JPEG_BUFFER_SIZE (32 * 1024)

// ─────────────────────────────────────────────────────────────────────────────
// Debug
// ─────────────────────────────────────────────────────────────────────────────

// Set to true to print a frame progress line every 30 frames during playback.
#define VIDEO_TIMING_DEBUG false

#endif // CONFIG_H

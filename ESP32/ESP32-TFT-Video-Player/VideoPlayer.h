#ifndef VIDEO_PLAYER_H
#define VIDEO_PLAYER_H

// ─────────────────────────────────────────────────────────────────────────────
// VideoPlayer
//
// Streams one JPEG frame at a time from a TVID binary stored in LittleFS,
// decodes it with JPEGDEC, and renders it on a TFT_eSPI display.
//
// Usage:
//   VideoPlayer player;
//
//   void setup() { player.begin(); }
//   void loop()  { player.update(); }
//
// Only one VideoPlayer instance may exist at a time. The JPEGDEC library
// uses a C-style callback with no user-data parameter; a static pointer
// (activeInstance_) is used to route callbacks to the correct object.
// Creating a second instance while the first is decoding is undefined.
// ─────────────────────────────────────────────────────────────────────────────

#include <Arduino.h>
#include <LittleFS.h>
#include <JPEGDEC.h>
#include <TFT_eSPI.h>

class VideoPlayer
{
public:
    VideoPlayer();
    ~VideoPlayer();

    // Initializes TFT, mounts LittleFS, allocates the JPEG buffer, opens
    // video.bin, and validates the header. Returns true on success.
    bool begin();

    // Call from loop(). Displays the next frame if enough time has elapsed
    // since the previous one. Non-blocking.
    void update();

    // ── Status accessors ──────────────────────────────────────────────────

    // Returns true after a successful begin() and while playback is running.
    bool isPlaying() const { return initialized_; }

    // Returns the current frame index (0-based).
    uint32_t currentFrame() const { return currentFrame_; }

    // Returns the total number of frames reported in the header.
    uint32_t totalFrames() const { return totalFrames_; }

private:
    // ── JPEGDEC callback (must be static; routes to activeInstance_) ──────
    static int jpegDrawCallback(JPEGDRAW* draw);
    int drawJpegBlock(JPEGDRAW* draw);

    // ── Internal helpers ──────────────────────────────────────────────────
    bool openVideoFile();
    bool readHeader();
    bool readAndDisplayNextFrame();
    bool decodeAndDrawFrame();
    void restartPlayback();

    // ── State ─────────────────────────────────────────────────────────────
    bool initialized_;

    TFT_eSPI tft_;
    JPEGDEC  jpeg_;
    File     videoFile_;

    uint16_t width_;
    uint16_t height_;
    uint16_t fps_;
    uint32_t totalFrames_;
    uint32_t currentFrame_;

    uint8_t* jpegBuffer_;   // Heap buffer reused every frame (size = JPEG_BUFFER_SIZE)
    uint32_t jpegSize_;     // Byte length of the JPEG data currently in jpegBuffer_

    uint32_t frameDelayMs_;    // Target inter-frame delay = 1000 / fps
    uint32_t lastFrameTickMs_; // millis() timestamp of the last rendered frame
    uint32_t decodeFailures_;  // Consecutive JPEG decode failures

    // Singleton pointer used by the static JPEGDEC callback.
    // See class-level note about single-instance limitation.
    static VideoPlayer* activeInstance_;
};

#endif // VIDEO_PLAYER_H

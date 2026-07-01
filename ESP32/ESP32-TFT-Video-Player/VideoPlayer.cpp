#include "VideoPlayer.h"

#include <string.h>   // memcmp — Arduino-compatible C header

#include "Config.h"
#include "TFT_Config.h"
#include "VideoTypes.h"

// ─────────────────────────────────────────────────────────────────────────────
// Static member definition
// ─────────────────────────────────────────────────────────────────────────────

VideoPlayer* VideoPlayer::activeInstance_ = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

VideoPlayer::VideoPlayer()
    : initialized_(false),
      width_(0),
      height_(0),
      fps_(0),
      totalFrames_(0),
      currentFrame_(0),
      jpegBuffer_(nullptr),
      jpegSize_(0),
      frameDelayMs_(0),
      lastFrameTickMs_(0),
      decodeFailures_(0)
{
}

VideoPlayer::~VideoPlayer()
{
    if (videoFile_)
    {
        videoFile_.close();
    }

    if (jpegBuffer_ != nullptr)
    {
        free(jpegBuffer_);
        jpegBuffer_ = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

bool VideoPlayer::begin()
{
    // ── TFT ──────────────────────────────────────────────────────────────────
    tft_.init();
    tft_.setRotation(TFT_ROTATION);
    tft_.fillScreen(TFT_BACKGROUND_COLOR);
    tft_.setSwapBytes(true); // JPEGDEC outputs RGB565 big-endian; TFT_eSPI needs little-endian

    // ── LittleFS ─────────────────────────────────────────────────────────────
    if (!LittleFS.begin(LITTLEFS_AUTO_FORMAT_IF_MOUNT_FAILED))
    {
        Serial.println("[VideoPlayer] LittleFS mount failed.");
        return false;
    }

    // ── JPEG frame buffer ─────────────────────────────────────────────────────
    // Allocated once here and reused for every frame to avoid heap fragmentation.
    jpegBuffer_ = static_cast<uint8_t*>(malloc(JPEG_BUFFER_SIZE));
    if (jpegBuffer_ == nullptr)
    {
        Serial.println("[VideoPlayer] Failed to allocate JPEG frame buffer.");
        return false;
    }

    // ── Video file ────────────────────────────────────────────────────────────
    if (!openVideoFile())
    {
        return false;
    }

    initialized_ = true;
    lastFrameTickMs_ = millis();

    Serial.println("[VideoPlayer] Ready.");
    return true;
}

void VideoPlayer::update()
{
    if (!initialized_)
    {
        return;
    }

    const uint32_t now = millis();
    const uint32_t elapsed = now - lastFrameTickMs_;

    // Not yet time for the next frame.
    if (elapsed < frameDelayMs_)
    {
        return;
    }

    // ── Frame timer resync ────────────────────────────────────────────────────
    // If we've fallen more than one frame behind (e.g. first call after begin(),
    // or a slow decode), resync the timer instead of trying to catch up.
    // This prevents a burst of frames being rendered back-to-back after a delay.
    if (elapsed >= frameDelayMs_ * 2)
    {
        lastFrameTickMs_ = now;
    }
    else
    {
        // Advance by exactly one frame period to maintain long-term accuracy.
        lastFrameTickMs_ += frameDelayMs_;
    }

    // ── Read and display the next frame ───────────────────────────────────────
    if (!readAndDisplayNextFrame())
    {
        // readAndDisplayNextFrame() returns false only on EOF or unrecoverable
        // file read error. Decode failures are handled internally (frame skipped).
        if (LOOP_PLAYBACK)
        {
            restartPlayback();
        }
        else
        {
            initialized_ = false;
            Serial.println("[VideoPlayer] Playback finished.");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — File Management
// ─────────────────────────────────────────────────────────────────────────────

bool VideoPlayer::openVideoFile()
{
    if (videoFile_)
    {
        videoFile_.close();
    }

    videoFile_ = LittleFS.open(VIDEO_FILE_PATH, "r");
    if (!videoFile_)
    {
        Serial.println("[VideoPlayer] Could not open " VIDEO_FILE_PATH);
        return false;
    }

    if (!readHeader())
    {
        videoFile_.close();
        return false;
    }

    currentFrame_ = 0;
    return true;
}

bool VideoPlayer::readHeader()
{
    // Minimum sane file: header + at least one frame size field.
    if (videoFile_.size() < static_cast<uint32_t>(TVID_HEADER_SIZE + TVID_FRAME_SIZE_FIELD))
    {
        Serial.println("[VideoPlayer] video.bin is too small to contain a valid header.");
        return false;
    }

    // Read the packed header directly into the struct.
    // VideoFileHeader has __attribute__((packed)) so sizeof() matches the
    // Python struct.pack("<4sHHHHI") layout exactly — no padding bytes.
    VideoFileHeader header = {};
    const size_t bytesRead = videoFile_.read(reinterpret_cast<uint8_t*>(&header), sizeof(header));

    if (bytesRead != sizeof(header))
    {
        Serial.println("[VideoPlayer] Failed to read complete header.");
        return false;
    }

    // ── Magic number ──────────────────────────────────────────────────────────
    if (memcmp(header.magic, TVID_MAGIC, TVID_MAGIC_LEN) != 0)
    {
        Serial.println("[VideoPlayer] Invalid magic number. Is this a TVID file?");
        return false;
    }

    // ── Format version ────────────────────────────────────────────────────────
    if (header.version != VIDEO_FORMAT_VERSION)
    {
        Serial.printf("[VideoPlayer] Unsupported format version: %u (expected %u)\n",
                      header.version,
                      static_cast<uint16_t>(VIDEO_FORMAT_VERSION));
        return false;
    }

    // ── Populate player state ─────────────────────────────────────────────────
    width_       = header.width;
    height_      = header.height;
    fps_         = (header.fps == 0) ? 1 : header.fps;
    totalFrames_ = header.totalFrames;

    frameDelayMs_ = 1000UL / fps_;
    if (frameDelayMs_ == 0)
    {
        frameDelayMs_ = 1;
    }

    Serial.printf("[VideoPlayer] %ux%u @ %u FPS — %lu frames\n",
                  width_,
                  height_,
                  fps_,
                  static_cast<unsigned long>(totalFrames_));

    // ── Resolution mismatch warning ───────────────────────────────────────────
    // The video must be converted at exactly TFT_WIDTH × TFT_HEIGHT (160 × 128
    // in landscape). A mismatch won't crash the player but will cause clipping
    // or blank areas. The Python converter defaults match these constants.
    if (width_ != TFT_WIDTH || height_ != TFT_HEIGHT)
    {
        Serial.printf("[VideoPlayer] WARNING: video resolution %ux%u != display %ux%u. "
                      "Re-run the converter with --width %d --height %d\n",
                      width_,
                      height_,
                      static_cast<uint16_t>(TFT_WIDTH),
                      static_cast<uint16_t>(TFT_HEIGHT),
                      TFT_WIDTH,
                      TFT_HEIGHT);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — Playback
// ─────────────────────────────────────────────────────────────────────────────

bool VideoPlayer::readAndDisplayNextFrame()
{
    if (!videoFile_)
    {
        return false;
    }

    // End of file detection by frame count (preferred — exact).
    if (totalFrames_ > 0 && currentFrame_ >= totalFrames_)
    {
        return false;
    }

    // ── Read frame size prefix ────────────────────────────────────────────────
    uint32_t frameSize = 0;
    const size_t sizeBytesRead = videoFile_.read(reinterpret_cast<uint8_t*>(&frameSize), sizeof(frameSize));

    if (sizeBytesRead != sizeof(frameSize))
    {
        // Short read = end of file (or file corruption).
        return false;
    }

    // ── Sanity-check frame size ───────────────────────────────────────────────
    if (frameSize == 0 || frameSize > JPEG_BUFFER_SIZE)
    {
        Serial.printf("[VideoPlayer] Frame %lu: invalid size %lu (buffer = %u). Stopping.\n",
                      static_cast<unsigned long>(currentFrame_),
                      static_cast<unsigned long>(frameSize),
                      static_cast<unsigned int>(JPEG_BUFFER_SIZE));
        return false;
    }

    // ── Read JPEG payload ─────────────────────────────────────────────────────
    const size_t dataBytesRead = videoFile_.read(jpegBuffer_, frameSize);
    if (dataBytesRead != frameSize)
    {
        Serial.println("[VideoPlayer] Unexpected EOF while reading frame payload.");
        return false;
    }
    jpegSize_ = frameSize;

    // ── Decode and render ─────────────────────────────────────────────────────
    // A failed decode is treated as a skipped frame, NOT as an end-of-file
    // condition. This prevents a single corrupt JPEG from restarting the video.
    if (!decodeAndDrawFrame())
    {
        decodeFailures_++;
        Serial.printf("[VideoPlayer] Frame %lu: JPEG decode failed (total failures: %lu).\n",
                      static_cast<unsigned long>(currentFrame_),
                      static_cast<unsigned long>(decodeFailures_));

        // Safety valve: give up after too many consecutive failures.
        if (decodeFailures_ >= MAX_DECODE_FAILURES)
        {
            Serial.println("[VideoPlayer] Too many consecutive decode failures. Stopping.");
            return false;
        }
    }
    else
    {
        // Reset the consecutive-failure counter on each successful decode.
        decodeFailures_ = 0;
    }

    currentFrame_++;

    if (VIDEO_TIMING_DEBUG && (currentFrame_ % 30 == 0))
    {
        Serial.printf("[VideoPlayer] Frame %lu / %lu\n",
                      static_cast<unsigned long>(currentFrame_),
                      static_cast<unsigned long>(totalFrames_));
    }

    // Return true even when a single frame was skipped — caller should
    // not restart the video just because one frame failed.
    return true;
}

bool VideoPlayer::decodeAndDrawFrame()
{
    // Set the singleton pointer so the static callback can reach this instance.
    // This is safe as long as only one VideoPlayer exists (see header note).
    activeInstance_ = this;

    if (!jpeg_.openRAM(jpegBuffer_, static_cast<int>(jpegSize_), jpegDrawCallback))
    {
        activeInstance_ = nullptr;
        return false;
    }

    // jpeg_.decode() returns 1 on success.
    // It may also return values > 1 for multi-scan images; treat any non-zero
    // positive result as success to handle edge cases in the JPEGDEC library.
    const int decodeResult = jpeg_.decode(0, 0, 0);
    jpeg_.close();
    activeInstance_ = nullptr;

    return decodeResult > 0;
}

void VideoPlayer::restartPlayback()
{
    // Prefer seeking over re-opening the file: faster and avoids file-system
    // overhead on every loop iteration.
    // TVID_HEADER_SIZE is derived from sizeof(VideoFileHeader), so it stays
    // correct if the header struct ever gains new fields.
    if (videoFile_ && videoFile_.seek(TVID_HEADER_SIZE, SeekSet))
    {
        currentFrame_    = 0;
        decodeFailures_  = 0;
        lastFrameTickMs_ = millis();
    }
    else
    {
        // Seek failed — re-open the file from scratch as a fallback.
        Serial.println("[VideoPlayer] Seek failed during loop; re-opening file.");
        if (!openVideoFile())
        {
            initialized_ = false;
            Serial.println("[VideoPlayer] Failed to restart playback.");
        }
        else
        {
            decodeFailures_  = 0;
            lastFrameTickMs_ = millis();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Private — JPEGDEC Callback
// ─────────────────────────────────────────────────────────────────────────────

// Static trampoline required by JPEGDEC's C-style callback signature.
int VideoPlayer::jpegDrawCallback(JPEGDRAW* draw)
{
    if (activeInstance_ == nullptr)
    {
        return 0;
    }
    return activeInstance_->drawJpegBlock(draw);
}

// Renders one decoded JPEG block (up to 16×16 pixels) to the TFT.
// Called repeatedly by JPEGDEC until the full frame is rendered.
int VideoPlayer::drawJpegBlock(JPEGDRAW* draw)
{
    if (draw == nullptr)
    {
        return 0;
    }

    // Skip blocks that start outside the display area.
    if (draw->x >= TFT_WIDTH || draw->y >= TFT_HEIGHT)
    {
        return 1; // Return 1 to allow JPEGDEC to continue with the next block.
    }

    // Clip blocks that extend beyond the display edge.
    // This can happen when the video resolution slightly exceeds the display
    // due to JPEG block-boundary rounding (JPEG blocks are 8 or 16 pixels wide).
    const int clippedWidth  = min(static_cast<int>(draw->iWidth),
                                  static_cast<int>(TFT_WIDTH)  - static_cast<int>(draw->x));
    const int clippedHeight = min(static_cast<int>(draw->iHeight),
                                  static_cast<int>(TFT_HEIGHT) - static_cast<int>(draw->y));

    if (clippedWidth <= 0 || clippedHeight <= 0)
    {
        return 1;
    }

    tft_.pushImage(draw->x,
                   draw->y,
                   clippedWidth,
                   clippedHeight,
                   static_cast<uint16_t*>(draw->pPixels));
    return 1;
}

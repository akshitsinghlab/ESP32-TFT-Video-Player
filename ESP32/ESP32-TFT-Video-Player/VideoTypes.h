#ifndef VIDEO_TYPES_H
#define VIDEO_TYPES_H

// ─────────────────────────────────────────────────────────────────────────────
// TVID Binary Format — Version 1
// ─────────────────────────────────────────────────────────────────────────────
//
// All multi-byte integers are stored little-endian to match the Python
// struct.pack("<...") output and native ESP32 byte order.
//
// File layout:
//   [ VideoFileHeader ]          — exactly TVID_HEADER_SIZE bytes
//   [ uint32_t frameSize ]       ┐
//   [ uint8_t  jpegData[...] ]   ┤ repeated totalFrames times
//   [ uint32_t frameSize ]       ┤
//   [ uint8_t  jpegData[...] ]   ┘
//
// ─────────────────────────────────────────────────────────────────────────────

#include <stdint.h>

// Magic bytes at offset 0. Must equal "TVID".
#define TVID_MAGIC "TVID"
#define TVID_MAGIC_LEN 4

// __attribute__((packed)) prevents the compiler from inserting padding bytes
// between members. Without it, struct sizeof() may differ from the Python
// struct.pack() output, causing silent header misreads.
struct __attribute__((packed)) VideoFileHeader
{
    char     magic[TVID_MAGIC_LEN]; // "TVID"
    uint16_t version;               // Format version (must equal VIDEO_FORMAT_VERSION)
    uint16_t width;                 // Frame width  in pixels
    uint16_t height;                // Frame height in pixels
    uint16_t fps;                   // Playback frame rate
    uint32_t totalFrames;           // Total number of frames in the file
    // NOTE: reserved bytes (if any) would go here in future versions.
};

// The byte size of the header as written to disk by the Python converter.
// Using sizeof() on the packed struct guarantees this stays in sync if
// members are ever added.
static constexpr uint32_t TVID_HEADER_SIZE = sizeof(VideoFileHeader);

// Size in bytes of the per-frame length prefix (uint32_t, little-endian).
static constexpr uint32_t TVID_FRAME_SIZE_FIELD = sizeof(uint32_t);

#endif // VIDEO_TYPES_H

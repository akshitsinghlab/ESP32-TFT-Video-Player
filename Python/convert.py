#!/usr/bin/env python3

"""
convert.py — MP4 to TVID video.bin converter for ESP32 + ST7735

Converts any MP4 into a single binary file (video.bin) suitable for
playback on an ESP32 via LittleFS without an SD card.

Binary format (TVID v1):
  Bytes  0– 3 : magic "TVID"
  Bytes  4– 5 : uint16 version
  Bytes  6– 7 : uint16 width
  Bytes  8– 9 : uint16 height
  Bytes 10–11 : uint16 fps
  Bytes 12–15 : uint32 totalFrames
  ...repeated for each frame:
  Bytes  0– 3 : uint32 frameSize
  Bytes  4–(4+frameSize-1) : JPEG data

All integers are little-endian to match ESP32 native byte order.

Usage:
  python convert.py -i video.mp4
  python convert.py -i clip.mp4 --width 160 --height 128 --fps 20 --quality 75
  python convert.py -i clip.mp4 --sharpen --contrast 1.2
"""

import argparse
import struct
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

import cv2
import numpy as np

try:
    from PIL import Image, ImageEnhance
    PIL_AVAILABLE = True
except ImportError:
    PIL_AVAILABLE = False

try:
    from tqdm import tqdm
    TQDM_AVAILABLE = True
except ImportError:
    TQDM_AVAILABLE = False

# ─────────────────────────────────────────────────────────────────────────────
# TVID header constants — must match VideoTypes.h on the ESP32 side
# ─────────────────────────────────────────────────────────────────────────────

TVID_MAGIC       = b"TVID"
TVID_VERSION     = 1

# struct.pack format for the 16-byte header (little-endian)
#   4s  magic[4]
#   H   version  (uint16)
#   H   width    (uint16)
#   H   height   (uint16)
#   H   fps      (uint16)
#   I   totalFrames (uint32)
HEADER_FORMAT    = "<4sHHHHI"
HEADER_SIZE      = struct.calcsize(HEADER_FORMAT)   # always 16 bytes
FRAME_LEN_FORMAT = "<I"                             # per-frame size prefix
FRAME_LEN_SIZE   = struct.calcsize(FRAME_LEN_FORMAT) # always 4 bytes

# Byte offset of the totalFrames field inside the header.
# Computed from the format string so it stays correct if fields are ever added.
TOTAL_FRAMES_OFFSET = struct.calcsize("<4sHHHH")  # magic + version + width + height + fps = 12

DEFAULT_OUTPUT = "output/video.bin"
DEFAULT_INPUT  = "video.mp4"

# ─────────────────────────────────────────────────────────────────────────────
# Data classes
# ─────────────────────────────────────────────────────────────────────────────

@dataclass
class ConversionSettings:
    input_path:  Path
    output_path: Path
    width:       int
    height:      int
    fps:         int
    quality:     int
    sharpen:     bool
    contrast:    float


@dataclass
class ConversionStats:
    source_fps:        float = 0.0
    source_frames:     int   = 0
    written_frames:    int   = 0
    total_jpeg_bytes:  int   = 0
    output_file_bytes: int   = 0
    elapsed_sec:       float = 0.0

# ─────────────────────────────────────────────────────────────────────────────
# Argument validation helpers
# ─────────────────────────────────────────────────────────────────────────────

def positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be > 0")
    return parsed


def quality_int(value: str) -> int:
    parsed = int(value)
    if not (1 <= parsed <= 100):
        raise argparse.ArgumentTypeError("quality must be in range 1–100")
    return parsed


def positive_float(value: str) -> float:
    parsed = float(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be > 0")
    return parsed

# ─────────────────────────────────────────────────────────────────────────────
# Frame processing
# ─────────────────────────────────────────────────────────────────────────────

def resize_frame(frame_bgr: np.ndarray, width: int, height: int) -> np.ndarray:
    """Resize a BGR frame using high-quality Lanczos interpolation."""
    return cv2.resize(frame_bgr, (width, height), interpolation=cv2.INTER_LANCZOS4)


def apply_enhancements(frame_bgr: np.ndarray, sharpen: bool, contrast: float) -> np.ndarray:
    """
    Optionally sharpen and/or adjust contrast using Pillow.
    Returns the processed frame as a BGR numpy array.
    """
    if not (sharpen or contrast != 1.0):
        return frame_bgr

    if not PIL_AVAILABLE:
        print("Warning: Pillow not installed; skipping sharpening/contrast.")
        return frame_bgr

    # Convert BGR → RGB for Pillow
    rgb = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2RGB)
    img = Image.fromarray(rgb)

    if contrast != 1.0:
        img = ImageEnhance.Contrast(img).enhance(contrast)

    if sharpen:
        img = ImageEnhance.Sharpness(img).enhance(2.0)

    # Convert back to BGR numpy array
    return cv2.cvtColor(np.array(img), cv2.COLOR_RGB2BGR)


def encode_frame_to_jpeg(frame_bgr: np.ndarray, quality: int) -> bytes:
    """JPEG-compress a BGR frame. Raises RuntimeError on failure."""
    ok, encoded = cv2.imencode(
        ".jpg",
        frame_bgr,
        [int(cv2.IMWRITE_JPEG_QUALITY), quality],
    )
    if not ok:
        raise RuntimeError("cv2.imencode failed to produce JPEG output.")
    return encoded.tobytes()

# ─────────────────────────────────────────────────────────────────────────────
# Header I/O
# ─────────────────────────────────────────────────────────────────────────────

def write_header(file_obj, width: int, height: int, fps: int, total_frames: int = 0) -> None:
    """Write the 16-byte TVID file header."""
    packed = struct.pack(HEADER_FORMAT, TVID_MAGIC, TVID_VERSION, width, height, fps, total_frames)
    assert len(packed) == HEADER_SIZE, f"Header size mismatch: {len(packed)} != {HEADER_SIZE}"
    file_obj.write(packed)


def patch_total_frames(file_obj, total_frames: int) -> None:
    """
    Overwrite the totalFrames field in an already-written header.
    Called after all frames have been written so we know the exact count.
    """
    file_obj.seek(TOTAL_FRAMES_OFFSET)
    file_obj.write(struct.pack("<I", total_frames))

# ─────────────────────────────────────────────────────────────────────────────
# Frame selection (integer arithmetic, no float drift)
# ─────────────────────────────────────────────────────────────────────────────

def should_keep_frame(source_frame_index: int, frames_kept: int, source_fps: float, target_fps: int) -> bool:
    """
    Returns True if this source frame should be included in the output.

    Uses integer cross-multiplication to avoid floating-point accumulation
    drift that compounds over thousands of frames:
        keep if: source_frame_index * target_fps >= frames_kept * source_fps
    """
    return source_frame_index * target_fps >= frames_kept * source_fps

# ─────────────────────────────────────────────────────────────────────────────
# Console output
# ─────────────────────────────────────────────────────────────────────────────

def print_config_summary(s: ConversionSettings) -> None:
    print("\n" + "=" * 56)
    print("  ESP32 TFT Video Converter")
    print("=" * 56)
    print(f"  Input        : {s.input_path}")
    print(f"  Output       : {s.output_path}")
    print(f"  Resolution   : {s.width} × {s.height}")
    print(f"  Target FPS   : {s.fps}")
    print(f"  JPEG Quality : {s.quality}")
    print(f"  Sharpen      : {'yes' if s.sharpen else 'no'}")
    print(f"  Contrast     : {s.contrast:.2f}")
    print(f"  Header size  : {HEADER_SIZE} bytes  (TVID v{TVID_VERSION})")
    print("=" * 56)


def print_stats(stats: ConversionStats, width: int, height: int) -> None:
    avg_jpeg       = stats.total_jpeg_bytes / max(1, stats.written_frames)
    raw_rgb565     = width * height * 2 * max(1, stats.written_frames)
    ratio          = raw_rgb565 / max(1, stats.output_file_bytes)
    effective_fps  = stats.written_frames / max(0.001, stats.elapsed_sec)
    source_dur     = stats.source_frames / max(0.001, stats.source_fps)

    print("\n" + "=" * 56)
    print("  Conversion Complete")
    print("=" * 56)
    print(f"  Source FPS           : {stats.source_fps:.2f}")
    print(f"  Source Frames        : {stats.source_frames}")
    print(f"  Source Duration      : {source_dur:.1f} s")
    print(f"  Frames Written       : {stats.written_frames}")
    print(f"  Avg JPEG Frame Size  : {avg_jpeg / 1024:.1f} KB")
    print(f"  Output File Size     : {stats.output_file_bytes / 1024 / 1024:.2f} MB")
    print(f"  vs Raw RGB565        : {ratio:.1f}× smaller")
    print(f"  Converter Speed      : {effective_fps:.1f} frame/s")
    print(f"  Elapsed Time         : {stats.elapsed_sec:.1f} s")
    print("=" * 56)
    print()

# ─────────────────────────────────────────────────────────────────────────────
# Core conversion
# ─────────────────────────────────────────────────────────────────────────────

def convert_video(s: ConversionSettings) -> ConversionStats:
    if not s.input_path.exists():
        raise FileNotFoundError(f"Input file not found: {s.input_path}")

    # Create output directory if it does not exist.
    if s.output_path.parent and not s.output_path.parent.exists():
        s.output_path.parent.mkdir(parents=True, exist_ok=True)

    cap = cv2.VideoCapture(str(s.input_path))
    if not cap.isOpened():
        raise RuntimeError(f"OpenCV could not open video: {s.input_path}")

    stats = ConversionStats()
    stats.source_fps    = cap.get(cv2.CAP_PROP_FPS) or float(s.fps)
    stats.source_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT) or 0)

    if stats.source_fps <= 0:
        print(f"Warning: source FPS unreadable; assuming {s.fps} FPS.")
        stats.source_fps = float(s.fps)

    start = time.perf_counter()
    source_frame_index = 0   # index into source video (every frame read)
    frames_kept        = 0   # how many frames we have written so far

    # Iterator helper — yields (ok, frame) from cap.read() with optional tqdm.
    total_for_bar: Optional[int] = stats.source_frames if stats.source_frames > 0 else None

    with s.output_path.open("wb") as out_file:
        # Write placeholder header; totalFrames will be patched at the end.
        write_header(out_file, width=s.width, height=s.height, fps=s.fps, total_frames=0)

        if TQDM_AVAILABLE:
            progress_iter = tqdm(total=total_for_bar, unit="frame", colour="green", desc="Converting")
        else:
            progress_iter = None

        try:
            while True:
                ok, frame = cap.read()
                if not ok:
                    break

                # ── Frame selection (integer arithmetic, no float drift) ──────
                if should_keep_frame(source_frame_index, frames_kept, stats.source_fps, s.fps):
                    # Resize → enhance → JPEG-encode → write
                    frame = resize_frame(frame, s.width, s.height)
                    frame = apply_enhancements(frame, s.sharpen, s.contrast)
                    jpeg_data = encode_frame_to_jpeg(frame, s.quality)

                    out_file.write(struct.pack(FRAME_LEN_FORMAT, len(jpeg_data)))
                    out_file.write(jpeg_data)

                    frames_kept           += 1
                    stats.written_frames  += 1
                    stats.total_jpeg_bytes += len(jpeg_data)

                source_frame_index += 1

                if progress_iter is not None:
                    progress_iter.update(1)
        finally:
            if progress_iter is not None:
                progress_iter.close()

        # Patch the totalFrames field now that we know the exact count.
        patch_total_frames(out_file, stats.written_frames)

    cap.release()

    stats.source_frames    = source_frame_index   # actual frames decoded
    stats.output_file_bytes = s.output_path.stat().st_size
    stats.elapsed_sec       = time.perf_counter() - start
    return stats

# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Convert MP4 → TVID video.bin for ESP32 + ST7735 (160×128 landscape)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )

    parser.add_argument(
        "-i", "--input",
        default=DEFAULT_INPUT,
        help="Input video file",
    )
    parser.add_argument(
        "-o", "--output",
        default=DEFAULT_OUTPUT,
        help="Output file path",
    )
    parser.add_argument(
        "--width",
        type=positive_int,
        default=160,
        help="Target frame width in pixels (must match TFT_WIDTH in Config.h)",
    )
    parser.add_argument(
        "--height",
        type=positive_int,
        default=128,
        help="Target frame height in pixels (must match TFT_HEIGHT in Config.h)",
    )
    parser.add_argument(
        "--fps",
        type=positive_int,
        default=20,
        help="Target playback frame rate",
    )
    parser.add_argument(
        "--quality",
        type=quality_int,
        default=75,
        help="JPEG quality (1–100). Higher = better image, larger file.",
    )
    parser.add_argument(
        "--sharpen",
        action="store_true",
        default=False,
        help="Apply sharpening to each frame (requires Pillow).",
    )
    parser.add_argument(
        "--contrast",
        type=positive_float,
        default=1.0,
        help="Contrast multiplier (1.0 = no change, >1.0 = more contrast). Requires Pillow.",
    )

    return parser


def main() -> int:
    parser = build_parser()
    args   = parser.parse_args()

    # Warn early if Pillow is needed but unavailable.
    if (args.sharpen or args.contrast != 1.0) and not PIL_AVAILABLE:
        print("Warning: --sharpen / --contrast require Pillow (pip install Pillow).")
        print("         These options will be ignored.")

    settings = ConversionSettings(
        input_path  = Path(args.input),
        output_path = Path(args.output),
        width       = args.width,
        height      = args.height,
        fps         = args.fps,
        quality     = args.quality,
        sharpen     = args.sharpen,
        contrast    = args.contrast,
    )

    print_config_summary(settings)

    try:
        stats = convert_video(settings)

        if stats.written_frames == 0:
            raise RuntimeError(
                "No frames were written. "
                "Check that the input file is a valid video and is not empty."
            )

        print_stats(stats, settings.width, settings.height)
        print(f"Output: {settings.output_path.resolve()}")
        print("Upload video.bin to ESP32 LittleFS, then flash the sketch.")
        return 0

    except KeyboardInterrupt:
        print("\nConversion cancelled.")
        return 130

    except Exception as exc:
        print(f"\nError: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

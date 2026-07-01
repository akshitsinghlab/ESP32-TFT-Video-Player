# ESP32-TFT-Video-Player

Play MP4 videos on a 1.8-inch ST7735 TFT LCD using an ESP32 — no SD card required.

Videos are converted to a single optimised binary (`video.bin`), stored in LittleFS flash, and streamed one JPEG frame at a time.

---

## Features

- SD-card-free playback via LittleFS
- Single binary file per video (no thousands of JPEG files)
- JPEG compressed frames — typically 10–30× smaller than raw RGB565
- Configurable resolution, FPS, and JPEG quality
- Loop playback
- Non-blocking `update()` loop — ready for future buttons, audio, OTA
- Clean object-oriented architecture
- Beginner-friendly, fully documented code

---

## Hardware

| Part       | Details                             |
| ---------- | ----------------------------------- |
| MCU        | ESP32-WROOM-32 NodeMCU (38-pin)     |
| Display    | 1.8-inch TFT LCD, ST7735 controller |
| Resolution | 160 × 128 (landscape)               |
| Interface  | SPI                                 |

### Wiring

| TFT Pin    | ESP32 GPIO |
| ---------- | ---------- |
| VCC        | 3V3        |
| GND        | GND        |
| SCK        | 18         |
| SDA (MOSI) | 23         |
| CS         | 5          |
| DC (A0)    | 2          |
| RESET      | 4          |
| LED        | 3V3        |

---

## Quick Start

### 1. Install Arduino Libraries

In the Arduino IDE Library Manager, install:

- **TFT_eSPI** by Bodmer
- **JPEGDEC** by Larry Bank
- **LittleFS** (bundled with the ESP32 Arduino core)

After installing TFT_eSPI, edit `libraries/TFT_eSPI/User_Setup.h` to match your pins:

```cpp
#define ST7735_DRIVER
#define TFT_WIDTH  128   // physical pin axis
#define TFT_HEIGHT 160
#define TFT_MOSI   23
#define TFT_SCLK   18
#define TFT_CS      5
#define TFT_DC      2
#define TFT_RST     4
```

### 2. Convert Your Video

```bash
cd Python
cd ..
python -m venv .venv
```

Activate the virtual environment:

```powershell
# Windows (PowerShell)
.\.venv\Scripts\Activate.ps1
```

```bash
# macOS / Linux
source .venv/bin/activate
```

Then install dependencies and run the converter:

```bash
cd Python
pip install -r requirements.txt
python convert.py -i your_video.mp4
```

This produces `Python/output/video.bin`.

**Options:**

```
-i / --input     Input MP4 file           (default: video.mp4)
-o / --output    Output path              (default: output/video.bin)
--width          Frame width in pixels    (default: 160)
--height         Frame height in pixels   (default: 128)
--fps            Target frame rate        (default: 20)
--quality        JPEG quality 1–100       (default: 75)
--sharpen        Apply sharpening filter  (requires Pillow)
--contrast N     Contrast multiplier      (default: 1.0, requires Pillow)
```

> **Important:** Always use `--width 160 --height 128` (landscape). These match `TFT_WIDTH` / `TFT_HEIGHT` in `Config.h`.

### 3. Upload video.bin to LittleFS

Install the [ESP32 LittleFS Upload Tool](https://github.com/lorol/arduino-esp32littlefs-plugin):

1. Copy `video.bin` into `ESP32/data/video.bin`
2. In the Arduino IDE: **Tools → ESP32 LittleFS Data Upload**

### 4. Flash the Sketch

Open `ESP32/ESP32-TFT-Video-Player.ino` in the Arduino IDE and upload.

---

## Configuration

### `Config.h`

| Constant              | Default | Description                          |
| --------------------- | ------- | ------------------------------------ |
| `TFT_WIDTH`           | `160`   | Logical display width (landscape)    |
| `TFT_HEIGHT`          | `128`   | Logical display height (landscape)   |
| `LOOP_PLAYBACK`       | `true`  | Restart after the last frame         |
| `JPEG_BUFFER_SIZE`    | `32768` | Heap buffer for one JPEG frame       |
| `MAX_DECODE_FAILURES` | `10`    | Stop after N consecutive bad frames  |
| `VIDEO_TIMING_DEBUG`  | `false` | Print frame progress every 30 frames |

### `TFT_Config.h`

| Constant               | Default     | Description                      |
| ---------------------- | ----------- | -------------------------------- |
| `TFT_ROTATION`         | `1`         | Display rotation (1 = landscape) |
| `TFT_BACKGROUND_COLOR` | `TFT_BLACK` | Fill color before first frame    |

---

## File Format (TVID v1)

```
Bytes  0– 3  magic "TVID"
Bytes  4– 5  uint16 version
Bytes  6– 7  uint16 width
Bytes  8– 9  uint16 height
Bytes 10–11  uint16 fps
Bytes 12–15  uint32 totalFrames
... repeated per frame:
  uint32 frameSize
  uint8  jpegData[frameSize]
```

All integers are little-endian.

---

## Performance

Typical results on ESP32-WROOM-32 (no PSRAM):

| Setting         | Value     |
| --------------- | --------- |
| Resolution      | 160 × 128 |
| FPS             | 20        |
| JPEG Quality    | 75        |
| Avg frame size  | ~6 KB     |
| 30-second video | ~3.5 MB   |

A 4 MB flash ESP32 holds approximately 20–25 seconds of video at default settings.

---

## Troubleshooting

**Black screen / no display**

- Verify all 7 wiring connections
- Confirm `TFT_ROTATION` produces landscape orientation
- Check that TFT_eSPI `User_Setup.h` matches `TFT_Config.h` pins

**"Could not open /video.bin"**

- Re-run the LittleFS upload tool
- Confirm `LITTLEFS_AUTO_FORMAT_IF_MOUNT_FAILED false` and that the partition scheme includes SPIFFS/LittleFS

**"Invalid magic number"**

- The file was not produced by this converter, or was truncated during upload

**Resolution warning in serial output**

- Re-convert with `--width 160 --height 128`

**Stuttering / frame drops**

- Reduce JPEG quality: `--quality 60`
- Reduce FPS: `--fps 15`
- Increase `JPEG_BUFFER_SIZE` if frames are being skipped due to size

---

## Project Structure

```
ESP32-TFT-Video-Player/
├── ESP32/
│   ├── ESP32-TFT-Video-Player.ino
│   ├── Config.h
│   ├── TFT_Config.h
│   ├── VideoTypes.h        ← shared binary format definition
│   ├── VideoPlayer.h
│   ├── VideoPlayer.cpp
│   └── data/
│       └── video.bin       ← place your converted file here
├── Python/
│   ├── convert.py
│   ├── requirements.txt
│   └── output/
│       └── video.bin
├── docs/
└── README.md
```

---

## Roadmap (v2)

- Pause / Resume / Stop
- Fast forward / Reverse
- Playlist support
- DMA double-buffered rendering
- PSRAM support (ESP32-S3)
- Wi-Fi / HTTP video streaming
- Audio via DFPlayer Mini
- Touchscreen controls

---

## License

MIT License. See `LICENSE` for details.

---

## Contributing

Pull requests welcome. Please open an issue first to discuss significant changes.

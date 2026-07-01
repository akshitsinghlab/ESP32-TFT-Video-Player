#ifndef TFT_CONFIG_H
#define TFT_CONFIG_H

// ─────────────────────────────────────────────────────────────────────────────
// SPI Pin Mapping — ESP32-WROOM-32 (38-pin NodeMCU)
// ─────────────────────────────────────────────────────────────────────────────
//
//  TFT Label   ESP32 Pin
//  ─────────   ─────────
//  VCC         3V3
//  GND         GND
//  SCK         GPIO18
//  SDA (MOSI)  GPIO23
//  CS          GPIO5
//  DC (A0)     GPIO2
//  RESET       GPIO4
//  LED         3V3
//
// These must also be set in your TFT_eSPI User_Setup.h (or User_Setup_Select.h).
// ─────────────────────────────────────────────────────────────────────────────

#define TFT_MOSI 23
#define TFT_MISO 19   // Not connected to display; required by TFT_eSPI SPI init
#define TFT_SCLK 18

#define TFT_CS   5
#define TFT_DC   2
#define TFT_RST  4

// ─────────────────────────────────────────────────────────────────────────────
// Display Orientation
//
//  0 = portrait  (128 wide × 160 tall)
//  1 = landscape (160 wide × 128 tall)  ← used by this project
//  2 = portrait  rotated 180°
//  3 = landscape rotated 180°
//
// TFT_WIDTH / TFT_HEIGHT in Config.h must match the logical dimensions
// produced by this rotation setting.
// ─────────────────────────────────────────────────────────────────────────────
#define TFT_ROTATION 1

// Color shown on the display while the first frame is being decoded.
#define TFT_BACKGROUND_COLOR TFT_BLACK

#endif // TFT_CONFIG_H

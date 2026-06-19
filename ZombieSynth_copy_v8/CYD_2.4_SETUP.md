# 2.4" CYD (ESP32-2432S024) — Build & Touch Setup

This branch (`claude/cyd-2.4-inch`) is the **2.4" Cheap Yellow Display** build of
the synth. The only differences from the 2.8" build are the display config and a
self-calibrating touch system so it works on the first boot.

## 1. Libraries (Arduino IDE → Library Manager)
- **TFT_eSPI** (Bodmer)
- **XPT2046_Touchscreen** (Paul Stoffregen)

## 2. Copy `User_Setup.h` into the TFT_eSPI library
Replace `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` with the
`User_Setup.h` from this folder. It is already set for the 2.4" CYD:
- `ILI9341_DRIVER`
- `TFT_INVERSION_OFF`
- `SPI_FREQUENCY 40000000` (40 MHz)
- Display pins: MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, RST −1, BL 21

## 3. Board settings
- Board: **ESP32 Dev Module**
- Partition Scheme: **Default 4MB with spiffs** (NVS is used for presets,
  patterns, and the saved touch calibration)

## 4. Flash, then calibrate touch (one time)
On the **very first boot** (or any time you **hold a finger on the screen while
powering on**) the synth shows a **TOUCH CALIBRATION** screen:

1. Tap the cross in the **top-left** corner.
2. Tap the cross in the **bottom-right** corner.
3. It prints "CALIBRATED!" and saves to flash.

From then on it boots straight into the synth with accurate touch. To redo it
later, just hold the screen while powering on.

> If you don't tap within ~12 s it falls back to sensible defaults and continues.

## 5. If something looks wrong
| Symptom | Fix |
|--------|-----|
| Colours inverted (red bg looks cyan/teal) | In `ZombieSynth_copy_v8.ino` set `#define CYD24_INVERT 1`, re-flash |
| Display blank / garbled / wrong colours at edges | In `User_Setup.h` switch to `ILI9341_2_DRIVER` (comment `ILI9341_DRIVER`) |
| Touch lands in the wrong place | Hold the screen while powering on to re-run calibration |
| Flicker / display noise | Lower `SPI_FREQUENCY` to `27000000` in `User_Setup.h` |

## Pinout (key differences from 2.8")

| Signal | 2.4" (ESP32-2432S024) | 2.8" (ESP32-2432S028R) |
|--------|----------------------|------------------------|
| TFT Backlight | **GPIO 27** | GPIO 21 |
| I2S LRCLK | **GPIO 21** | GPIO 27 |

- **Touch (XPT2046, VSPI):** IRQ 36, MOSI 32, MISO 39, CLK 25, CS 33
- **Display (ILI9341, HSPI):** MISO 12, MOSI 13, SCLK 14, CS 15, DC 2, BL **27**
- **Audio I2S (PCM5102):** BCLK 22, LRCLK **21**, DIN 17 (remap `I2S_DATA_PIN` in
  `synth_engine.h` if needed)
- **MIDI in:** GPIO 35

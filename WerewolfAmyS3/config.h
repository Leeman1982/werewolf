// WerewolfAmyS3 — hardware + build configuration
// Target: ESP32-S3-WROOM-1 DevKitC (N16R8: 16 MB flash / 8 MB PSRAM recommended)
//         3.2" ILI9341 320x240 SPI TFT with XPT2046 resistive touch
//         PCM5102A I2S DAC, 2x CD74HC4067 muxes, DIN MIDI in/out, USB MIDI

#pragma once
#include <Arduino.h>

// ── Firmware identity ───────────────────────────────────────────────────────
#define WEREWOLF_VERSION "1.0"
#define WEREWOLF_NAME    "WEREWOLF"

// ── Display (SPI2/FSPI @ 40 MHz) — must match User_Setup.h for TFT_eSPI ─────
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_MISO   13
#define PIN_TFT_CS     10
#define PIN_TFT_DC      9
#define PIN_TFT_RST    14
#define PIN_TFT_BL     21   // backlight, active HIGH

// ── XPT2046 touch (dedicated SPI bus — the module has its own T_* header) ───
#define PIN_TOUCH_CLK  15
#define PIN_TOUCH_MOSI 16   // T_DIN
#define PIN_TOUCH_MISO 47   // T_DO
#define PIN_TOUCH_CS    8
#define PIN_TOUCH_IRQ   3

// Raw touch calibration (landscape rotation 1). Adjust in SETUP page.
#define TOUCH_X_MIN   200
#define TOUCH_X_MAX  3700
#define TOUCH_Y_MIN   240
#define TOUCH_Y_MAX  3800

// ── SD card slot on the display module (optional, shares TFT SPI) ───────────
#define PIN_SD_CS       7

// ── I2S audio out → PCM5102A ────────────────────────────────────────────────
// PCM5102A module: SCK→GND, FLT/DEMP/FMT→GND, XSMT→3.3V
#define PIN_I2S_BCLK    5
#define PIN_I2S_LRCLK   4
#define PIN_I2S_DOUT    6

// ── DIN MIDI (UART1, 31250 baud). IN via 6N138 optocoupler, OUT via buffer ──
#define PIN_MIDI_IN    18
#define PIN_MIDI_OUT   17
#define MIDI_UART_NUM   1

// ── CD74HC4067 multiplexers (shared address bus) ────────────────────────────
// MUX A: 16 potentiometers  → SIG to ADC (GPIO1 = ADC1_CH0)
// MUX B: 16 momentary switches (to GND) → SIG to GPIO2 (INPUT_PULLUP)
#define PIN_MUX_S0     39
#define PIN_MUX_S1     40
#define PIN_MUX_S2     41
#define PIN_MUX_S3     42
#define PIN_MUX_POTS    1   // analog in
#define PIN_MUX_SW      2   // digital in, pull-up

#define NUM_POTS       16
#define NUM_SWITCHES   16

// ── Status LED (DevKitC WS2812: GPIO48 on v1.0 boards, GPIO38 on v1.1) ──────
#define PIN_STATUS_LED 48

// ── Engine sizes ────────────────────────────────────────────────────────────
#define WOLF_SYNTH_NUM     1     // AMY synth slot for the werewolf synth
#define WOLF_VOICES        8     // polyphony
#define WOLF_PATCH_NUMBER  1024  // AMY memory-patch slot we (re)write live

#define GM_MAX_VOICES      24    // TinySoundFont voice cap
#define GM_CHANNELS        16
#define GM_DRUM_CHANNEL    9     // 0-based (MIDI ch 10)

// ── Sequencer dimensions ────────────────────────────────────────────────────
#define PPQ                48    // AMY sequencer ticks per quarter note
#define TICKS_PER_16TH     (PPQ / 4)          // 12
#define MIDI_CLOCK_DIV     (PPQ / 24)         // send 0xF8 every 2 ticks

#define WSEQ_TRACKS        4
#define WSEQ_STEPS         16
#define WSEQ_PATTERNS      8

#define GMSEQ_TRACKS       16
#define GMSEQ_STEPS        16
#define GMSEQ_PATTERNS     8
#define GMSEQ_CHAIN_LEN    8

#define BPM_MIN            30
#define BPM_MAX            300
#define BPM_DEFAULT        120

// ── SoundFont flash partition ───────────────────────────────────────────────
// Matches partitions_werewolf_16MB.csv: type 0x40, subtype 0x00, label "sf2"
#define SF2_PARTITION_LABEL "sf2"

// ── UI ──────────────────────────────────────────────────────────────────────
#define SCREEN_W 320
#define SCREEN_H 240
#define UI_FPS   30

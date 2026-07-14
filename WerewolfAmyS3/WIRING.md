# WerewolfAmyS3 — Wiring Guide

Target: **ESP32-S3-WROOM-1 DevKitC-1 (N16R8)**. All pins below are GPIO
numbers. Avoid GPIO 26–37 (flash/PSRAM), 19/20 are the native USB pins
(the "USB" connector), 43/44 are UART0 (serial log), 0/45/46 are strapping.

## 3.2" ILI9341 SPI display (320×240, 14-pin header)

| Module pin | ESP32-S3 | Notes |
|---|---|---|
| VCC | 3.3 V | check your module's regulator; most accept 3.3–5 V |
| GND | GND | |
| CS | **10** | |
| RESET | **14** | |
| DC/RS | **9** | |
| SDI (MOSI) | **11** | |
| SCK | **12** | |
| LED (BL) | **21** | driven HIGH by firmware |
| SDO (MISO) | **13** | |

## XPT2046 touch (same module, T_* pins — separate SPI bus)

| Module pin | ESP32-S3 |
|---|---|
| T_CLK | **15** |
| T_CS | **8** |
| T_DIN | **16** |
| T_DO | **47** |
| T_IRQ | **3** |

## SD card slot (optional, on the display module)

Wire SD_SCK/SD_MOSI/SD_MISO to the display SPI (12/11/13) and SD_CS to
**7**. Unused by the current firmware (reserved for future SMF playback).

## PCM5102A I2S DAC

| PCM5102A | ESP32-S3 | Notes |
|---|---|---|
| VIN | 3.3 V | |
| GND | GND | |
| BCK | **5** | bit clock |
| LCK | **4** | word select |
| DIN | **6** | data |
| SCK | GND | no external master clock |
| FLT / DEMP / FMT | GND | normal filter, no de-emphasis, I2S format |
| XSMT | 3.3 V | un-mute |

## DIN MIDI (UART1 @ 31250 baud)

**MIDI IN** (6N138 optocoupler, standard circuit):
```
DIN pin 5 ──220Ω──► 6N138 pin 2        6N138 pin 8 ── 3.3V
DIN pin 4 ─────────► 6N138 pin 3        6N138 pin 5 ── GND
1N4148 across pins 2-3 (cathode to pin 2)
6N138 pin 6 ──► GPIO 18   + 4.7kΩ pull-up to 3.3V
```

**MIDI OUT**:
```
GPIO 17 ──10Ω──► DIN pin 5
3.3V   ──33Ω──► DIN pin 4
DIN pin 2 ── GND (shield)
```

## USB MIDI

Use the ESP32-S3 **USB connector** (GPIO 19/20, the one marked "USB", not
"COM"). The firmware enumerates as a class-compliant USB-MIDI device
(build with USB Mode = USB-OTG/TinyUSB). Serial logging stays on the COM
(UART) connector.

## Multiplexers — 2 × CD74HC4067

Shared address bus (both muxes):

| Signal | ESP32-S3 |
|---|---|
| S0 | **39** |
| S1 | **40** |
| S2 | **41** |
| S3 | **42** |
| EN (both) | GND |

**MUX A — 16 pots** (10 k linear; ends to 3.3 V and GND, wiper to channel):
- SIG → **GPIO 1** (ADC1_CH0)

**MUX B — 16 momentary switches** (one side to channel, other side to GND):
- SIG → **GPIO 2** (internal pull-up enabled)

Channel n on MUX A = pot P(n+1); channel n on MUX B = switch SW(n+1) —
see `MIDI_MAP.md` for what each does.

## Power

Everything runs from the dev board's 3.3 V regulator except the display
backlight (~60 mA). If you add more LEDs, feed the display from 5 V/VIN
where the module supports it. Audio quality improves noticeably with a
clean supply to the PCM5102A — decouple with 10 µF + 100 nF at the module.

## Pin budget summary

| GPIO | Use | | GPIO | Use |
|---|---|---|---|---|
| 1 | pots (ADC) | | 14 | TFT RST |
| 2 | switches | | 15 | touch CLK |
| 3 | touch IRQ | | 16 | touch DIN |
| 4 | I2S LRCK | | 17 | MIDI OUT (TX1) |
| 5 | I2S BCK | | 18 | MIDI IN (RX1) |
| 6 | I2S DIN | | 21 | TFT backlight |
| 7 | SD CS (opt) | | 39–42 | mux S0–S3 |
| 8 | touch CS | | 47 | touch DO |
| 9 | TFT DC | | 48 | onboard RGB LED (free) |
| 10 | TFT CS | | 19/20 | native USB |
| 11 | TFT MOSI | | 43/44 | UART0 log |
| 12 | TFT SCK | | | |
| 13 | TFT MISO | | | |

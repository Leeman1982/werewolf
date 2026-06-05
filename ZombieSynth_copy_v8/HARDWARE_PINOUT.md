# ESP32-2432S028R (CYD) Hardware Pin Reference

## Verified Pinout for ZOMBIE SS Synthesizer

This document provides the **correct and verified** pin assignments for the ESP32-2432S028R "Cheap Yellow Display" board used in the ZOMBIE SS synthesizer project.

## Display & Touch (Built-in - DO NOT MODIFY)

### ILI9341 TFT Display (SPI/HSPI)
```
MISO:  GPIO 12
MOSI:  GPIO 13
SCLK:  GPIO 14
CS:    GPIO 15
DC:    GPIO 2
BL:    GPIO 21 (Backlight - active HIGH)
```

### XPT2046 Touchscreen (SPI)
```
CLK:   GPIO 25
MOSI:  GPIO 32
MISO:  GPIO 39
CS:    GPIO 33
IRQ:   GPIO 36
```

## Onboard Peripherals (Factory Connected)

### RGB LED
```
Red:   GPIO 4
Green: GPIO 16  ⚠️ CONFLICTS with typical MIDI RX usage!
Blue:  GPIO 17  ⚠️ CONFLICTS with I2S DOUT usage!
```

**Note:** To free GPIOs 4, 16, 17, physically remove the RGB LED from the board.

### Speaker Amplifier (P4 Connector)
```
SC8002B Amplifier Control: GPIO 26  ⚠️ CONFLICTS with I2S BCLK usage!
```

### Light Sensor (CDS Photoresistor)
```
LDR: GPIO 34 (Analog input for brightness)
```

## Available Expansion GPIOs

### CN1 Connector (Expansion Header)
These are the primary expansion GPIOs available without modification:
```
GPIO 35  (Input only - perfect for MIDI RX) ✅
GPIO 22  (I/O - shared with this connector) ✅
GPIO 27  (I/O - available) ✅
```

### P1 Connector
```
TX (GPIO 1)
RX (GPIO 3)
```
⚠️ **Avoid using these** - they're connected to USB serial for programming/debugging.

## ZOMBIE Synthesizer Pin Assignments

### I2S Audio Output (External DAC)
```
BCLK (Bit Clock):     GPIO 22  (CN1 connector)
LRCLK (Word Select):  GPIO 27  (CN1 connector)
DOUT (Data Out):      GPIO 17  ⚠️ REQUIRES RGB LED REMOVAL
```

**Alternative I2S Configuration (if RGB LED kept):**
If you want to keep the RGB LED, you could:
1. Use the onboard speaker amp via P4 (GPIO 26)
2. Or use bit-banged I2S on GPIOs 35, 22, 27 (limited but possible)

### MIDI Input (5-pin DIN)
```
MIDI RX:  GPIO 35  (Input only - via CN1 connector) ✅
```

**Optocoupler Circuit Required:**
```
MIDI Cable Pin 5 → 220Ω → 6N138 Pin 2
MIDI Cable Pin 4 → 6N138 Pin 3
6N138 Pin 6 → GPIO 35
6N138 Pin 5 → GND
6N138 Pin 8 → 3.3V
6N138 Pin 6 → 4.7kΩ → 3.3V (pull-up)
```

### USB MIDI
```
Native ESP32 USB (no additional hardware required)
Uses default Serial port
```

## PCM5052/PCM5102A DAC Connection

**Recommended I2S DAC Wiring (to CN1 connector):**
```
ESP32 (CN1) → PCM5102A Module
─────────────────────────────
3.3V        → VIN
GND         → GND
GPIO 22     → BCK (Bit Clock)
GPIO 27     → LCK (LRCLK/WS)
GPIO 17     → DIN (Data) [REQUIRES RGB LED REMOVAL]
```

**PCM5102A Module Configuration:**
- Connect FLT, DEMP, FMT to GND (I2S format)
- Connect SCK to GND (no external MCLK)
- Connect XMT to 3.3V (soft mute off)

## Alternative: MAX98357A I2S Amplifier

If using MAX98357A I2S Class-D amplifier:
```
ESP32 (CN1) → MAX98357A
─────────────────────────
3.3V        → VIN
GND         → GND
GPIO 22     → BCLK
GPIO 27     → LRCLK
GPIO 17     → DIN [REQUIRES RGB LED REMOVAL]
            → Connect SD pin to VIN for max volume
```

## Pin Usage Summary

| GPIO | Function (Original) | ZOMBIE Synth Use | Conflicts? | Available? |
|------|---------------------|------------------|------------|------------|
| 2    | TFT DC              | Display          | -          | ❌ NO      |
| 4    | RGB Red             | -                | RGB LED    | ⚠️ If removed |
| 12   | TFT MISO            | Display          | -          | ❌ NO      |
| 13   | TFT MOSI            | Display          | -          | ❌ NO      |
| 14   | TFT SCLK            | Display          | -          | ❌ NO      |
| 15   | TFT CS              | Display          | -          | ❌ NO      |
| 16   | RGB Green           | -                | RGB LED    | ⚠️ If removed |
| 17   | RGB Blue            | I2S DOUT         | RGB LED    | ⚠️ If removed |
| 21   | TFT Backlight       | Display          | -          | ❌ NO      |
| 22   | CN1 Expansion       | I2S BCLK         | -          | ✅ YES     |
| 25   | Touch CLK           | Touch            | -          | ❌ NO      |
| 26   | Speaker Amp         | -                | SC8002B    | ❌ NO      |
| 27   | CN1 Expansion       | I2S LRCLK        | -          | ✅ YES     |
| 32   | Touch MOSI          | Touch            | -          | ❌ NO      |
| 33   | Touch CS            | Touch            | -          | ❌ NO      |
| 34   | Light Sensor        | -                | LDR        | ❌ NO      |
| 35   | CN1 Expansion       | MIDI RX          | Input only | ✅ YES     |
| 36   | Touch IRQ           | Touch            | -          | ❌ NO      |
| 39   | Touch MISO          | Touch            | -          | ❌ NO      |

## Recommended Hardware Setup

### Option 1: Full Featured (RGB LED Removed)
- **Audio:** External I2S DAC on GPIO 22, 27, 17
- **MIDI:** 5-pin DIN on GPIO 35
- **USB MIDI:** Native support

### Option 2: Keep RGB LED (Limited Audio)
- **Audio:** Onboard speaker via P4 (GPIO 26), lower quality
- **MIDI:** 5-pin DIN on GPIO 35
- **USB MIDI:** Native support

### Option 3: Wireless Only
- **Audio:** Bluetooth audio (requires additional code)
- **MIDI:** USB MIDI only
- **Keep RGB LED for visual feedback**

## Important Warnings

1. **Do NOT connect external I2S to GPIO 26** - it's already used by the onboard amp!
2. **GPIO 35 is input only** - perfect for MIDI RX, but cannot be used for outputs
3. **GPIO 21 is the backlight** - disabling it will black out the display
4. **Removing RGB LED is recommended** for best audio quality (frees GPIO 17)

## Testing Your Hardware

### Test 1: Display & Touch
If the display and touch work with the original CYD examples, all core peripherals are correct.

### Test 2: MIDI Input
Use a serial monitor to verify MIDI bytes are received on GPIO 35 at 31250 baud.

### Test 3: I2S Audio
Connect an oscilloscope to verify:
- BCLK on GPIO 22 shows clock signal
- LRCLK on GPIO 27 shows L/R channel sync
- DOUT on GPIO 17 shows audio data

## Schematic Reference

For the complete schematic, see:
- [Mischianti's CYD Pinout](https://mischianti.org/esp32-2432s028-cheap-yellow-display-high-resolution-pinout-datasheet-schema-and-specs/)
- [Random Nerd Tutorials CYD Guide](https://randomnerdtutorials.com/esp32-cheap-yellow-display-cyd-pinout-esp32-2432s028r/)

## Questions?

If you encounter pin conflicts or audio issues, verify:
1. RGB LED status (present or removed?)
2. No external connections to GPIO 26
3. Proper I2S DAC module wiring
4. MIDI optocoupler powered correctly (3.3V logic)

---

**Last Updated:** Based on ESP32-2432S028R Rev 2.1 (most common version)

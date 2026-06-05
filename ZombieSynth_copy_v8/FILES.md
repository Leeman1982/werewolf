# ZOMBIE SS Synth - File List

This branch contains ONLY the ZOMBIE SS synthesizer files. All legacy CYD-MIDI-Controller modes have been removed.

## Essential Files (Copy ALL to Arduino)

### Main Application
- `ZombieSynth.ino` - Main sketch file

### Core Engine
- `synth_engine.h` - 8-voice polyBLEP synthesizer engine
- `arpeggiator_patterns.h` - 50-pattern arpeggiator
- `zombie_step_sequencer.h` - 16-step sequencer
- `midi_input.h` - 5-pin DIN MIDI input (GPIO 35)

### UI Modes
- `zombie_synth_mode.h` - Synth parameter control UI
- `zombie_arp_mode.h` - Arpeggiator pattern selector UI
- `zombie_seq_mode.h` - Step sequencer UI

### Framework
- `common_definitions.h` - ZOMBIE SS theme colors & shared definitions
- `ui_elements.h` - Touch handling and UI helpers
- `User_Setup.h` - TFT_eSPI display configuration

## Documentation
- `ZOMBIE_SYNTH_README.md` - Complete feature documentation
- `HARDWARE_PINOUT.md` - Verified GPIO pin assignments
- `ARDUINO_IDE_SETUP.md` - Arduino IDE setup instructions
- `copy_to_arduino.txt` - Quick checklist for file copying
- `README.md` - Original repository info

## Hardware Configuration

### Display & Touch (Built-in)
- ILI9341 TFT: GPIOs 2, 12-15, 21
- XPT2046 Touch: GPIOs 25, 32, 33, 36, 39

### Audio Output (External DAC)
- I2S BCLK: GPIO 22
- I2S LRCLK: GPIO 27
- I2S DOUT: GPIO 17 (requires RGB LED removal)

### MIDI Input
- 5-pin DIN RX: GPIO 35 (via optocoupler)

## What Was Removed

This clean branch removed all legacy modes from original CYD-MIDI-Controller:
- ❌ keyboard_mode.h
- ❌ sequencer_mode.h (old 808-style)
- ❌ bouncing_ball_mode.h
- ❌ physics_drop_mode.h
- ❌ random_generator_mode.h
- ❌ xy_pad_mode.h
- ❌ arpeggiator_mode.h (old version)
- ❌ grid_piano_mode.h
- ❌ auto_chord_mode.h
- ❌ lfo_mode.h
- ❌ midi_utils.h
- ❌ CYD-MIDI-Controller.ino (old main)

## Quick Start

1. **Copy all .h and .ino files** to Arduino sketch folder
2. **Copy User_Setup.h** to TFT_eSPI library folder
3. **Install libraries:**
   - TFT_eSPI (by Bodmer)
   - XPT2046_Touchscreen (by Paul Stoffregen)
4. **Select board:** ESP32 Dev Module
5. **Upload!**

See `ARDUINO_IDE_SETUP.md` for detailed instructions.

## File Sizes
```
Total code: ~2,700 lines (excluding docs)
- synth_engine.h:         ~600 lines
- arpeggiator_patterns.h: ~400 lines
- zombie_step_sequencer.h:~240 lines
- zombie_synth_mode.h:    ~420 lines
- ZombieSynth.ino:        ~320 lines
- Other files:            ~720 lines
```

## Branch Info
- **Branch:** `claude/zombie-synth-clean-rEZb9`
- **Base:** ESP32-2432S028R (CYD)
- **Focus:** Clean, professional synthesizer only
- **No bloat:** Zero legacy modes or unused code

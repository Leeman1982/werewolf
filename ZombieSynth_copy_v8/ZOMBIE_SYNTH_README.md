# ZOMBIE SS PROPHET SYNTHESIZER

![Version](https://img.shields.io/badge/version-1.0-red)
![Platform](https://img.shields.io/badge/platform-ESP32-blue)
![License](https://img.shields.io/badge/license-MIT-green)

A fully-featured Prophet-8 inspired polyphonic synthesizer for the ESP32 Cheap Yellow Display (CYD), with ZOMBIE SS themed UI.

## Features

### Synthesizer Engine
- **8-voice polyphony** with voice stealing algorithm
- **PolyBLEP oscillators** for bandlimited, alias-free waveforms:
  - Sawtooth
  - Square
  - Triangle
  - Sine
  - Pulse (variable width)
- **Dual oscillators per voice** with detune
- **State variable filter** with multiple modes:
  - Lowpass
  - Highpass
  - Bandpass
  - Notch
- **Envelope control:**
  - Full ADSR amplitude envelope
  - Full ADSR filter envelope with cutoff modulation
- **Master volume control**

### Arpeggiator
- **50 unique arpeggiator patterns** including:
  - Classic patterns (Up, Down, Up/Down, Random)
  - Interval patterns (Thirds, Fourths, Octaves)
  - Rhythmic patterns (Triplets, Dotted, Swing)
  - Euclidean rhythms (5/8, 7/12)
  - Polyrhythms (3/4, 5/4)
  - Advanced patterns (Fibonacci, Chaos, Fractal)
- **BPM control** (30-300 BPM)
- **Octave range** (1-4 octaves)
- **Gate length control**

### Step Sequencer
- **16-step sequencer** with 4 independent tracks
- **Per-step control:**
  - Note selection
  - Velocity
  - Gate length
  - Tie/legato
- **Pattern operations:**
  - Clear track
  - Randomize track
  - Shift left/right
  - Reverse
- **BPM and swing control**
- **Real-time playback** with visual feedback

### MIDI Input
- **USB MIDI** via ESP32 native USB
- **5-pin DIN MIDI** via hardware serial (GPIO 35 - input only, perfect for RX)
- **Full MIDI implementation:**
  - Note On/Off
  - Control Change (CC74: Filter Cutoff, CC71: Resonance, CC7: Volume)
  - Pitch Bend (ready for implementation)

### Audio Output
- **I2S audio output** configured for external PCM5052 DAC
- **44.1 kHz sample rate**, 16-bit stereo
- **GPIO pin assignment (verified for CYD ESP32-2432S028R):**
  - GPIO 22: Bit Clock (BCLK) - via CN1 connector
  - GPIO 27: Word Select (LRCLK/WS) - via CN1 connector
  - GPIO 17: Data Output (DOUT) - requires RGB LED removal or conflicts

**Note:** The onboard P4 speaker connector uses GPIO 26 for the SC8002B amplifier. For external I2S DAC, use the pins above.

### User Interface
- **ZOMBIE SS themed UI:**
  - Black background
  - Red text
  - White outlines
  - German WW2 style typography
- **Touch-based control** with XPT2046 touchscreen
- **Multiple parameter pages:**
  - Oscillators
  - Filter
  - Amp Envelope
  - Filter Envelope
- **Real-time visual feedback**
- **Voice count display**

## Hardware Requirements

### ESP32 CYD Board
- **Model:** ESP32-2432S028R "Cheap Yellow Display"
- **Display:** 240x320 ILI9341 TFT
- **Touch:** XPT2046 resistive touchscreen
- **Processor:** Dual-core ESP32 @ 240MHz

### Audio DAC (Required for Sound Output)
- **External I2S DAC** like PCM5102A or MAX98357A
- Connect via CN1 expansion connector:
  - GPIO 22 → BCLK
  - GPIO 27 → LRCLK/WS
  - GPIO 17 → DIN (may require RGB LED removal)
- **Important:** GPIO 26 is used by onboard speaker amp, don't use for I2S!
- Alternative options:
  - Use onboard speaker (P4 connector with SC8002B amp on GPIO 26)
  - Remove RGB LED to free GPIO 4, 16, 17 for more flexibility

### MIDI Input
- **5-pin DIN MIDI:** Requires optocoupler circuit on GPIO 35
  - Standard MIDI input circuit with 6N138 or similar optocoupler
  - Connect MIDI RX to GPIO 35 (input-only pin, perfect for MIDI)
  - **Do NOT use GPIO 16** - it's connected to RGB LED (green)
- **USB MIDI:** Works natively via USB port (no extra hardware needed)

## Software Requirements

### Arduino IDE Setup
1. **Install ESP32 Board Support:**
   - Add to Board Manager URLs: `https://dl.espressif.com/dl/package_esp32_index.json`
   - Install "ESP32 by Espressif Systems"

2. **Install Required Libraries:**
   ```
   TFT_eSPI (by Bodmer)
   XPT2046_Touchscreen (by Paul Stoffregen)
   ESP32-USB-MIDI (if using USB MIDI)
   ```

3. **Configure TFT_eSPI:**
   - Copy `User_Setup.h` to Arduino libraries folder: `libraries/TFT_eSPI/User_Setup.h`
   - This file is already configured for CYD board

4. **Board Settings:**
   - Board: "ESP32 Dev Module"
   - Upload Speed: 115200
   - Flash Frequency: 80MHz
   - Flash Mode: QIO
   - Flash Size: 4MB
   - Partition Scheme: Default 4MB with spiffs

## Building and Flashing

### Arduino IDE
1. Open `ZombieSynth.ino` in Arduino IDE
2. Select your ESP32 board and COM port
3. Click Upload

### PlatformIO (Alternative)
```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
lib_deps =
    bodmer/TFT_eSPI@^2.5.0
    paulstoffregen/XPT2046_Touchscreen@^1.4
monitor_speed = 115200
```

## Usage

### Navigation
1. **Power on** - ZOMBIE SS splash screen appears
2. **Main Menu** - Select from 3 modes:
   - **SYNTH** - Main synthesizer control
   - **ARP** - Arpeggiator with pattern selection
   - **SEQ** - 16-step sequencer

3. **BACK button** - Tap top-left to return to menu

### Synth Mode
- **Page tabs:** Tap to switch between OSC, FLTR, AMP, F.ENV
- **Oscillators page:**
  - Sliders control oscillator levels and master volume
  - Buttons cycle through waveforms
- **Filter page:**
  - Control cutoff, resonance, and envelope amount
  - Switch between filter types (LP/HP/BP/Notch)
- **Envelope pages:**
  - Adjust Attack, Decay, Sustain, Release for amp and filter

### Arpeggiator Mode
- **Pattern list:** 50 patterns displayed 5 per page
- **Tap pattern** to select
- **< PG / > PG** - Navigate between pattern pages
- **PLAY/STOP** - Start/stop arpeggiator
- **Play notes via MIDI** to trigger arpeggio

### Sequencer Mode
- **Track buttons (T1-T4):** Select track to edit
- **16-step grid:** Tap steps to toggle on/off
- **PLAY/STOP** - Start/stop sequencer
- **CLEAR** - Clear current track
- **RAND** - Randomize current track
- **< BPM / BPM >** - Adjust tempo

### MIDI Control
Send MIDI notes to the synth:
- **USB MIDI:** Connect via USB cable
- **5-pin DIN:** Connect MIDI controller to GPIO 16 circuit

MIDI CC Mapping:
- CC 74: Filter Cutoff
- CC 71: Filter Resonance
- CC 7: Master Volume

## Architecture

### Dual-Core Processing
- **Core 1:** UI rendering, touch handling, MIDI input
- **Core 0:** Real-time audio synthesis (high priority FreeRTOS task)

### File Structure
```
ZombieSynth.ino                 - Main application
synth_engine.h                  - PolyBLEP synth engine
arpeggiator_patterns.h          - 50-pattern arpeggiator
zombie_step_sequencer.h         - 16-step sequencer
midi_input.h                    - USB and DIN MIDI input
zombie_synth_mode.h             - Synth UI mode
zombie_arp_mode.h               - Arpeggiator UI mode
zombie_seq_mode.h               - Sequencer UI mode
common_definitions.h            - Shared definitions and theme
ui_elements.h                   - UI helper functions
User_Setup.h                    - TFT_eSPI configuration
```

### Audio Pipeline
```
MIDI Input → Note On/Off → Voice Allocation →
Oscillators → Filter → Envelopes → Mixing →
I2S Output (PCM5052 DAC)
```

## Performance

- **Audio latency:** ~5ms (buffer-dependent)
- **Polyphony:** 8 voices simultaneous
- **Sample rate:** 44.1 kHz, 16-bit
- **CPU usage:** ~60% Core 0 (audio), ~30% Core 1 (UI)
- **UI refresh rate:** 50 Hz (20ms)

## Troubleshooting

### No Audio Output
1. Verify I2S DAC connections (GPIO 26, 27, 22)
2. Check PCM5052 power supply (3.3V or 5V depending on module)
3. Monitor serial output for initialization errors

### MIDI Not Working
1. **USB MIDI:** Check USB cable and driver installation
2. **DIN MIDI:** Verify optocoupler circuit and GPIO 16 connection
3. Test with simple MIDI monitor first

### Touch Not Responding
1. Verify XPT2046 SPI connections
2. Check touch calibration values in code
3. Try different touch pressure

### Display Issues
1. Ensure `User_Setup.h` is correctly installed in TFT_eSPI library
2. Check SPI frequency settings
3. Verify display power and connections

## Future Enhancements

- [ ] LFO modulation routing
- [ ] Effects (chorus, delay, reverb)
- [ ] Patch save/load to SPIFFS
- [ ] MIDI clock sync
- [ ] Additional oscillator waveforms
- [ ] Polyphonic aftertouch support
- [ ] SD card pattern storage

## Credits

- **PolyBLEP Algorithm:** Based on research by Välimäki et al.
- **Prophet-8 Inspiration:** Sequential/Dave Smith Instruments
- **CYD Hardware:** Espressif ESP32 community
- **UI Framework:** TFT_eSPI by Bodmer

## License

MIT License - See LICENSE file for details

## Author

Created with Claude Code
ZOMBIE SS themed synthesizer project

---

**WARNING:** This synthesizer can produce loud audio. Start with low volume and increase gradually. Always use hearing protection when testing audio equipment.

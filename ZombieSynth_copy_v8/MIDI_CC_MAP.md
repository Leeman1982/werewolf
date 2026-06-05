# ZOMBIE SS MIDI CC Control Map

All synth parameters can be controlled via MIDI CC messages from external controllers.

## Filter Controls
```
CC 74  - Filter Cutoff     (0-127 → 0.0-1.0)
CC 71  - Filter Resonance  (0-127 → 0.0-1.0)
```

## Volume Controls
```
CC 7   - Master Volume     (0-127 → 0.0-1.0)
CC 12  - OSC1 Level        (0-127 → 0.0-1.0)
CC 13  - OSC2 Level        (0-127 → 0.0-1.0)
```

## Amp Envelope (ADSR)
```
CC 73  - Amp Attack        (0-127 → 0.0-2.0 seconds)
CC 75  - Amp Decay         (0-127 → 0.0-2.0 seconds)
CC 70  - Amp Sustain       (0-127 → 0.0-1.0 level)
CC 72  - Amp Release       (0-127 → 0.0-2.0 seconds)
```

## Filter Envelope (ADSR)
```
CC 76  - Filter Attack     (0-127 → 0.0-2.0 seconds)
CC 77  - Filter Decay      (0-127 → 0.0-2.0 seconds)
CC 78  - Filter Sustain    (0-127 → 0.0-1.0 level)
CC 79  - Filter Release    (0-127 → 0.0-2.0 seconds)
```

## Arpeggiator Controls
```
CC 80  - Arp BPM           (0-127 → 30-300 BPM)
CC 81  - Arp Pattern       (0-127 → Pattern 0-49)
CC 82  - Arp Octave Range  (0-127 → 1-4 octaves)
CC 83  - Arp Gate Length   (0-127 → 10-100%)
```

## Sequencer Controls
```
CC 85  - Sequencer BPM     (0-127 → 40-300 BPM)
CC 86  - Sequencer Swing   (0-127 → 50-75%)
```

## MIDI Note Control

### Direct Synth Play
- Send MIDI Note On → Synth plays note immediately
- Send MIDI Note Off → Synth releases note

### Arpeggiator Mode
- **Arpeggiator follows your playing!**
- Send MIDI Note On → Note is added to arp pattern
- Send MIDI Note Off → Note is removed from arp pattern
- Arpeggiator plays the held notes in selected pattern
- Works with any MIDI keyboard or controller

Example:
1. Select arpeggiator mode on CYD
2. Choose pattern (e.g., "Up")
3. Press C, E, G on your MIDI keyboard
4. Arpeggiator plays C-E-G pattern ascending
5. Release G, now plays C-E
6. Add D, now plays C-D-E

### Sequencer Mode
- Sequencer plays independently
- Can still trigger synth with MIDI notes while sequencer runs

## Usage Examples

### With MIDI Controller Keyboard
```
1. Connect MIDI keyboard to GPIO 35 (5-pin DIN)
2. Play notes → Synth responds
3. Switch to arpeggiator mode
4. Hold chord → Arp plays pattern
5. Change pattern with CC 81
```

### With DAW (Ableton, FL Studio, etc.)
```
1. Connect CYD via USB MIDI
2. Create MIDI track
3. Set destination to "ZOMBIE SS"
4. Send CC messages to control parameters
5. Send notes to play synth/arpeggiator
```

### With Hardware Controller (Novation, Arturia, etc.)
```
1. Map knobs/sliders to CC numbers above
2. Real-time control of all parameters
3. Example mappings:
   - Knob 1: CC 74 (Filter Cutoff)
   - Knob 2: CC 71 (Resonance)
   - Knob 3: CC 7 (Volume)
   - Knob 4: CC 80 (Arp BPM)
```

## Standard MIDI CC Reference

For compatibility with common controllers:
- **CC 7** (Volume) - Standard master volume
- **CC 70** (Sound Controller 1) - Sustain level
- **CC 71** (Sound Controller 2) - Resonance
- **CC 72** (Sound Controller 3) - Release time
- **CC 73** (Sound Controller 4) - Attack time
- **CC 74** (Sound Controller 5) - Cutoff frequency
- **CC 75** (Sound Controller 6) - Decay time
- **CC 76-79** (Sound Controllers 7-10) - Filter envelope

## Testing MIDI CC

Use this command to test from serial monitor:
```cpp
// Send CC message
onMIDICC(0, 74, 127);  // Set filter cutoff to max
onMIDICC(0, 80, 64);   // Set arp BPM to ~165
```

## Tip: Controller Templates

Many MIDI controllers have templates for popular synths.
Map your controller using the CC numbers above for
full hands-on control of ZOMBIE SS synth!

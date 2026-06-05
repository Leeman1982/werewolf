# ZOMBIE Synth - Arduino IDE Setup Instructions

## Method 1: Copy All Files (Recommended)

Copy ALL these files from the repository to your Arduino sketch folder:

**Required Files:**
```
C:\Users\Default.DESKTOP-QO1EVKV\Documents\Arduino\ZombieSynth\
├── ZombieSynth.ino              (main sketch)
├── common_definitions.h          (ZOMBIE theme colors)
├── ui_elements.h                 (UI helpers)
├── synth_engine.h                (polyBLEP synth engine)
├── arpeggiator_patterns.h        (50-pattern arpeggiator)
├── zombie_step_sequencer.h       (step sequencer)
├── midi_input.h                  (MIDI input handler)
├── zombie_synth_mode.h           (synth UI)
├── zombie_arp_mode.h             (arpeggiator UI)
├── zombie_seq_mode.h             (sequencer UI)
└── User_Setup.h                  (TFT_eSPI config)
```

**Steps:**

1. **Close Arduino IDE** if it's open

2. **Copy files from repository:**
   - Navigate to your cloned repository folder
   - Copy ALL the .h files listed above
   - Also copy `ZombieSynth.ino`

3. **Paste into Arduino sketch folder:**
   - Navigate to: `C:\Users\Default.DESKTOP-QO1EVKV\Documents\Arduino\ZombieSynth\`
   - Paste all the files (overwrite if asked)

4. **Configure TFT_eSPI library:**
   - Copy `User_Setup.h` to:
     `C:\Users\Default.DESKTOP-QO1EVKV\Documents\Arduino\libraries\TFT_eSPI\User_Setup.h`
   - Overwrite the existing file

5. **Open Arduino IDE:**
   - Open `ZombieSynth.ino` from the sketch folder
   - All tabs should appear at the top (one for each .h file)

6. **Select board:**
   - Tools → Board → ESP32 Arduino → "ESP32 Dev Module"
   - Tools → Port → (select your COM port)

7. **Upload!**

## Method 2: Open Directly from Git Repository

If you want to work directly from the git repository:

1. **In Arduino IDE:**
   - File → Open
   - Navigate to your git repository folder
   - Open `ZombieSynth.ino`
   - All header files should be detected automatically

2. **Configure TFT_eSPI:**
   - Still need to copy `User_Setup.h` to the library folder (see step 4 above)

## Troubleshooting

### Error: "No such file or directory"
- Make sure ALL .h files are in the same folder as ZombieSynth.ino
- Check that you didn't create a subfolder

### Error: "TFT_eSPI.h: No such file or directory"
- Install TFT_eSPI library: Tools → Manage Libraries → search "TFT_eSPI"
- Install by Bodmer

### Error: "XPT2046_Touchscreen.h: No such file or directory"
- Install XPT2046 library: Tools → Manage Libraries → search "XPT2046"
- Install by Paul Stoffregen

### Error: "USB.h: No such file or directory"
- Install ESP32 board support if not already done
- Boards Manager → Search "esp32" → Install "ESP32 by Espressif Systems"

### Error: Multiple definition errors
- Make sure you're not mixing files from different sketches
- Clean build: Sketch → "Clean" (if available) or delete build cache

## Quick Test

After setup, verify all files are found:
1. Open ZombieSynth.ino
2. Click verify (checkmark icon)
3. Should see: "Compiling sketch..." then "Done compiling"
4. If errors appear, check which file is missing and copy it

## File Checklist

Use this checklist to verify all files are present:

- [ ] ZombieSynth.ino
- [ ] common_definitions.h
- [ ] ui_elements.h
- [ ] synth_engine.h
- [ ] arpeggiator_patterns.h
- [ ] zombie_step_sequencer.h
- [ ] midi_input.h
- [ ] zombie_synth_mode.h
- [ ] zombie_arp_mode.h
- [ ] zombie_seq_mode.h
- [ ] User_Setup.h (in both sketch folder AND TFT_eSPI library folder)

All files should show as tabs in Arduino IDE when you open the sketch.

# WEREWOLF — AMY Synth + GM SoundFont Workstation (ESP32-S3)

Port of the werewolf/ZombieSynth sequencer & synth to an **ESP32-S3 WROOM
dev board** with a **3.2" ILI9341 SPI touch screen**, replacing the original
PolyBLEP engine with the full **[AMY synthesizer engine](https://github.com/shorepine/amy)**
and adding a separate **16-track General MIDI sequencer** playing **embedded
SoundFonts** (Medusa sequencer engine).

```
                    ┌───────────────────────────────┐
 DIN MIDI IN ──────►│                               │────► DIN MIDI OUT
 USB MIDI IN ──────►│        6 x 4 MIDI ROUTER      │────► USB MIDI OUT
 ARP / SEQs  ──────►│                               │
 TOUCH KEYS  ──────►│      ┌───────────┬────────┐   │
                    └──────►  WOLF     │  GM    ◄───┘
                           │  (AMY)    │ (SF2)  │
                           └─────┬─────┴───┬────┘
                                 └── mix ──┘
                                     │ I2S
                                 PCM5102A DAC
```

## Features

### WOLF synth (AMY engine)
- **Fully controllable 3-oscillator voice**, 8-voice polyphonic:
  - per-osc wave (sine/square/saw↓/saw↑/tri/noise), level, octave,
    semitone, fine detune (cents), pulse width
  - shared LP/BP/HP/LP24 filter with resonance, key tracking and its own ADSR
  - amp ADSR, LFO (5 waves) routable to filter / pitch / amp / PWM
  - glide (portamento), reverb / chorus / echo sends
- **20 custom werewolf presets** + curated highlights, **plus the entire AMY
  factory bank**: 128 Juno-106 patches (0–127) and 128 DX7 patches (128–255),
  browsable by number
- 8 user preset slots in flash (NVS)

### GM section (embedded SoundFonts, Medusa engine)
- **16-track / 16-step GM sequencer** — one track per GM channel, channel 10
  = drums (drum-name step editing)
- Per step: note, velocity, gate, probability, accent, **slide/tie**
- Per track: GM program (0–127 with names), level, per-track **polymeter
  length**, transpose, mute
- 8 patterns + **pattern chaining** (Medusa chains), swing
- GM mixer page (16 levels/mutes)
- SoundFont engine: TinySoundFont, patched to stream **int16 samples straight
  from a memory-mapped 8 MB flash partition** — a 7.5 MB GM bank costs
  **zero RAM**. Two GM banks are included in `soundfonts/`.

### WOLF sequencer + arpeggiator (ZombieSynth ports)
- 4-track × 16-step sequencer, 8 patterns, swing, probability, accent,
  per-track channel/transpose/mute, pattern ops (clear/random/shift/reverse)
- 50-pattern arpeggiator with octave range, gate, latch and clock divisions
  (1/8 … 1/32, triplets, dotted)

### MIDI routing (lots of options)
- 6 sources × 4 destinations **routing matrix**, editable on screen:
  DIN IN / USB IN / WOLF SEQ / GM SEQ / ARP / LOCAL →
  WOLF / GM / DIN OUT / USB OUT
- Clock: internal (48 PPQ master clock), or **external sync** from DIN or USB
  MIDI clock; MIDI clock + transport out per port
- WOLF receive channel (omni or 1–16), LOCAL channel, panic
- Panel pots double as a MIDI CC controller (see `MIDI_MAP.md`)

### Hardware control
- **16 potentiometers** (CD74HC4067 mux) with soft-takeover, mapped to the
  most-used synth params
- **16 momentary switches** (second CD74HC4067): waves, filter, arp, transport,
  tap-tempo, preset step, shift, panic
- Full map in `MIDI_MAP.md`, wiring in `WIRING.md`

## Hardware

| Part | Notes |
|---|---|
| ESP32-S3-WROOM DevKitC | **N16R8** (16 MB flash / 8 MB PSRAM) recommended |
| 3.2" ILI9341 320×240 SPI touch | XPT2046 resistive touch, 14-pin header |
| PCM5102A I2S DAC module | SCK→GND, FLT/DEMP/FMT→GND, XSMT→3.3 V |
| 2 × CD74HC4067 | 16-ch analog multiplexers |
| 16 × 10 k linear pots, 16 × momentary switches | |
| 6N138 optocoupler + DIN sockets | MIDI in/out circuits (see WIRING.md) |

Full pinout: **`WIRING.md`**.

## Building

### PlatformIO (recommended)
```bash
cd WerewolfAmyS3
pio run -t upload          # builds and flashes the firmware
scripts/flash_soundfont.sh soundfonts/MIRACLE.sf2 /dev/ttyUSB0
```

### Arduino IDE
1. Install **ESP32 board support** (Espressif) and select
   **ESP32S3 Dev Module** with:
   - Flash Size: **16 MB**, PSRAM: **OPI PSRAM**
   - Partition Scheme: **Custom** — copy `partitions_werewolf_16MB.csv`
     (see note below)
   - USB Mode: **USB-OTG (TinyUSB)** ← required for USB MIDI
2. Install libraries: **TFT_eSPI** (Bodmer), **XPT2046_Touchscreen**
   (P. Stoffregen), **AMY Synthesizer** (shorepine/amy, or Library Manager)
3. Copy the provided `User_Setup.h` over `libraries/TFT_eSPI/User_Setup.h`
4. Open `WerewolfAmyS3.ino`, build, upload
5. Flash a soundfont: `scripts/flash_soundfont.sh soundfonts/MIRACLE.sf2`

> Arduino IDE custom partitions: copy `partitions_werewolf_16MB.csv` into the
> sketch folder as `partitions.csv` and pick "Custom" — or add an entry to
> `boards.local.txt`. PlatformIO handles it automatically.

### Embedded SoundFonts
Two GM banks ship in `soundfonts/`:
- `MIRACLE.sf2` (7.3 MB, 637 presets — GM + variation banks)
- `Creative_Labs_8MB_GM.sf2` (7.4 MB, 138 presets — classic E-mu 8 MB GM)

Flash either one into the `sf2` partition (offset `0x700000`):
```bash
scripts/flash_soundfont.sh soundfonts/MIRACLE.sf2 [port]
```
Any GM-compatible SF2 up to 8 MB works. The firmware memory-maps the
partition and verifies the RIFF header at boot (see the SETUP page).

## UI map

- **MENU** → WLF (synth editor) · PRE (presets) · ARP · WSQ (wolf seq) ·
  GM (GM seq) · MIX (GM mixer) · RTE (routing) · SET (setup)
- **WOLF** — tabs OSC / MIX / FILT / ENV / LFO + one-octave touch keyboard
- **GM SEQ** — 16 track tabs, program picker, step grid, step params
  (note/vel/gate/prob/accent/slide), per-track length, swing, chain editor
- **ROUTE** — tap matrix cells to connect sources to destinations

## Repo layout

```
WerewolfAmyS3.ino      main sketch: setup, tasks, mode dispatch
config.h               pin map + engine sizes
wolf_synth.h           3-osc AMY voice (memory patch + live edits)
wolf_presets.h         preset bank
gm_engine.h            TinySoundFont wrapper (flash-mapped SF2)
gm_seq.h               16-track GM sequencer (Medusa engine)
wolf_seq.h             4-track wolf sequencer (ZombieSynth port)
arp_engine.h           50-pattern arpeggiator (ZombieSynth port)
arpeggiator_patterns.h ZombieSynth pattern bank
midi_router.h          6x4 routing matrix, clock, USB/DIN I/O
clock.h                48 PPQ master clock (AMY sequencer hook)
controls.h             CD74HC4067 pots/switches scanning
storage.h              NVS persistence
ui_common.h / theme.h  touch + widgets + theme
mode_*.h               UI pages
tsf.h / tml.h          TinySoundFont (patched: TSF_INT16_SAMPLES)
```

## Credits
- [AMY](https://github.com/shorepine/amy) — Brian Whitman & DAn Ellis
  (shore pine sound systems)
- [TinySoundFont](https://github.com/schellingb/TinySoundFont) — Bernhard
  Schelling (MIT, see `LICENSE.tinysoundfont`); local patch adds
  `TSF_INT16_SAMPLES` zero-copy flash playback
- ZombieSynth (werewolf repo) — original sequencer/synth/arp UI concepts
- Medusa sequencer — pattern/chain/slide sequencer engine

#pragma once
// ============================================================================
//  Carpenter Synth — config.h
//  Board : Raspberry Pi Pico 2 / RP2350, 16 MB flash (arduino-pico core)
//  Audio : 17-engine multi-synth (up to 10-voice each, one live at a time,
//          see engine.h) -> I2S -> PCM5102
//  Disp  : 128x64 OLED (SSD1309) on hardware I2C
//  Input : 1x rotary encoder w/ push + 5x momentary switches + 2x
//          performance switches (GP2/GP3) + 2x analog pots (GP26/GP27)
//
//  CS-80 / New York Ripper hardware edition.  Exact port of the main xtro
//  board: same pinout, same OLED, same encoder-and-five-buttons panel, same
//  two performance switches and two pots — see that project's own config.h,
//  which this one is a line-for-line hardware match of.  Only the synthesis
//  side differs, and only in that this build carries seventeen selectable
//  engines (engine.h) instead of one fixed voice.  Nothing below the
//  "Audio" heading changed in this port: the DSP, every engine constant and
//  the whole effects/sequencer/storage layout are byte-for-byte what the
//  240x240 build ran.  MIDI keeps its original pins untouched — see the
//  MIDI section below.
// ============================================================================

#include <Arduino.h>

// ─── I2S audio out -> PCM5102 ───────────────────────────────────────────────
// The I2S PIO needs BCLK and LRCLK on consecutive GPIOs (LRCLK = BCLK + 1).
// PCM5102 wiring:  BCK<-GP15  LRCK<-GP16  DIN<-GP17  SCK->GND (internal PLL)
//                  FLT/DEMP/FMT to GND, XSMT to 3V3.
#define PIN_I2S_BCLK   15      // -> PCM5102 BCK   (LRCK is BCLK+1 = GP16)
#define PIN_I2S_DOUT   17      // -> PCM5102 DIN

// ─── 128x64 OLED over hardware I2C ──────────────────────────────────────────
// SSD1309 — same panel and driver as the CS-80/NYR board. Driven with U8g2
// (see ui.h's SynthOled typedef) rather than a hand-rolled register-bang
// driver or the old ST7789/Adafruit-GFX canvas shim.
#define OLED_DRIVER_SSD1309
// #define OLED_DRIVER_SH1106
// #define OLED_DRIVER_SSD1306
#define PIN_OLED_SDA    4
#define PIN_OLED_SCL    5
#define OLED_ADDR      0x3C
#define OLED_I2C_HZ    1000000     // 400000 is the safe fallback

// ─── Rotary encoder (EC11 w/ push) ──────────────────────────────────────────
#define PIN_ENC_A       6
#define PIN_ENC_B       7
#define PIN_ENC_SW      8
#define ENC_TICKS_PER_DETENT 4

// ─── 5 momentary switches (active-low to GND, internal pull-ups) ────────────
#define PIN_BTN_PLAY    9
#define PIN_BTN_SHIFT  10
#define PIN_BTN_PAGE   11
#define PIN_BTN_TRACK  12
#define PIN_BTN_MUTE   13
#define DEBOUNCE_MS    15
#define LONG_PRESS_MS  600

#define PIN_CLICK_OUT  14      // metronome / gate out

// ─── 2 performance momentary switches (active-low to GND, internal pull-ups) ─
// OCT DOWN / OCT UP: quick transpose for an external MIDI keyboard on MIDI IN,
// independent of the sequencer's own patterns, which are left untouched.
#define PIN_BTN_OCT_DOWN 2
#define PIN_BTN_OCT_UP    3

// ─── 2 analog pots on the ADC-capable pins (GP26/27 = ADC0/ADC1) ────────────
// Hands-on hardware knobs for the two controls most worth having under a
// finger during a performance: filter cutoff and resonance, mapped through
// engineFilterCutoffParamId()/engineFilterResonanceParamId() (engine.h) so
// they always reach whichever engine is currently live. Continuously read
// and applied directly (see controls.h's Pot class) — turning the knob
// always wins, the same as a real analogue synth's non-motorized panel.
#define PIN_POT_CUTOFF     26
#define PIN_POT_RESONANCE  27
#define POT_ADC_BITS       12
#define POT_ADC_MAX        4095.0f
#define POT_DEADBAND       (1.0f / 400.0f)   // ~0.25% of travel before re-applying

// ─── MIDI: OUT on GP1, IN on GP0, via PIO UART ──────────────────────────────
#define PIN_MIDI_TX     1
#define PIN_MIDI_RX     0
#define MIDI_BAUD   31250
#define MIDI_USE_PIO_UART 1

// ─── CPU clock ──────────────────────────────────────────────────────────────
// Set from the Arduino IDE:  Tools -> CPU Speed.
//
//   150 MHz  — the stock default.  Guaranteed to boot on any RP2350; the CPU
//              guard simply runs fewer voices.  START HERE if anything is odd.
//   250 MHz  — the tested target for the full MAX_VOICES = 8 polyphony.
//
// Nothing in this firmware requires 250 MHz: core1 measures its own render time
// and lowers the voice ceiling if it runs short, so an underclocked board plays
// correctly with less polyphony rather than glitching.  Check the PERF page.
//
// Do NOT re-clock at runtime.  The core's main() runs set_sys_clock_khz(F_CPU),
// then brings USB up, then launches core1, and only then calls setup() — so
// re-clocking inside setup() moves the clock out from under an enumerated USB
// peripheral and a running core1.

// ─── Audio ──────────────────────────────────────────────────────────────────
#define SAMPLE_RATE       48000
#define SAMPLE_RATE_F     48000.0f
#define INV_SAMPLE_RATE   (1.0f / SAMPLE_RATE_F)
#define AUDIO_BLOCK       64      // samples rendered per pass on core1 (1.33 ms)
// How many AUDIO_BLOCKs of DMA cushion the I2S driver keeps queued.  Costs
// AUDIO_BLOCK*4 bytes each (16 x 64 x 4 = 4 KB) and buys ~21 ms of slack, so a
// flash erase or a heavy LCD redraw on core0 can't punch a hole in the output.
#define I2S_BUFFERS       16

// Shared voice allocator ceiling — sized to the widest engine's polyphony,
// which is the Prophet-10 (two 5-voice banks), not the CS-80's own 8.
#define MAX_VOICES        10      // hard ceiling; the CPU guard may lower it live
#define CONTROL_RATE_DIV  8       // filter coeffs / mod recomputed every N samples

// ─── CS-80 voice architecture ───────────────────────────────────────────────
// Every one of these is a direct port from the multi-engine build's own
// config.h — the CS-80 engine (engine_cs80.*/cs80_voice.*/cs80_params.*) is
// "moved verbatim", so its constants move verbatim with it.
//
// The real instrument is strictly 8-note polyphonic, and every note triggers
// BOTH channels — 16 complete signal paths.  That pairing is the instrument;
// it is not a "layer" feature bolted on top, so it is not optional here either.
#define CS_VOICES         8
#define CS_CHANNELS       2       // I and II

// The sawtooth reset glitch: a one-shot pulse at the start of every cycle,
// ~30 us wide and ~0.45 V against a ~10 V pk-pk saw.  Frequency-independent in
// TIME, which is why it is specified in samples rather than in phase — at
// 48 kHz it is 1.44 samples wide no matter what note is playing.  This is a
// large part of the CS-80's bite and is deliberately not band-limited.
#define CS_GLITCH_US      30.0f
#define CS_GLITCH_SAMPLES (CS_GLITCH_US * 1e-6f * SAMPLE_RATE_F)
#define CS_GLITCH_AMP     0.09f   // 0.45 V of 10 V pk-pk, on a +/-1 saw

// Filter damping law.  k = 1/Q, and it NEVER reaches zero: the CS filter
// achieves resonance by reducing damping rather than by positive feedback, so
// it cannot self-oscillate.  The floor also rises with cutoff, which is why Q
// falls as the filter opens and why a CS never turns shrill up top.
#define CS_K_FLOOR        0.22f   // damping floor at the bottom of the range
#define CS_K_SLOPE        0.58f   // ...rising this much by the top
#define CS_K_OPEN         2.0f    // damping with resonance at zero (Q = 0.5)

// The HPF and LPF sections don't share one exponential converter on the real
// instrument: a technical teardown of the two VCF chips found the highpass
// section (SVF1, processed first -- see cs80SvfProcess's HP-feeds-LP order in
// engine_cs80.cpp) tracking control voltage at 2V/octave against the lowpass
// section (SVF2)'s standard 1V/octave -- i.e. the HPF needs twice the CV for
// the same pitch-domain sweep, so it moves at half the LPF's rate for any
// shared modulation source. Applied only to the modulation bus (envelope,
// LFO, brilliance, drift, velocity/aftertouch brightness); the two CUTOFF
// knobs themselves (hpIdx/lpIdx) stay independent, as they already were.
#define CS_HPF_TRACK_SCALE 0.5f

// Gentle fixed single-pole low-pass on the overall filter response.
#define CS_FIXED_LP_HZ    7600.0f
// Linear cutoff offset — the filter never closes below this.
#define CS_MIN_CUTOFF_HZ  23.0f

// ─── Effects ────────────────────────────────────────────────────────────────
// Delay lines are power-of-two so the read pointer is a bit-mask, not a modulo.
#define DELAY_BUF_LEN     32768   // 683 ms @ 48 kHz, 64 KB per channel
#define DELAY_BUF_MASK    (DELAY_BUF_LEN - 1)
#define CHORUS_BUF_LEN    2048    // 42 ms, plenty for chorus/flanger
#define CHORUS_BUF_MASK   (CHORUS_BUF_LEN - 1)

// ─── Sequencer dimensions ───────────────────────────────────────────────────
// Track 0 plays the internal synth (and echoes to MIDI OUT).  Tracks 1..3 are
// MIDI-only lanes for external gear — that is what the GP1 MIDI OUT is for.
#define NUM_TRACKS        4
#define INTERNAL_TRACK    0
#define NUM_STEPS         64
#define STEPS_PER_PAGE    16      // the 2x8 grid the main screen draws
#define NUM_PAGES         (NUM_STEPS / STEPS_PER_PAGE)
#define DEFAULT_STEPS     16
#define NUM_PATTERNS      8
#define CHAIN_LEN         8
#define STEP_MAX_NOTES    4       // chord size per step
#define STEP_LOCKS        4       // parameter locks per step

// ─── Tempo ──────────────────────────────────────────────────────────────────
#define BPM_DEFAULT    120
#define BPM_MIN        40
#define BPM_MAX        300
#define SWING_MAX      66      // % maximum swing

// ─── Display layout ─────────────────────────────────────────────────────────
#define DISP_W            128
#define DISP_H             64
#define HEADER_H           10
#define STEP_CELL_W        16    // 128 / 8 columns, exact
#define STEP_CELL_H        12
#define STEP_ROW1_Y        12
#define STEP_ROW2_Y        25
#define UI_REFRESH_MS     33    // ~30 fps

// ─── Storage ────────────────────────────────────────────────────────────────
// FatFS (not LittleFS) so the partition can also be exported over USB.
// Tools -> Flash Size: pick a layout that reserves a big FS on the 16 MB part,
// e.g. "8 MB Sketch / 8 MB FS".
#define STORAGE_MAGIC     0x5553   // 'US' — UltimateSynth
// Bumped from 2: GlobalSettings/UserPreset moved from the single-engine
// params[NUM_PARAMETERS] shape to an engineId + params[MAX_ENGINE_PARAMS]
// shape (see engine.h) now that the synth is multi-engine.
// Bumped from 3: GlobalSettings gained midiKbdPlay (the MIDI SET page's new
// "Kbd Play" row). Old settings.bin/preset files are rejected outright on
// version mismatch, not migrated — same policy as every prior bump.
// Bumped from 4: GlobalSettings gained bootChime (SETTINGS page's new
// "Boot Chime" row).
// Bumped from 5: GlobalSettings lost bootChime -- the boot-up chord through
// the live patch is gone (removed feature, not a bug fix), so its byte is no
// longer part of the struct. Old settings.bin/preset files are rejected
// outright, not migrated, same policy as every prior bump.
#define STORAGE_VERSION   6
#define USER_PRESET_SLOTS 64
// A saved song is ~90 bytes (BPM, name, and each track's chain + which
// pattern it starts on — songs reference pattern *slots*, not copies of
// their data, same as everything else that points at a pattern index).
// That's cheap enough that "as many as possible" comfortably means 200
// without thinking hard about flash budget; kept a uint8_t-friendly count
// to match every other slot-count constant in this file.
#define SONG_SLOTS        200
// Extra DX7 32-voice cartridges (.syx) read from /banks on the filesystem,
// on top of the 8 banks built into flash (dx7_rom.h) -- see voicebank.h.
// Costs ~42 bytes of RAM each (path + display name); voices themselves are
// read from flash on demand, not cached, so this is cheap even at the top
// end. 64 -> up to 2048 extra selectable voices.
#define MAX_FS_BANKS       64

#pragma once
// ============================================================================
//  Carpenter Synth — config.h        WEREWOLF S3 EDITION
//  Board : ESP32-S3 (DevKitC-1 style), 16 MB flash (arduino-esp32 3.x core)
//  Audio : 17-engine multi-synth (up to 10-voice each, one live at a time,
//          see engine.h) -> I2S -> PCM5102A
//  Disp  : 3.2" ILI9341 320x240 SPI TFT, XPT2046 resistive touch
//  Input : the touchscreen (soft keys + direct touch), plus OPTIONAL
//          encoder, five buttons and two pots on spare GPIOs
//
//  Werewolf hardware: the display, touch, DAC and MIDI pins below are the
//  same ones the werewolf's earlier ESP32-S3 build (WerewolfAmyS3) used, so
//  an existing werewolf S3 harness needs no rewiring. The synthesis side
//  (engines, effects, sequencer, storage layout) is the xtro Carpenter Synth
//  unchanged -- only the hardware layer below and the panel UI differ.
//
//  Pins the ESP32-S3 must not be given: 26-32 (SPI flash), 33-37 (octal
//  PSRAM on N16R8 modules), 19/20 (native USB), 0/3/45/46 (strapping --
//  see the notes where one of them IS used).
// ============================================================================

#include <Arduino.h>

// ─── ILI9341 TFT (SPI2 / FSPI, native IOMUX pins for 40 MHz) ────────────────
#define PIN_TFT_SCLK   12
#define PIN_TFT_MOSI   11
#define PIN_TFT_MISO   13
#define PIN_TFT_CS     10
#define PIN_TFT_DC      9
#define PIN_TFT_RST    14
#define PIN_TFT_BL     21      // backlight, active HIGH, PWM-dimmed
#define TFT_SPI_HZ     40000000
// Landscape. 0x28 = MV|BGR (connector on the left); 0xE8 = MX|MY|MV|BGR
// turns it 180 degrees. If red and blue come out swapped, drop the 0x08 bit.
#define TFT_MADCTL     0xE8
#define TFT_INVERT     0       // 1 for the (rarer) modules that need INVON

// ─── XPT2046 touch (the module's T_* header, its own SPI3 bus) ──────────────
#define PIN_TOUCH_CLK  15
#define PIN_TOUCH_MOSI 16      // T_DIN
#define PIN_TOUCH_MISO 47      // T_DO
#define PIN_TOUCH_CS    8
#define PIN_TOUCH_IRQ   3      // wired but unused: the panel is polled
#define TOUCH_SPI_HZ   2000000
#define TOUCH_Z_MIN     350    // pressure below this = not touched
#define TOUCH_POLL_MS    8

// ─── I2S audio out -> PCM5102A ──────────────────────────────────────────────
// PCM5102A: SCK->GND (internal PLL), FLT/DEMP/FMT->GND, XSMT->3V3.
#define PIN_I2S_BCLK    5
#define PIN_I2S_LRCK    4
#define PIN_I2S_DOUT    6

// ─── DIN MIDI on UART1 (IN via 6N138, OUT via 2 resistors) ──────────────────
#define PIN_MIDI_RX    18
#define PIN_MIDI_TX    17
#define MIDI_BAUD   31250

// ─── Optional panel controls ────────────────────────────────────────────────
// The touchscreen does everything on its own. These are the xtro board's
// physical controls, on the S3 pins the werewolf leaves free; set any pin to
// -1 if it is not fitted. Buttons and the encoder are to GND with internal
// pull-ups, so an unwired pin just reads "not pressed".
#define PIN_ENC_A      39
#define PIN_ENC_B      40
#define PIN_ENC_SW     41
#define ENC_TICKS_PER_DETENT 4

#define PIN_BTN_PLAY   42
#define PIN_BTN_SHIFT  38      // also the safe-boot key (held at power-up)
#define PIN_BTN_PAGE    7
#define PIN_BTN_TRACK   2
#define PIN_BTN_MUTE    1
// OCT DOWN/UP transpose an external MIDI keyboard. GPIO0 is the dev board's
// own BOOT button, so OCT DOWN works with nothing wired -- just don't hold it
// while powering up (that is the ROM's download mode). GPIO46 is an input-only
// strapping pin whose boot-time default is the "not pressed" level here.
#define PIN_BTN_OCT_DOWN 0
#define PIN_BTN_OCT_UP  46
#define DEBOUNCE_MS    15
#define LONG_PRESS_MS  600

// Pots: two ADC1 pins would be needed and every ADC1 pin is spoken for above,
// so the xtro board's CUTOFF/RESONANCE pots are off by default -- the same
// two parameters are on every engine's FILTER page. A floating ADC pin reads
// noise, which would drift the filter, hence off rather than "wire if you
// like". To fit them, free two of GPIO 1/2/7 (ADC1_CH0/CH1/CH6) above.
#define POTS_FITTED     0
#define PIN_POT_CUTOFF     -1
#define PIN_POT_RESONANCE  -1
#define POT_ADC_BITS       12
#define POT_ADC_MAX        4095.0f
#define POT_DEADBAND       (1.0f / 400.0f)   // ~0.25% of travel before re-applying

// Metronome gate out. GPIO43 is UART0 TX: the ROM prints a boot banner on it
// at reset (a burst of pulses on the click output), after which it is ours --
// the sketch's Serial is the native USB port, not UART0.
#define PIN_CLICK_OUT  43

// ─── CPU / cores ────────────────────────────────────────────────────────────
// NOTE the numbering is the reverse of the xtro build's: there "core0" was
// control and "core1" audio (arduino-pico's setup1/loop1), and the engine
// sources still say so in their comments. Here audio is on core 0.
// Tools -> CPU Frequency: 240 MHz. Audio renders on core 0 in its own task
// (nothing else runs there: no Wi-Fi, no Bluetooth); the Arduino loop() --
// sequencer, UI, MIDI, storage -- stays on core 1.
#define AUDIO_CORE        0
#define AUDIO_TASK_PRIO  (configMAX_PRIORITIES - 2)
#define AUDIO_TASK_STACK  8192

// ─── Audio ──────────────────────────────────────────────────────────────────
#define SAMPLE_RATE       48000
#define SAMPLE_RATE_F     48000.0f
#define INV_SAMPLE_RATE   (1.0f / SAMPLE_RATE_F)
#define AUDIO_BLOCK       64      // samples rendered per pass on the audio core (1.33 ms)
// How many AUDIO_BLOCKs of DMA cushion the I2S driver keeps queued.  Costs
// AUDIO_BLOCK*4 bytes each (16 x 64 x 4 = 4 KB) and buys ~21 ms of slack, so a
// flash erase or a heavy redraw on the UI core can't punch a hole in the output.
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

// ─── Display ────────────────────────────────────────────────────────────────
#define DISP_W           320
#define DISP_H           240
#define UI_REFRESH_MS     33      // ~30 fps; only changed bands are sent

// ─── Storage ────────────────────────────────────────────────────────────────
// FAT (arduino-esp32's FFat, on the "ffat" partition) rather than LittleFS so
// the same partition can be exported to a computer over USB (SETTINGS -> USB
// DRIVE). Tools -> Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)".
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
// Bumped from 6 (werewolf S3 port): GlobalSettings gained phosphor and
// crtLines for the colour panel.
#define STORAGE_VERSION   7
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

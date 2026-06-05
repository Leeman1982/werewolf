/*******************************************************************
 ZOMBIE SS PROPHET-8 SYNTHESIZER  v3.1
 Prophet-8 inspired band-limited wavetable synth for ESP32 CYD

 v3.1 Audio quality & reliability pass:
 - Master output now uses an always-on cubic soft-clipper (replaces the
   brick-wall clamp): loud chords saturate smoothly instead of producing
   harsh, "gain-accumulating" digital clipping.
 - Master limiter enabled by default for transparent peak control.
 - Filter coefficient caching: powf/sinf in the modulated-cutoff path is
   recomputed only when the modulated cutoff actually moves, eliminating
   ~44 kHz transcendental calls/voice that starved Core-0 and caused
   dropouts/glitches with several voices (chords) active.
 - State-variable filter stability margin tightened (no runaway screech
   at high cutoff + resonance).
 - Chord pad: STRUM is now non-blocking (no delay() stalling MIDI/touch);
   OCT/type changes re-voice the held chord cleanly instead of stacking
   voices; all strummed notes are tracked so ALL-NOTES-OFF always silences.
 - LFO pitch path no longer re-pitches every voice on every tick when idle.
 - FX-page section labels no longer clipped off the bottom of the screen;
   chord-pad chord label no longer overlaps the wordmark.

 v3 Features (on top of v2):
 - Band-limited wavetable oscillators (replaces polyBLEP)
   * SAW / SQUARE / TRIANGLE: additive-synthesis tables, alias-free
   * ~40% less CPU per oscillator vs polyBLEP
   * SINE: direct sinf(); PULSE: polyBLEP (variable PW)
 - Pitch bend fully wired (MIDI ±2 semitones)
 - OSC1/OSC2 level sliders now control the actual mix
 - Filter env-amount slider now modulates the filter correctly
 - osc2Detune saved/loaded per preset

 v2 Features:
 - 8-voice polyphony
 - State variable filter with ADSR envelopes
 - LFO (sine/tri/saw/square/S&H) targeting filter/pitch/amp
 - 50-pattern arpeggiator with BPM ±1/±10 controls
 - 16-step sequencer with per-track sound select (4 tracks)
 - Presets: 10 factory + 10 user (NVS persistent via Preferences)
 - On-screen QWERTY for preset naming
 - Chord Pad: 8 chord types × 12 roots (bonus feature)
 - Note name display as notes are played
 - USB and 5-pin DIN MIDI input (GPIO 35)
 - PCM5052 DAC output (I2S) on GPIO 22/27/17
 *******************************************************************/

#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <TFT_eSPI.h>

// Core engine and input
#include "synth_engine.h"
#include "midi_input.h"
#include "arpeggiator_patterns.h"
#include "zombie_step_sequencer.h"

// v2 support files (included before mode files)
#include "zombie_lfo.h"
#include "zombie_presets.h"
#include "zombie_keyboard_input.h"

// UI elements and mode files (synth_mode first – defines synthParams + getZombieSynth)
#include "ui_elements.h"
#include "zombie_synth_mode.h"
#include "zombie_arp_mode.h"
#include "zombie_seq_mode.h"
#include "zombie_presets_mode.h"
#include "zombie_chord_pad.h"

// ── Hardware pins ──────────────────────────────────────────────────────────
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33

// ── Global objects ─────────────────────────────────────────────────────────
SPIClass mySpi = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

MIDIInput  midiInput;
TouchState touch;
AppMode    currentMode = MENU;

// Global LFO shared across all modes
LFOEngine globalLFO;

// Last played MIDI note (for note-name corner display)
int lastPlayedMidiNote = -1;

// Audio task handle
TaskHandle_t audioTaskHandle = NULL;

// ── MIDI callbacks ─────────────────────────────────────────────────────────
void onMIDINoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  lastPlayedMidiNote = note;

  SynthEngine* synth = getZombieSynth();
  Arpeggiator* arp   = getZombieArp();

  if (synth) synth->noteOn(note, velocity);
  if (arp)   arp->noteOn(note);
  // Live recording: if the sequencer is armed and playing, capture this note.
  ZombieSequencer* seq = getZombieSeq();
  if (seq) seq->recordNote(note, velocity);
}

// MIDI clock/transport pass-through to the sequencer.
void onMIDIClockTick()     { ZombieSequencer* s = getZombieSeq(); if (s) s->onMidiClockTick();     }
void onMIDIClockStart()    { ZombieSequencer* s = getZombieSeq(); if (s) s->onMidiClockStart();    }
void onMIDIClockContinue() { ZombieSequencer* s = getZombieSeq(); if (s) s->onMidiClockContinue(); }
void onMIDIClockStop()     { ZombieSequencer* s = getZombieSeq(); if (s) s->onMidiClockStop();     }

// Sequencer → MIDI TX (used in CLOCK_OUT mode to emit 0xF8 / 0xFA / 0xFC bytes).
void seqMidiSendByte(uint8_t b) { midiInput.sendByte(b); }

void onMIDINoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
  SynthEngine* synth = getZombieSynth();
  Arpeggiator* arp   = getZombieArp();

  if (synth) synth->noteOff(note);
  if (arp)   arp->noteOff(note);
}

void onMIDICC(uint8_t channel, uint8_t cc, uint8_t value) {
  SynthEngine*     synth = getZombieSynth();
  Arpeggiator*     arp   = getZombieArp();
  ZombieSequencer* seq   = getZombieSeq();

  float v = value / 127.0f;

  switch (cc) {
    // ── Filter ──────────────────────────────────────────────────────────
    case 74: if (synth) { synth->setFilterCutoff(v);    synthParams.filterCutoff    = v; synthParams.needsRedraw = true; } break;
    case 71: if (synth) { synth->setFilterResonance(v); synthParams.filterResonance = v; synthParams.needsRedraw = true; } break;

    // ── Volume ──────────────────────────────────────────────────────────
    case 7:  if (synth) { synth->setMasterVolume(v); synthParams.masterVolume = v; synthParams.needsRedraw = true; } break;
    case 12: synthParams.osc1Level = v; synthParams.needsRedraw = true; break;
    case 13: synthParams.osc2Level = v; synthParams.needsRedraw = true; break;

    // ── Amp Envelope ────────────────────────────────────────────────────
    case 73: synthParams.ampAttack  = v*2.0f; if (synth) synth->setAmpEnvelope(synthParams.ampAttack, synthParams.ampDecay, synthParams.ampSustain, synthParams.ampRelease); synthParams.needsRedraw=true; break;
    case 75: synthParams.ampDecay   = v*2.0f; if (synth) synth->setAmpEnvelope(synthParams.ampAttack, synthParams.ampDecay, synthParams.ampSustain, synthParams.ampRelease); synthParams.needsRedraw=true; break;
    case 70: synthParams.ampSustain = v;      if (synth) synth->setAmpEnvelope(synthParams.ampAttack, synthParams.ampDecay, synthParams.ampSustain, synthParams.ampRelease); synthParams.needsRedraw=true; break;
    case 72: synthParams.ampRelease = v*2.0f; if (synth) synth->setAmpEnvelope(synthParams.ampAttack, synthParams.ampDecay, synthParams.ampSustain, synthParams.ampRelease); synthParams.needsRedraw=true; break;

    // ── Filter Envelope ─────────────────────────────────────────────────
    case 76: synthParams.filterAttack  = v*2.0f; if (synth) synth->setFilterEnvelope(synthParams.filterAttack, synthParams.filterDecay, synthParams.filterSustain, synthParams.filterRelease); synthParams.needsRedraw=true; break;
    case 77: synthParams.filterDecay   = v*2.0f; if (synth) synth->setFilterEnvelope(synthParams.filterAttack, synthParams.filterDecay, synthParams.filterSustain, synthParams.filterRelease); synthParams.needsRedraw=true; break;
    case 78: synthParams.filterSustain = v;      if (synth) synth->setFilterEnvelope(synthParams.filterAttack, synthParams.filterDecay, synthParams.filterSustain, synthParams.filterRelease); synthParams.needsRedraw=true; break;
    case 79: synthParams.filterRelease = v*2.0f; if (synth) synth->setFilterEnvelope(synthParams.filterAttack, synthParams.filterDecay, synthParams.filterSustain, synthParams.filterRelease); synthParams.needsRedraw=true; break;

    // ── Arpeggiator ─────────────────────────────────────────────────────
    case 80: if (arp) arp->setBPM(30.0f + v*270.0f);                        break;
    case 81: if (arp) arp->setPattern((ArpPattern)(int)(v * 49.0f));         break;
    case 82: if (arp) arp->setOctaveRange(1 + (int)(v * 3.0f));              break;
    case 83: if (arp) arp->setGateLength(10 + (int)(v * 90.0f));             break;

    // ── Sequencer ───────────────────────────────────────────────────────
    case 85: if (seq) seq->setBPM(40.0f + v * 260.0f);   break;
    case 86: if (seq) seq->setSwing(50 + (int)(v * 25)); break;

    // ── LFO (CC 87=rate, CC 88=depth) ───────────────────────────────────
    case 87: globalLFO.rate  = v * 20.0f;                             break;
    case 88: globalLFO.depth = v; globalLFO.enabled = (v > 0.01f);   break;
  }
}

void onMIDIPitchBend(uint8_t channel, int16_t bend) {
  SynthEngine* synth = getZombieSynth();
  if (synth) {
    // bend: -8192..+8191 → ±2 semitones (standard MIDI pitch-bend range)
    float semitones = (float)bend / 8192.0f * 2.0f;
    synth->setPitchBend(semitones);
  }
}

// ── Audio task (Core 0) ────────────────────────────────────────────────────
void audioTask(void* parameter) {
  while (true) {
    SynthEngine* synth = getZombieSynth();
    if (synth) synth->processAudio();
    vTaskDelay(1);
  }
}

// ── Menu ───────────────────────────────────────────────────────────────────
struct AppIcon {
  const char* name;
  const char* symbol;
  AppMode     mode;
};

static const AppIcon apps[] = {
  {"SYNTH",   "SS",  ZOMBIE_SYNTH},
  {"ARP",     "ARP", ZOMBIE_ARP},
  {"SEQ",     "SEQ", ZOMBIE_SEQ},
  {"PRESETS", "PRE", ZOMBIE_PRESETS},
  {"CHORD",   "CHD", ZOMBIE_CHORD},
};
static const int NUM_APPS = 5;

// Layout: row0 = icons 0-2 (3 wide), row1 = icons 3-4 (2 wide, centred)
static const int ICON_W = 88, ICON_H = 58, ICON_GAP = 8;

static int iconX(int i) {
  if (i < 3) return (320 - 3*(ICON_W+ICON_GAP) + ICON_GAP) / 2 + (i % 3)*(ICON_W+ICON_GAP);
  else        return (320 - 2*(ICON_W+ICON_GAP) + ICON_GAP) / 2 + (i - 3)*(ICON_W+ICON_GAP);
}
static int iconY(int i) {
  return 85 + (i / 3) * (ICON_H + ICON_GAP);
}

void drawMenu() {
  tft.fillScreen(THEME_BG);

  // Header
  tft.drawRect(0, 0, 320, 62, THEME_OUTLINE);
  tft.drawRect(1, 1, 318, 60, THEME_OUTLINE);
  tft.drawRect(2, 2, 316, 58, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("ZOMBIE SS", 160, 8, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("PROPHET SYNTHESIZER  v3", 160, 38, 2);

  // Status line
  SynthEngine* synth = getZombieSynth();
  if (synth) {
    char buf[20];
    sprintf(buf, "VOICES:%d/8", synth->getActiveVoiceCount());
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawRightString(buf, 314, 68, 2);
  }
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("v3.0", 6, 68, 2);

  // App icons
  for (int i = 0; i < NUM_APPS; i++) {
    int x = iconX(i), y = iconY(i);
    tft.fillRoundRect(x, y, ICON_W, ICON_H, 8, THEME_BG);
    tft.drawRoundRect(x, y, ICON_W, ICON_H, 8, THEME_OUTLINE);
    tft.drawRoundRect(x+1, y+1, ICON_W-2, ICON_H-2, 7, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(apps[i].symbol, x + ICON_W/2, y + 8,  4);
    tft.drawCentreString(apps[i].name,   x + ICON_W/2, y + 40, 2);
  }

  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("TAP TO SELECT MODE", 160, 228, 2);
}

void enterMode(AppMode mode) {
  currentMode = mode;
  tft.fillScreen(THEME_BG);
  switch (mode) {
    case ZOMBIE_SYNTH:   zombieSynthInit();   break;
    case ZOMBIE_ARP:     zombieArpInit();     break;
    case ZOMBIE_SEQ:     zombieSeqInit();     break;
    case ZOMBIE_PRESETS: zombiePresetsInit(); break;
    case ZOMBIE_CHORD:   zombieChordInit();   break;
    default:             drawMenu();          break;
  }
}

void exitToMenu() {
  SynthEngine*     synth = getZombieSynth();
  Arpeggiator*     arp   = getZombieArp();
  ZombieSequencer* seq   = getZombieSeq();
  if (synth) synth->allNotesOff();
  if (arp)   arp->allNotesOff();
  if (seq)   seq->stop();
  chordAllOff();
  enterMode(MENU);
}

// ── Setup ──────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("ZOMBIE SS v3 — initializing (BL wavetable oscillators)");

  // Touch SPI
  mySpi.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(mySpi);
  ts.setRotation(1);

  // Display
  tft.init();
  tft.setRotation(1);
  tft.invertDisplay(true);   // Required for ESP32-2432S028R colour fix
  tft.fillScreen(THEME_BG);

  // Splash screen
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("ZOMBIE SS", 160, 75, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("PROPHET SYNTHESIZER v3", 160, 112, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("Building BL wavetables...", 160, 148, 2);
  delay(800);

  // Init synth engine
  zombieSynthInit();
  Serial.println("Synth OK");

  // Init MIDI
  tft.fillRect(0, 148, 320, 16, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("Initializing MIDI...", 160, 148, 2);
  midiInput.init();
  midiInput.setNoteOnCallback(onMIDINoteOn);
  midiInput.setNoteOffCallback(onMIDINoteOff);
  midiInput.setCCCallback(onMIDICC);
  midiInput.setPitchBendCallback(onMIDIPitchBend);
  midiInput.setClockTickCallback(onMIDIClockTick);
  midiInput.setClockStartCallback(onMIDIClockStart);
  midiInput.setClockContinueCallback(onMIDIClockContinue);
  midiInput.setClockStopCallback(onMIDIClockStop);
  Serial.println("MIDI OK");

  // Audio task on Core 0 (priority 24)
  tft.fillRect(0, 148, 320, 16, THEME_BG);
  tft.drawCentreString("Starting audio task...", 160, 148, 2);
  xTaskCreatePinnedToCore(audioTask, "AudioTask", 8192, NULL, 24, &audioTaskHandle, 0);
  Serial.println("Audio task on Core 0");

  delay(400);
  enterMode(MENU);
  Serial.println("Ready!");
}

// ── Main loop ──────────────────────────────────────────────────────────────
void loop() {
  // Update touch state
  updateTouch();

  // Process incoming MIDI
  midiInput.update();

  // ── LFO tick at 200 Hz ──────────────────────────────────────────────
  static unsigned long lastLfoTick = 0;
  if (millis() - lastLfoTick >= 5) {
    lastLfoTick = millis();
    globalLFO.tick();

    SynthEngine* synth = getZombieSynth();
    if (synth) {
      // Tracks whether the pitch path is currently detuned, so we only re-pitch
      // every active voice (a powf + per-voice loop) when it actually changes —
      // not on every 5 ms tick while the LFO is off or targeting filter/amp.
      static bool pitchModActive = false;
      float out = globalLFO.enabled ? globalLFO.output : 0.0f;
      // Reset the cheap scalar mod paths then apply only the active target
      synth->setLFOFilterMod(0.0f);
      synth->setLFOAmpMod(0.0f);
      bool pitchTarget = false;
      if (globalLFO.enabled && out != 0.0f) {
        switch (globalLFO.target) {
          case LFO_TARGET_FILTER:
            // ±0.15 in log-cutoff space ≈ ±1.5 octaves of filter sweep
            synth->setLFOFilterMod(out * 0.15f);
            break;
          case LFO_TARGET_AMP:
            // Tremolo: ±40 % amplitude at depth=1
            synth->setLFOAmpMod(out * 0.4f);
            break;
          case LFO_TARGET_PITCH:
            // Vibrato: ±2 semitones at depth=1 (standard synth vibrato range)
            synth->setLFOPitch(out * 2.0f);
            pitchModActive = true;
            pitchTarget    = true;
            break;
        }
      }
      // Snap pitch back to base exactly once when vibrato stops, instead of
      // re-pitching every voice on every tick.
      if (!pitchTarget && pitchModActive) {
        synth->setLFOPitch(0.0f);
        pitchModActive = false;
      }
    }
  }

  // ── Mode dispatch ────────────────────────────────────────────────────
  switch (currentMode) {
    case MENU:
      if (touch.justPressed) {
        for (int i = 0; i < NUM_APPS; i++) {
          if (isButtonPressed(iconX(i), iconY(i), ICON_W, ICON_H)) {
            enterMode(apps[i].mode);
            break;
          }
        }
      }
      break;

    case ZOMBIE_SYNTH:
      zombieSynthDraw();
      zombieSynthHandleTouch();
      zombieSynthUpdate();
      break;

    case ZOMBIE_ARP:
      zombieArpDraw();
      zombieArpHandleTouch();
      zombieArpUpdate();
      break;

    case ZOMBIE_SEQ:
      zombieSeqDraw();
      zombieSeqHandleTouch();
      zombieSeqUpdate();
      break;

    case ZOMBIE_PRESETS:
      zombiePresetsDraw();
      zombiePresetsHandleTouch();
      zombiePresetsUpdate();
      break;

    case ZOMBIE_CHORD:
      zombieChordDraw();
      zombieChordHandleTouch();
      zombieChordUpdate();
      break;
  }

  delay(20);  // ~50 Hz UI refresh rate
}

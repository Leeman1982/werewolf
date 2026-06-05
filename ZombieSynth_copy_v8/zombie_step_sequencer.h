#ifndef ZOMBIE_STEP_SEQUENCER_H
#define ZOMBIE_STEP_SEQUENCER_H

#include <Preferences.h>

// Define USE_SD_STORAGE before including this header (or via build flags) to
// enable optional MicroSD persistence.  The CYD board has an SD slot on the
// secondary SPI bus; when mounted, saveSongSD/loadSongSD/savePatternSD/
// loadPatternSD write to /zombi/*.bin.  NVS-based save still works as a
// fallback when no card is present.
#ifdef USE_SD_STORAGE
#include <SD.h>
#include <FS.h>
#include <SPI.h>
#ifndef SEQ_SD_CS
#define SEQ_SD_CS 5
#endif
#endif

#include "synth_engine.h"
#include "synth_patch.h"

#define MAX_SEQ_STEPS  32
#define MAX_SEQ_TRACKS  4
#define MAX_PATTERNS    8
#define MAX_SONG_SLOTS 32
#define NUM_SONGS       8

// ── Scale tables ──────────────────────────────────────────────────────────────
// Bit N = semitone N above root is in-scale. Bit 0 = root.
static const uint16_t SEQ_SCALE_MASKS[9] = {
  0x0FFF,  // CHROMATIC
  0x0AB5,  // MAJOR       C D E F G A B
  0x05AD,  // NAT MINOR   C D Eb F G Ab Bb
  0x06AD,  // DORIAN      C D Eb F G A Bb
  0x05AB,  // PHRYGIAN    C Db Eb F G Ab Bb
  0x06B5,  // MIXOLYDIAN  C D E F G A Bb
  0x0295,  // PENTA MAJ   C D E G A
  0x04A9,  // PENTA MIN   C Eb F G Bb
  0x09CD,  // HUNGARIAN   C D Eb F# G Ab B
};
static const char* SEQ_SCALE_NAMES[9] = {
  "CHROM","MAJOR","MINOR","DORIAN","PHRYG","MIXO","PEN+","PEN-","HUNG"
};
static const int   SEQ_DIV_TICKS[4] = { 1, 2, 4, 8 };   // 1/32, 1/16, 1/8, 1/4
static const char* SEQ_DIV_NAMES[4] = { "1/32","1/16","1/8","1/4" };
static const char* SEQ_NOTE_NAMES[12] = {
  "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"
};

// ── Step condition ────────────────────────────────────────────────────────────
enum StepCondition : uint8_t {
  COND_NONE=0, COND_FILL, COND_NOT_FILL,
  COND_EVERY2, COND_EVERY3, COND_EVERY4,
  COND_COUNT
};
static const char* COND_NAMES[COND_COUNT] = {
  "ALW","FIL","!FIL","E:2","E:3","E:4"
};

// ── Step ─────────────────────────────────────────────────────────────────────
struct SequencerStep {
  bool    active;
  int     note;         // MIDI 0–127
  int     velocity;     // 1–127
  int     gate;         // 1–16 fraction of step length (16 = full)
  bool    tie;
  uint8_t probability;  // 0–100 %
  uint8_t ratchets;     // 1–4 hits per step
  uint8_t condition;    // StepCondition
  // Parameter locks (P-Locks): per-step modulation applied at trigger time.
  // 0 = no lock (default).  Range chosen so a single signed byte covers the
  // useful musical sweep with audible per-step granularity.
  int8_t  cutoffLock;   // -64..+63 → normalized cutoff offset (÷64)
  int8_t  panLock;      // -64..+63 → pan offset (÷64), added to track pan
};

typedef void (*SeqNoteOnCB)(int trackIdx, int noteNum, int vel);
typedef void (*SeqNoteOffCB)(int trackIdx, int noteNum);

// ── Track ─────────────────────────────────────────────────────────────────────
struct SequencerTrack {
  SequencerStep steps[MAX_SEQ_STEPS];
  int     length;        // active steps: 8, 16, 24, or 32
  int     divIdx;        // index into SEQ_DIV_TICKS (0=1/32 … 3=1/4)
  int     scale;         // 0–8
  int     rootNote;      // 0–11 (C…B)
  int     octave;        // 0–6 (3 = middle C area)
  bool    muted;
  bool    soloed;        // pro: solo overrides mute for non-soloed tracks
  float   trackVolume;   // 0..1, scales velocity at trigger
  float   trackPan;      // -1..+1
  int     soundPatchIdx; // last factory preset loaded (display only)
  SynthPatch trackPatch;   // per-track full synth patch used on note-on
  // Runtime state (not saved)
  int     currentStep;
  int     tickCount;
  int     gateCountdown;
  int     repeatCounter; // loops completed (for EVERY-N conditions)
  int     ratchetCount;
  int     ratchetTick;

  int division() const { return SEQ_DIV_TICKS[constrain(divIdx, 0, 3)]; }

  void init() {
    length = 16; divIdx = 1; scale = 0; rootNote = 0; octave = 3;
    muted = false; soloed = false; trackVolume = 1.0f; trackPan = 0.0f;
    soundPatchIdx = 0;
    // Default patch: open SAW bass with sensible ADSR
    memset(&trackPatch, 0, sizeof(SynthPatch));
    strncpy(trackPatch.name, "DEFAULT", PRESET_NAME_LEN);
    trackPatch.osc1Wave       = WAVE_SAW;
    trackPatch.osc1Level      = 0.80f;
    trackPatch.osc2Wave       = WAVE_SAW;
    trackPatch.osc2Level      = 0.0f;
    trackPatch.osc2Detune     = 0.005f;
    trackPatch.filterType     = FILTER_LOWPASS;
    trackPatch.filterCutoff   = 0.60f;
    trackPatch.filterResonance= 0.20f;
    trackPatch.filterEnvAmount= 0.30f;
    trackPatch.ampAttack      = 0.005f;
    trackPatch.ampDecay       = 0.20f;
    trackPatch.ampSustain     = 0.70f;
    trackPatch.ampRelease     = 0.15f;
    trackPatch.filterAttack   = 0.005f;
    trackPatch.filterDecay    = 0.20f;
    trackPatch.filterSustain  = 0.40f;
    trackPatch.filterRelease  = 0.15f;
    trackPatch.lfoWave        = 0;
    trackPatch.lfoRate        = 0.0f;
    trackPatch.lfoDepth       = 0.0f;
    trackPatch.lfoTarget      = 0;
    trackPatch.masterVolume   = 0.75f;
    trackPatch.subWave        = WAVE_SQUARE;
    trackPatch.subLevel       = 0.0f;
    trackPatch.subOctave      = 1;
    currentStep = 0; tickCount = 0; gateCountdown = 0;
    repeatCounter = 0; ratchetCount = 0; ratchetTick = 0;
    for (int i = 0; i < MAX_SEQ_STEPS; i++) {
      steps[i].active      = false;
      steps[i].note        = 60 + (i % 12);
      steps[i].velocity    = 100;
      steps[i].gate        = 8;
      steps[i].tie         = false;
      steps[i].probability = 100;
      steps[i].ratchets    = 1;
      steps[i].condition   = COND_NONE;
      steps[i].cutoffLock  = 0;
      steps[i].panLock     = 0;
    }
  }

  void clear() {
    for (int i = 0; i < MAX_SEQ_STEPS; i++) steps[i].active = false;
  }

  void randomize() {
    for (int i = 0; i < length; i++) {
      steps[i].active      = (random(100) < 60);
      steps[i].note        = 48 + random(24);
      steps[i].velocity    = 70 + random(57);
      steps[i].gate        = 4 + random(9);
      steps[i].probability = 75 + random(26);
      steps[i].ratchets    = 1;
      steps[i].condition   = COND_NONE;
      steps[i].cutoffLock  = 0;
      steps[i].panLock     = 0;
    }
  }

  // Quantize a MIDI note to this track's scale / root
  int quantizeNote(int raw) const {
    if (scale == 0) return constrain(raw, 0, 127);
    uint16_t mask = SEQ_SCALE_MASKS[scale];
    int semitone  = raw % 12;
    int best = semitone, bestDist = 13;
    for (int s = 0; s < 12; s++) {
      int rel = (s - rootNote + 12) % 12;
      if (mask & (1 << rel)) {
        int d = abs(s - semitone);
        if (d > 6) d = 12 - d;
        if (d < bestDist) { bestDist = d; best = s; }
      }
    }
    return constrain((raw / 12) * 12 + best, 0, 127);
  }

  // Fill an Euclidean pattern: k hits spread over n steps
  void euclidean(int k, int n) {
    n = constrain(n, 1, length);
    k = constrain(k, 0, n);
    clear();
    for (int i = 0; i < n; i++)
      steps[i].active = ((i * k % n) < k);
  }
};

// ── Pattern ───────────────────────────────────────────────────────────────────
struct SeqPattern {
  SequencerTrack tracks[MAX_SEQ_TRACKS];
};

// ── Song (ordered list of pattern slots with repeat counts) ───────────────────
struct SongSlot {
  uint8_t patternIdx;   // 0..MAX_PATTERNS-1
  uint8_t repeatCount;  // 1..255
};

struct Song {
  char     name[16];
  uint8_t  numSlots;
  SongSlot slots[MAX_SONG_SLOTS];
};

// ── Sequencer engine ──────────────────────────────────────────────────────────
class ZombieSequencer {
public:
  SeqPattern  patterns[MAX_PATTERNS];
  int         activePattern;
  int         nextPattern;   // -1 = chain disabled
  bool        fillMode;
  // Swing: 50 = straight (no swing), 50..75 = % delay of every other 16th.
  uint8_t     swing;

  // Song mode
  Song        currentSong;
  int         songSlotIdx;       // current position in song
  int         songRepeatsLeft;   // repeats remaining of current slot's pattern
  bool        songMode;          // true while playSong() is driving the engine
  bool        songLoop;          // true = wrap to slot 0 at end (default true)
  bool        instantSwitch;     // true = nextPattern takes effect at next tick,
                                 //        not at end of current pattern.
                                 //        For live performance use.

  // MIDI clock sync mode
  enum ClockMode { CLOCK_INTERNAL = 0, CLOCK_EXT = 1, CLOCK_OUT = 2 };
  ClockMode   clockMode;
  // 24 PPQN external clock counts into 1/32-note internal ticks.
  // 24 PPQN means 24 ticks per quarter; our internal tick is 1/32, i.e. 8/quarter.
  // → every 3 external ticks advance one internal tick.  extClockAccum counts.
  int         extClockAccum;
  // 24 PPQN output: emit one clock byte every (BPM tick interval / 3) when in OUT mode.
  int         outClockAccum;
  // Callback invoked when we want to send a MIDI clock byte (0xF8/start/stop).
  void        (*_midiSendByteCB)(uint8_t);

  // Live recording: when armed, incoming MIDI note-ons are quantized to the
  // active track's current step and written there in real time.
  bool        recordArmed;
  int         recordTrack;       // which track to record into (default: active)

private:
  float           bpm;
  unsigned long   lastTickUs;
  unsigned long   tickIntervalUs;
  bool            isPlaying;
  int             activeTrack;
  unsigned long   tapTimes[4];
  int             tapCount;
  SynthEngine*    synth;
  SeqNoteOnCB     _noteOnCB;
  SeqNoteOffCB    _noteOffCB;
  int             activeNotes[MAX_SEQ_TRACKS];
  Preferences     prefs;          // namespace "zombie_seq"
  bool            prefsOpen;
#ifdef USE_SD_STORAGE
  bool            sdAvailable = false;
#endif

  void calcInterval() {
    // 1 tick = 1/32nd note = 60 s / (BPM * 8)
    tickIntervalUs = (unsigned long)(60000000.0f / (bpm * 8.0f));
  }

  void fireNoteOff(int t) {
    if (activeNotes[t] >= 0) {
      if (_noteOffCB) _noteOffCB(t, activeNotes[t]);
      else if (synth)  synth->noteOff(activeNotes[t]);
      activeNotes[t] = -1;
    }
  }

  bool anySoloed() const {
    for (int t = 0; t < MAX_SEQ_TRACKS; t++)
      if (patterns[activePattern].tracks[t].soloed) return true;
    return false;
  }

  // Attempt to trigger the current step of track t
  void triggerStep(int t) {
    SequencerTrack& tr   = patterns[activePattern].tracks[t];
    SequencerStep&  step = tr.steps[tr.currentStep];
    if (!step.active) return;
    // Solo overrides mute: if anyone is soloed, non-soloed tracks are silent.
    if (anySoloed()) {
      if (!tr.soloed) return;
    } else if (tr.muted) {
      return;
    }

    // Probability gate
    if ((int)step.probability < 100 && random(100) >= (int)step.probability) return;

    // Condition gate
    switch ((StepCondition)step.condition) {
      case COND_FILL:     if (!fillMode)              return; break;
      case COND_NOT_FILL: if ( fillMode)              return; break;
      case COND_EVERY2:   if (tr.repeatCounter % 2)  return; break;
      case COND_EVERY3:   if (tr.repeatCounter % 3)  return; break;
      case COND_EVERY4:   if (tr.repeatCounter % 4)  return; break;
      default: break;
    }

    // Octave 3 = middle C area (MIDI 48–59)
    int raw  = step.note + (tr.octave - 3) * 12;
    int note = tr.quantizeNote(constrain(raw, 0, 127));

    // Per-track volume scales velocity.
    int effVel = constrain((int)(step.velocity * tr.trackVolume), 1, 127);
    // Per-step parameter locks: fold panLock into pan, hand cutoffLock to engine
    float pan      = constrain(tr.trackPan + (float)step.panLock * (1.0f / 64.0f), -1.0f, 1.0f);
    float cutoffMod = (float)step.cutoffLock * (1.0f / 64.0f);

    if (!step.tie) fireNoteOff(t);

    if (_noteOnCB) _noteOnCB(t, note, effVel);
    else if (synth) synth->noteOnPanCutoff(note, effVel, pan, cutoffMod);
    activeNotes[t] = note;

    int div = tr.division();
    tr.gateCountdown = max(1, step.gate * div / 16);
    int interval     = max(1, div / (int)step.ratchets);
    tr.ratchetCount  = step.ratchets - 1;
    tr.ratchetTick   = interval;
  }

  void processTick() {
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
      SequencerTrack& tr = patterns[activePattern].tracks[t];

      // Gate countdown → note-off
      if (tr.gateCountdown > 0 && --tr.gateCountdown == 0) fireNoteOff(t);

      // Ratchet retrigger
      if (tr.ratchetCount > 0 && --tr.ratchetTick <= 0) {
        SequencerStep& step = tr.steps[tr.currentStep];
        int raw  = step.note + (tr.octave - 3) * 12;
        int note = tr.quantizeNote(constrain(raw, 0, 127));
        int div  = tr.division();
        int rvel = constrain((int)(step.velocity * tr.trackVolume), 1, 127);
        float rpan      = constrain(tr.trackPan + (float)step.panLock * (1.0f / 64.0f), -1.0f, 1.0f);
        float rcutoffMod = (float)step.cutoffLock * (1.0f / 64.0f);
        fireNoteOff(t);
        if (_noteOnCB) _noteOnCB(t, note, rvel);
        else if (synth) synth->noteOnPanCutoff(note, rvel, rpan, rcutoffMod);
        activeNotes[t]   = note;
        tr.gateCountdown = max(1, div / (2 * (int)step.ratchets));
        tr.ratchetCount--;
        tr.ratchetTick   = max(1, div / (int)step.ratchets);
      }

      // Step advance
      if (++tr.tickCount >= tr.division()) {
        tr.tickCount = 0;
        if (++tr.currentStep >= tr.length) {
          tr.currentStep = 0;
          tr.repeatCounter++;
        }
        triggerStep(t);
      }
    }

    // Pattern chaining: switch when all tracks have completed ≥1 loop and are back at step 0
    // In song mode this same trigger drives slot advancement once the current
    // slot's repeatCount has been consumed.
    // INSTANT mode: any queued pattern change fires on the next tick instead
    // of waiting for the bar boundary — for live performance pattern jumps.
    bool ready = false;
    if (nextPattern >= 0 && instantSwitch) {
      ready = true;  // live pattern jump, fire immediately
    } else if (nextPattern >= 0 || songMode) {
      ready = true;
      for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
        SequencerTrack& tr = patterns[activePattern].tracks[t];
        if (tr.repeatCounter == 0 || tr.currentStep != 0 || tr.tickCount != 0)
          { ready = false; break; }
      }
    }

    if (!ready) return;

    if (songMode) {
      // One loop of the current song slot has just completed.
      if (--songRepeatsLeft <= 0) {
        // Advance to next slot
        songSlotIdx++;
        if (songSlotIdx >= (int)currentSong.numSlots) {
          if (songLoop && currentSong.numSlots > 0) {
            songSlotIdx = 0;
          } else {
            songMode = false;
            return;
          }
        }
        const SongSlot& s = currentSong.slots[songSlotIdx];
        nextPattern     = (int)s.patternIdx % MAX_PATTERNS;
        songRepeatsLeft = (s.repeatCount < 1) ? 1 : (int)s.repeatCount;
      }
      // else: same pattern, just keep looping (repeatCounter naturally accumulates)
    }

    if (nextPattern >= 0) {
      activePattern = nextPattern;
      nextPattern   = -1;
      for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
        patterns[activePattern].tracks[t].repeatCounter = 0;
        patterns[activePattern].tracks[t].currentStep   = 0;
        patterns[activePattern].tracks[t].tickCount     = 0;
        triggerStep(t);
      }
    }
  }

public:
  ZombieSequencer()
    : activePattern(0), nextPattern(-1), fillMode(false), swing(50),
      songSlotIdx(0), songRepeatsLeft(0), songMode(false), songLoop(true),
      instantSwitch(false),
      clockMode(CLOCK_INTERNAL), extClockAccum(0), outClockAccum(0),
      _midiSendByteCB(NULL),
      recordArmed(false), recordTrack(0),
      bpm(120.0f), lastTickUs(0), tickIntervalUs(0), isPlaying(false),
      activeTrack(0), tapCount(0), synth(NULL), _noteOnCB(NULL), _noteOffCB(NULL),
      prefsOpen(false) {
    for (int i = 0; i < MAX_SEQ_TRACKS; i++) activeNotes[i] = -1;
    for (int i = 0; i < 4; i++) tapTimes[i] = 0;
    for (int p = 0; p < MAX_PATTERNS; p++)
      for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
        patterns[p].tracks[t].init();
        patterns[p].tracks[t].soundPatchIdx = t;
      }
    // Default song: 4 slots (PAT 1..4, each playing once) so the SONG tab
    // has something useful to chain out of the box.
    memset(&currentSong, 0, sizeof(currentSong));
    strncpy(currentSong.name, "SONG 1", sizeof(currentSong.name)-1);
    currentSong.numSlots = 4;
    for (int i = 0; i < 4; i++) {
      currentSong.slots[i].patternIdx  = (uint8_t)i;
      currentSong.slots[i].repeatCount = 1;
    }
    calcInterval();
  }

  void setCallbacks(SeqNoteOnCB on, SeqNoteOffCB off) { _noteOnCB = on; _noteOffCB = off; }
  void setSynthEngine(SynthEngine* s) { synth = s; }

  void init() {
    for (int p = 0; p < MAX_PATTERNS; p++)
      for (int t = 0; t < MAX_SEQ_TRACKS; t++)
        patterns[p].tracks[t].init();
  }

  void play() {
    isPlaying  = true;
    lastTickUs = micros();
    extClockAccum = 0;
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
      SequencerTrack& tr = patterns[activePattern].tracks[t];
      tr.currentStep   = 0; tr.tickCount    = 0;
      tr.gateCountdown = 0; tr.repeatCounter = 0;
      tr.ratchetCount  = 0; tr.ratchetTick  = 0;
      triggerStep(t);
    }
    if (clockMode == CLOCK_OUT && _midiSendByteCB) _midiSendByteCB(0xFA);
  }

  void stop() {
    isPlaying = false;
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
      fireNoteOff(t);
      SequencerTrack& tr = patterns[activePattern].tracks[t];
      tr.currentStep = 0; tr.tickCount = 0; tr.gateCountdown = 0; tr.ratchetCount = 0;
    }
    if (clockMode == CLOCK_OUT && _midiSendByteCB) _midiSendByteCB(0xFC);
  }

  void tapTempo() {
    unsigned long now = millis();
    if (tapCount > 0 && (now - tapTimes[tapCount-1]) > 2000) tapCount = 0;
    if (tapCount < 4) tapTimes[tapCount++] = now;
    if (tapCount >= 2) {
      float avgMs = (float)(tapTimes[tapCount-1] - tapTimes[0]) / (float)(tapCount - 1);
      setBPM(60000.0f / avgMs);
    }
  }

  // Call every frame – processes all pending ticks (≤8 per call).
  // In CLOCK_EXT mode the tick driver is silent here (ticks are driven by
  // incoming MIDI 0xF8 messages via onMidiClockTick()).  In CLOCK_OUT mode
  // we additionally emit 24 PPQN out of midiSerial via the callback.
  void update(unsigned long /*unused*/) {
    if (!isPlaying) return;
    if (clockMode == CLOCK_EXT) return;  // ticks come from external clock
    unsigned long now = micros();
    int limit = 8;
    while (limit-- > 0 && (now - lastTickUs) >= tickIntervalUs) {
      lastTickUs += tickIntervalUs;
      processTick();
      // Emit MIDI clock when in OUT mode: 1/32 internal tick = 8 ticks/quarter,
      // 24 PPQN = 24 ticks/quarter ⇒ emit 3 clock bytes per internal tick.
      if (clockMode == CLOCK_OUT && _midiSendByteCB) {
        _midiSendByteCB(0xF8);
        _midiSendByteCB(0xF8);
        _midiSendByteCB(0xF8);
      }
    }
  }

  // ── MIDI clock sync API ───────────────────────────────────────────────────
  void setClockMode(ClockMode m) { clockMode = m; }
  ClockMode getClockMode() const { return clockMode; }
  void setMidiSendByteCallback(void (*cb)(uint8_t)) { _midiSendByteCB = cb; }

  // External MIDI clock hook (call from MIDIInput 0xF8 handler).
  // 24 PPQN → 1/32 ticks: every 3 external ticks = 1 internal tick.
  void onMidiClockTick() {
    if (clockMode != CLOCK_EXT || !isPlaying) return;
    if (++extClockAccum >= 3) {
      extClockAccum = 0;
      processTick();
    }
  }
  // Transport hooks from MIDI 0xFA/0xFB/0xFC.
  void onMidiClockStart()    { if (clockMode == CLOCK_EXT) { extClockAccum = 0; play(); } }
  void onMidiClockContinue() { if (clockMode == CLOCK_EXT && !isPlaying) isPlaying = true; }
  void onMidiClockStop()     { if (clockMode == CLOCK_EXT) stop(); }

  // ── Live MIDI recording API ───────────────────────────────────────────────
  void setRecordArmed(bool arm) { recordArmed = arm; }
  bool getRecordArmed() const   { return recordArmed; }
  void setRecordTrack(int t)    { if (t >= 0 && t < MAX_SEQ_TRACKS) recordTrack = t; }

  // Call from MIDI note-on handler.  When armed AND playing, writes the note
  // into the currently-playing step of the record track, quantizing to the
  // current step boundary.
  void recordNote(int note, int velocity) {
    if (!recordArmed || !isPlaying) return;
    SequencerTrack& tr = patterns[activePattern].tracks[recordTrack];
    int s = tr.currentStep;
    if (s < 0 || s >= MAX_SEQ_STEPS) return;
    tr.steps[s].active   = true;
    tr.steps[s].note     = constrain(note, 0, 127);
    tr.steps[s].velocity = constrain(velocity, 1, 127);
  }

  // ── Public API ────────────────────────────────────────────────────────────
  void setBPM(float v)       { bpm = constrain(v, 30.0f, 300.0f); calcInterval(); }
  void setSwing(int pct)     { swing = (uint8_t)constrain(pct, 50, 75); }
  int  getSwing() const      { return (int)swing; }
  void setActiveTrack(int t) { activeTrack = constrain(t, 0, MAX_SEQ_TRACKS-1); }
  void toggleFill()          { fillMode = !fillMode; }

  // ── Solo / volume / pan ───────────────────────────────────────────────────
  void toggleSolo(int t) {
    if (t < 0 || t >= MAX_SEQ_TRACKS) return;
    SequencerTrack& tr = patterns[activePattern].tracks[t];
    tr.soloed = !tr.soloed;
  }
  void setTrackVolume(int t, float v) {
    if (t < 0 || t >= MAX_SEQ_TRACKS) return;
    patterns[activePattern].tracks[t].trackVolume = constrain(v, 0.0f, 1.0f);
  }
  void setTrackPan(int t, float p) {
    if (t < 0 || t >= MAX_SEQ_TRACKS) return;
    patterns[activePattern].tracks[t].trackPan = constrain(p, -1.0f, 1.0f);
  }

  // ── Live pattern switching ───────────────────────────────────────────────
  // Queue a pattern.  In INSTANT mode the change takes effect on the next
  // tick (DJ-style live jump).  Otherwise it transitions at the next bar
  // boundary (musical "wait for it" behaviour).
  void jumpToPattern(int pat) {
    if (pat < 0 || pat >= MAX_PATTERNS) return;
    if (!isPlaying) { activePattern = pat; return; }
    nextPattern = pat;
  }
  void setInstantSwitch(bool on) { instantSwitch = on; }
  bool getInstantSwitch() const   { return instantSwitch; }

  void muteTrack(int t, bool m) {
    if (t < 0 || t >= MAX_SEQ_TRACKS) return;
    patterns[activePattern].tracks[t].muted = m;
    if (m) fireNoteOff(t);
  }
  void toggleMute(int t) {
    if (t < 0 || t >= MAX_SEQ_TRACKS) return;
    bool& m = patterns[activePattern].tracks[t].muted;
    m = !m;
    if (m) fireNoteOff(t);
  }

  void toggleStep(int track, int step) {
    if (track < 0 || track >= MAX_SEQ_TRACKS || step < 0 || step >= MAX_SEQ_STEPS) return;
    patterns[activePattern].tracks[track].steps[step].active ^= true;
  }
  void clearTrack(int t) {
    if (t >= 0 && t < MAX_SEQ_TRACKS) patterns[activePattern].tracks[t].clear();
  }
  void clearAll()  { for (int t = 0; t < MAX_SEQ_TRACKS; t++) clearTrack(t); }
  void randomizeTrack(int t) {
    if (t >= 0 && t < MAX_SEQ_TRACKS) patterns[activePattern].tracks[t].randomize();
  }
  void copyPattern(int from, int to) {
    if (from >= 0 && from < MAX_PATTERNS && to >= 0 && to < MAX_PATTERNS)
      patterns[to] = patterns[from];
  }
  void euclideanTrack(int t, int k, int n) {
    if (t >= 0 && t < MAX_SEQ_TRACKS) patterns[activePattern].tracks[t].euclidean(k, n);
  }

  // Getters
  int  getCurrentStep() { return patterns[activePattern].tracks[0].currentStep; }
  int  getTrackStep(int t) {
    return (t >= 0 && t < MAX_SEQ_TRACKS) ? patterns[activePattern].tracks[t].currentStep : 0;
  }
  bool  getIsPlaying()   { return isPlaying; }
  float getBPM()         { return bpm; }
  int   getActiveTrack() { return activeTrack; }
  bool  getFillMode()    { return fillMode; }
  SequencerTrack* getTrack(int t) {
    return (t >= 0 && t < MAX_SEQ_TRACKS) ? &patterns[activePattern].tracks[t] : NULL;
  }
  bool isStepActive(int t, int s) {
    if (t < 0 || t >= MAX_SEQ_TRACKS || s < 0 || s >= MAX_SEQ_STEPS) return false;
    return patterns[activePattern].tracks[t].steps[s].active;
  }

  // ── Storage: NVS (always) + optional SD card ─────────────────────────────
  // Patterns/songs are serialized once (packed form), then dispatched to
  // whichever backend is active.  SD is opt-in via -DUSE_SD_STORAGE.  When
  // an SD card is present, it is preferred for saves so you can fit 100+
  // patterns; NVS provides a guaranteed-available fallback (~10 KB budget).
  void openPrefs() {
    if (!prefsOpen) {
      prefs.begin("zombie_seq", false);
      prefsOpen = true;
    }
  }

  // Returns true if an SD card was mounted successfully and may be used
  // for persistent storage.  Call once at boot.
  bool initSDStorage() {
#ifdef USE_SD_STORAGE
    if (sdAvailable) return true;
    if (!SD.begin(SEQ_SD_CS)) return false;
    if (!SD.exists("/zombi")) SD.mkdir("/zombi");
    sdAvailable = true;
    return true;
#else
    return false;
#endif
  }
  bool isSDAvailable() const {
#ifdef USE_SD_STORAGE
    return sdAvailable;
#else
    return false;
#endif
  }

  // Packed-step pattern layout (smaller NVS footprint, fits 8 slots in <16 KB).
  //
  // Per step v3: 8 bytes (v2 was 6 bytes; v2 blobs are still loadable)
  //   B0: active(1)|tie(1)|condition(3)|ratchets(3 → +1)
  //   B1: note (0..127)
  //   B2: velocity (0..127)
  //   B3: gate (1..16) low 4 bits
  //   B4: probability (0..100)
  //   B5: reserved (must be 0)
  //   B6: cutoffLock (int8_t, -64..+63)         ← new v3
  //   B7: panLock    (int8_t, -64..+63)         ← new v3
  // Per track (after steps): length(2), divIdx(1), scale(1), rootNote(1), octave(1),
  //   muted(1), soloed(1), trackVolume(4f), trackPan(4f), soundPatchIdx(2), SynthPatch
  // Per pattern: 4 tracks back-to-back.
  // Blob: [u8 version][serialized pattern]
  static const size_t PACKED_STEP_SIZE_V2 = 6;
  static const size_t PACKED_STEP_SIZE    = 8;
  static const size_t PACKED_TRACK_SIZE = (PACKED_STEP_SIZE * MAX_SEQ_STEPS)
                                          + 2 + 1 + 1 + 1 + 1 + 1 + 1
                                          + 4 + 4 + 2 + sizeof(SynthPatch);
  static const size_t PACKED_PAT_SIZE   = PACKED_TRACK_SIZE * MAX_SEQ_TRACKS;

  static void packTrack(const SequencerTrack& tr, uint8_t* out) {
    uint8_t* p = out;
    for (int s = 0; s < MAX_SEQ_STEPS; s++) {
      const SequencerStep& st = tr.steps[s];
      uint8_t rat = (uint8_t)((st.ratchets > 0 ? st.ratchets - 1 : 0) & 0x07);
      uint8_t b0 = (st.active ? 0x80 : 0) | (st.tie ? 0x40 : 0)
                 | (((uint8_t)st.condition & 0x07) << 3) | rat;
      *p++ = b0;
      *p++ = (uint8_t)(st.note & 0x7F);
      *p++ = (uint8_t)(st.velocity & 0x7F);
      *p++ = (uint8_t)(st.gate & 0x1F);
      *p++ = (uint8_t)(st.probability);
      *p++ = 0;
      *p++ = (uint8_t)st.cutoffLock;
      *p++ = (uint8_t)st.panLock;
    }
    *(int16_t*)p = (int16_t)tr.length;  p += 2;
    *p++ = (uint8_t)tr.divIdx;
    *p++ = (uint8_t)tr.scale;
    *p++ = (uint8_t)tr.rootNote;
    *p++ = (uint8_t)tr.octave;
    *p++ = tr.muted ? 1 : 0;
    *p++ = tr.soloed ? 1 : 0;
    memcpy(p, &tr.trackVolume, sizeof(float)); p += 4;
    memcpy(p, &tr.trackPan,    sizeof(float)); p += 4;
    *(int16_t*)p = (int16_t)tr.soundPatchIdx; p += 2;
    memcpy(p, &tr.trackPatch, sizeof(SynthPatch));
  }

  // version=2 → 6 byte/step (no P-Locks); version=3 → 8 byte/step.
  static void unpackTrack(const uint8_t* in, SequencerTrack& tr, uint8_t version) {
    tr.init();   // sane defaults for any field we don't touch
    const uint8_t* p = in;
    for (int s = 0; s < MAX_SEQ_STEPS; s++) {
      uint8_t b0 = *p++;
      tr.steps[s].active      = (b0 & 0x80) != 0;
      tr.steps[s].tie         = (b0 & 0x40) != 0;
      tr.steps[s].condition   = (b0 >> 3) & 0x07;
      tr.steps[s].ratchets    = (uint8_t)((b0 & 0x07) + 1);
      tr.steps[s].note        = *p++ & 0x7F;
      tr.steps[s].velocity    = *p++ & 0x7F;
      tr.steps[s].gate        = *p++ & 0x1F; if (tr.steps[s].gate == 0) tr.steps[s].gate = 1;
      tr.steps[s].probability = *p++;
      p++;  // reserved
      if (version >= 3) {
        tr.steps[s].cutoffLock = (int8_t)*p++;
        tr.steps[s].panLock    = (int8_t)*p++;
      } else {
        tr.steps[s].cutoffLock = 0;
        tr.steps[s].panLock    = 0;
      }
    }
    tr.length        = (int)(*(int16_t*)p); p += 2;
    tr.divIdx        = *p++;
    tr.scale         = *p++;
    tr.rootNote      = *p++;
    tr.octave        = *p++;
    tr.muted         = (*p++) != 0;
    tr.soloed        = (*p++) != 0;
    memcpy(&tr.trackVolume, p, sizeof(float)); p += 4;
    memcpy(&tr.trackPan,    p, sizeof(float)); p += 4;
    tr.soundPatchIdx = (int)(*(int16_t*)p); p += 2;
    memcpy(&tr.trackPatch, p, sizeof(SynthPatch));
  }

  bool savePattern(int slot) {
    if (slot < 0 || slot >= MAX_PATTERNS) return false;
    const size_t blobLen = 1 + PACKED_PAT_SIZE;
    uint8_t* buf = (uint8_t*)malloc(blobLen);
    if (!buf) return false;
    buf[0] = 3;  // P-Lock-aware layout
    for (int t = 0; t < MAX_SEQ_TRACKS; t++)
      packTrack(patterns[slot].tracks[t],
                buf + 1 + t * PACKED_TRACK_SIZE);

#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/pat_%d.bin", slot);
      File f = SD.open(path, FILE_WRITE);
      if (f) {
        size_t w = f.write(buf, blobLen);
        f.close();
        free(buf);
        return w == blobLen;
      }
    }
#endif
    openPrefs();
    char key[8];
    snprintf(key, sizeof(key), "pat_%d", slot);
    size_t written = prefs.putBytes(key, buf, blobLen);
    free(buf);
    return written == blobLen;
  }

  bool loadPattern(int slot) {
    if (slot < 0 || slot >= MAX_PATTERNS) return false;
    // Accept both v2 (no P-Locks) and v3 (with P-Locks) blob sizes.
    const size_t v3BlobLen = 1 + PACKED_PAT_SIZE;
    const size_t v2TrackSz = PACKED_STEP_SIZE_V2 * MAX_SEQ_STEPS
                             + (PACKED_TRACK_SIZE - PACKED_STEP_SIZE * MAX_SEQ_STEPS);
    const size_t v2BlobLen = 1 + v2TrackSz * MAX_SEQ_TRACKS;
    uint8_t* buf = (uint8_t*)malloc(v3BlobLen);
    if (!buf) return false;

    size_t blobLen = 0;
#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/pat_%d.bin", slot);
      File f = SD.open(path, FILE_READ);
      if (f) {
        size_t sz = f.size();
        if (sz == v3BlobLen || sz == v2BlobLen) {
          f.read(buf, sz);
          blobLen = sz;
        }
        f.close();
      }
    }
#endif
    if (blobLen == 0) {
      openPrefs();
      char key[8];
      snprintf(key, sizeof(key), "pat_%d", slot);
      if (!prefs.isKey(key)) { free(buf); return false; }
      size_t stored = prefs.getBytesLength(key);
      if (stored != v3BlobLen && stored != v2BlobLen) { free(buf); return false; }
      prefs.getBytes(key, buf, stored);
      blobLen = stored;
    }
    uint8_t version = buf[0];
    if (version != 2 && version != 3) { free(buf); return false; }
    size_t trackSz = (version == 3) ? PACKED_TRACK_SIZE : v2TrackSz;
    for (int t = 0; t < MAX_SEQ_TRACKS; t++)
      unpackTrack(buf + 1 + t * trackSz, patterns[slot].tracks[t], version);
    free(buf);
    return true;
  }

  bool isPatternSaved(int slot) {
    if (slot < 0 || slot >= MAX_PATTERNS) return false;
#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/pat_%d.bin", slot);
      if (SD.exists(path)) return true;
    }
#endif
    openPrefs();
    char key[8];
    snprintf(key, sizeof(key), "pat_%d", slot);
    return prefs.isKey(key);
  }

  bool saveSong(int slot) {
    if (slot < 0 || slot >= NUM_SONGS) return false;
    const size_t blobLen = 1 + sizeof(Song);
    uint8_t* buf = (uint8_t*)malloc(blobLen);
    if (!buf) return false;
    buf[0] = 1;
    memcpy(buf + 1, &currentSong, sizeof(Song));

#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/song_%d.bin", slot);
      File f = SD.open(path, FILE_WRITE);
      if (f) {
        size_t w = f.write(buf, blobLen);
        f.close();
        free(buf);
        return w == blobLen;
      }
    }
#endif
    openPrefs();
    char key[10];
    snprintf(key, sizeof(key), "song_%d", slot);
    size_t written = prefs.putBytes(key, buf, blobLen);
    free(buf);
    return written == blobLen;
  }

  bool loadSong(int slot) {
    if (slot < 0 || slot >= NUM_SONGS) return false;
    const size_t blobLen = 1 + sizeof(Song);
    uint8_t* buf = (uint8_t*)malloc(blobLen);
    if (!buf) return false;

    bool loaded = false;
#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/song_%d.bin", slot);
      File f = SD.open(path, FILE_READ);
      if (f && f.size() == blobLen) {
        size_t r = f.read(buf, blobLen);
        f.close();
        loaded = (r == blobLen);
      }
    }
#endif
    if (!loaded) {
      openPrefs();
      char key[10];
      snprintf(key, sizeof(key), "song_%d", slot);
      if (!prefs.isKey(key)) { free(buf); return false; }
      size_t stored = prefs.getBytesLength(key);
      if (stored != blobLen) { free(buf); return false; }
      prefs.getBytes(key, buf, blobLen);
      loaded = true;
    }
    if (!loaded || buf[0] != 1) { free(buf); return false; }
    memcpy(&currentSong, buf + 1, sizeof(Song));
    free(buf);
    if (currentSong.numSlots > MAX_SONG_SLOTS) currentSong.numSlots = MAX_SONG_SLOTS;
    currentSong.name[sizeof(currentSong.name)-1] = '\0';
    return true;
  }

  bool isSongSaved(int slot) {
    if (slot < 0 || slot >= NUM_SONGS) return false;
#ifdef USE_SD_STORAGE
    if (sdAvailable) {
      char path[32];
      snprintf(path, sizeof(path), "/zombi/song_%d.bin", slot);
      if (SD.exists(path)) return true;
    }
#endif
    openPrefs();
    char key[10];
    snprintf(key, sizeof(key), "song_%d", slot);
    return prefs.isKey(key);
  }

  // ── Song mode ─────────────────────────────────────────────────────────────
  void playSong() {
    if (currentSong.numSlots == 0) return;
    songMode        = true;
    songSlotIdx     = 0;
    const SongSlot& s = currentSong.slots[0];
    activePattern   = (int)s.patternIdx % MAX_PATTERNS;
    songRepeatsLeft = (s.repeatCount < 1) ? 1 : (int)s.repeatCount;
    nextPattern     = -1;
    play();
  }

  void stopSong() {
    songMode = false;
    stop();
  }

  void songInsertSlot(int after) {
    if (currentSong.numSlots >= MAX_SONG_SLOTS) return;
    int pos = constrain(after + 1, 0, (int)currentSong.numSlots);
    for (int i = (int)currentSong.numSlots; i > pos; --i)
      currentSong.slots[i] = currentSong.slots[i-1];
    currentSong.slots[pos].patternIdx  = 0;
    currentSong.slots[pos].repeatCount = 1;
    currentSong.numSlots++;
  }

  void songDeleteSlot(int pos) {
    if (currentSong.numSlots <= 1) return;
    if (pos < 0 || pos >= (int)currentSong.numSlots) return;
    for (int i = pos; i < (int)currentSong.numSlots - 1; ++i)
      currentSong.slots[i] = currentSong.slots[i+1];
    currentSong.numSlots--;
  }

  void songSetSlotPattern(int pos, int patIdx) {
    if (pos < 0 || pos >= (int)currentSong.numSlots) return;
    currentSong.slots[pos].patternIdx =
        (uint8_t)constrain(patIdx, 0, MAX_PATTERNS - 1);
  }

  void songSetSlotRepeats(int pos, int reps) {
    if (pos < 0 || pos >= (int)currentSong.numSlots) return;
    currentSong.slots[pos].repeatCount = (uint8_t)constrain(reps, 1, 255);
  }

  void songClear() {
    memset(&currentSong, 0, sizeof(currentSong));
    strncpy(currentSong.name, "SONG", sizeof(currentSong.name)-1);
    currentSong.numSlots = 4;
    for (int i = 0; i < 4; i++) {
      currentSong.slots[i].patternIdx  = (uint8_t)i;
      currentSong.slots[i].repeatCount = 1;
    }
  }
};

#endif

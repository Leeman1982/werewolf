// WerewolfAmyS3 — WOLF sequencer
//
// Port of the werewolf/ZombieSynth 16-step, 4-track step sequencer, re-timed
// to the 48 PPQ master clock and re-plumbed through the MIDI router (so a
// track can drive the WOLF synth, the GM engine or external gear, depending
// on the routing matrix and its channel).
#pragma once
#include <Arduino.h>
#include "config.h"
#include "midi_router.h"

struct WStep {
  uint8_t note = 60;
  uint8_t vel  = 100;
  uint8_t gate = 75;       // % of step
  uint8_t prob = 100;      // % chance
  bool    accent = false;
  bool    on = false;
};

struct WTrack {
  WStep   steps[WSEQ_STEPS];
  uint8_t channel = 1;     // MIDI channel stamped on events (via router)
  int8_t  transpose = 0;
  bool    mute = false;
};

struct WPattern {
  WTrack  tracks[WSEQ_TRACKS];
  uint8_t length = WSEQ_STEPS;  // 1..16
  uint8_t swing  = 0;           // 0..50 (% of a step the off-beats are delayed)
};

class WolfSeq {
public:
  WPattern patterns[WSEQ_PATTERNS];
  uint8_t  patIdx = 0;
  uint8_t  editTrack = 0;
  volatile bool playing = false;
  volatile uint8_t playStep = 0;

  WPattern& pat() { return patterns[patIdx]; }
  WTrack&   trk() { return pat().tracks[editTrack]; }

  void play(uint32_t nowTick) {
    _nextStepTick = nowTick + 1;   // start on the next tick
    _step = 0;
    playing = true;
    router.sendStart();
  }
  void stop() {
    playing = false;
    flushOffs(true);
    router.sendStop();
  }
  void toggle(uint32_t nowTick) { playing ? stop() : play(nowTick); }

  // one master-clock tick
  void onTick(uint32_t t) {
    flushOffs(false, t);
    if (!playing) return;
    if ((int32_t)(t - _nextStepTick) < 0) return;

    WPattern& p = pat();
    if (_step >= p.length) _step = 0;
    playStep = _step;

    for (int tr = 0; tr < WSEQ_TRACKS; tr++) {
      WTrack& track = p.tracks[tr];
      const WStep& s = track.steps[_step];
      if (!s.on || track.mute) continue;
      if (s.prob < 100 && (uint8_t)random(100) >= s.prob) continue;
      int note = constrain(s.note + track.transpose, 0, 127);
      uint8_t vel = s.accent ? min(127, s.vel + 30) : s.vel;
      router.noteOn(SRC_WOLFSEQ, track.channel - 1, note, vel);
      queueOff(t + max(1, (int)(TICKS_PER_16TH * s.gate / 100)), track.channel - 1, note);
    }

    // schedule next step with swing on the off-16ths
    uint32_t interval = TICKS_PER_16TH;
    if (p.swing && (_step & 1) == 0)
      interval += (TICKS_PER_16TH * p.swing) / 100;       // delay upcoming off-beat
    else if (p.swing && (_step & 1) == 1)
      interval -= (TICKS_PER_16TH * p.swing) / 100;
    _nextStepTick += interval;
    _step = (_step + 1) % p.length;
  }

  // ── Pattern operations (ported from ZombieSynth) ──────────────────────────
  void clearTrack(int tr) {
    for (int i = 0; i < WSEQ_STEPS; i++) pat().tracks[tr].steps[i] = WStep();
  }
  void randomizeTrack(int tr) {
    static const uint8_t MINOR[] = { 0, 2, 3, 5, 7, 8, 10 };
    WTrack& track = pat().tracks[tr];
    for (int i = 0; i < WSEQ_STEPS; i++) {
      WStep& s = track.steps[i];
      s.on   = random(100) < 55;
      s.note = 48 + 12 * random(2) + MINOR[random(7)];
      s.vel  = 70 + random(50);
      s.gate = 40 + random(55);
      s.accent = random(100) < 20;
      s.prob = 100;
    }
  }
  void shiftTrack(int tr, int dir) {
    WTrack& track = pat().tracks[tr];
    WStep tmp[WSEQ_STEPS];
    for (int i = 0; i < WSEQ_STEPS; i++)
      tmp[(i + dir + WSEQ_STEPS) % WSEQ_STEPS] = track.steps[i];
    memcpy(track.steps, tmp, sizeof(tmp));
  }
  void reverseTrack(int tr) {
    WTrack& track = pat().tracks[tr];
    for (int i = 0; i < WSEQ_STEPS / 2; i++) {
      WStep t = track.steps[i];
      track.steps[i] = track.steps[WSEQ_STEPS - 1 - i];
      track.steps[WSEQ_STEPS - 1 - i] = t;
    }
  }
  void copyPattern(uint8_t src, uint8_t dst) {
    if (src < WSEQ_PATTERNS && dst < WSEQ_PATTERNS) patterns[dst] = patterns[src];
  }

private:
  struct POff { uint32_t tick; uint8_t ch, note; bool used; };
  static const int MAX_OFFS = 32;
  POff _offs[MAX_OFFS] = {};
  uint32_t _nextStepTick = 0;
  uint8_t  _step = 0;

  void queueOff(uint32_t tick, uint8_t ch, uint8_t note) {
    for (int i = 0; i < MAX_OFFS; i++)
      if (!_offs[i].used) { _offs[i] = { tick, ch, note, true }; return; }
    // table full: release oldest
    router.noteOff(SRC_WOLFSEQ, ch, note);
  }
  void flushOffs(bool all, uint32_t t = 0) {
    for (int i = 0; i < MAX_OFFS; i++) {
      if (!_offs[i].used) continue;
      if (all || (int32_t)(t - _offs[i].tick) >= 0) {
        router.noteOff(SRC_WOLFSEQ, _offs[i].ch, _offs[i].note);
        _offs[i].used = false;
      }
    }
  }
};

extern WolfSeq wolfSeq;

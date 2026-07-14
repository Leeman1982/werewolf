// WerewolfAmyS3 — GM sequencer
//
// The Medusa sequencer engine (patterns, chains, swing, probability, accent,
// slide/tie, per-track length polymeter), grown from 1 track to 16: one track
// per GM channel, playing the embedded SoundFont through the MIDI router
// (and/or external gear — track events are ordinary routed MIDI).
// Track 10 is the GM drum channel.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "midi_router.h"
#include "gm_engine.h"

struct GStep {
  uint8_t note = 60;       // drum key on channel 10
  uint8_t vel  = 100;
  uint8_t gate = 75;       // % of step; ties extend through slide flag
  uint8_t prob = 100;
  bool    accent = false;
  bool    slide  = false;  // legato/tie into the next step (Medusa slide)
  bool    on = false;
};

struct GTrack {
  GStep   steps[GMSEQ_STEPS];
  uint8_t program = 0;      // GM program 0-127 (ignored by drum channel bank)
  uint8_t length  = GMSEQ_STEPS;  // 1..16 per-track polymeter (Medusa-style)
  uint8_t level   = 100;    // CC7 0..127
  int8_t  transpose = 0;
  bool    mute = false;
};

struct GPattern {
  GTrack  tracks[GMSEQ_TRACKS];
  uint8_t swing = 0;        // 0..50
};

struct GChainEntry { int8_t patternIdx = -1; uint8_t repeats = 1; };

class GMSeq {
public:
  GPattern patterns[GMSEQ_PATTERNS];
  GChainEntry chain[GMSEQ_CHAIN_LEN];
  uint8_t chainLen = 0;
  uint8_t patIdx = 0;
  uint8_t editTrack = 0;
  volatile bool playing = false;
  volatile uint8_t playStep = 0;   // 0..15 master step (16th notes)

  GPattern& pat() { return patterns[patIdx]; }
  GTrack&   trk() { return pat().tracks[editTrack]; }

  void begin() {
    // sensible starting point: kick as the drum-track default note, and a
    // piano/bass/strings program spread on the first melodic tracks
    for (int p = 0; p < GMSEQ_PATTERNS; p++) {
      for (int s = 0; s < GMSEQ_STEPS; s++)
        patterns[p].tracks[GM_DRUM_CHANNEL].steps[s].note = 36;
      patterns[p].tracks[0].program = 0;    // piano  ch 1
      patterns[p].tracks[1].program = 33;   // bass   ch 2
      patterns[p].tracks[2].program = 48;   // strings ch 3
    }
  }

  void applyPrograms() {
    GPattern& p = pat();
    for (int tr = 0; tr < GMSEQ_TRACKS; tr++) {
      if (tr != GM_DRUM_CHANNEL)
        router.send(SRC_GMSEQ, 0xC0 | tr, p.tracks[tr].program, 0);
      router.send(SRC_GMSEQ, 0xB0 | tr, 7, p.tracks[tr].level);
    }
  }

  void play(uint32_t nowTick) {
    _nextStepTick = nowTick + 1;
    _step = 0;
    _chainPos = _chainRepeat = 0;
    if (chainLen > 0 && chain[0].patternIdx >= 0) patIdx = chain[0].patternIdx;
    applyPrograms();
    playing = true;
    router.sendStart();
  }
  void stop() {
    playing = false;
    flushOffs(true);
    for (int tr = 0; tr < GMSEQ_TRACKS; tr++)
      router.send(SRC_GMSEQ, 0xB0 | tr, 123, 0);
    router.sendStop();
  }
  void toggle(uint32_t nowTick) { playing ? stop() : play(nowTick); }

  void onTick(uint32_t t) {
    flushOffs(false, t);
    if (!playing) return;
    if ((int32_t)(t - _nextStepTick) < 0) return;

    GPattern& p = pat();
    playStep = _step & 15;

    for (int tr = 0; tr < GMSEQ_TRACKS; tr++) {
      GTrack& track = p.tracks[tr];
      uint8_t local = _step % track.length;       // per-track polymeter
      const GStep& s = track.steps[local];

      if (!s.on || track.mute) { releaseHeld(tr, t); continue; }
      if (s.prob < 100 && (uint8_t)random(100) >= s.prob) { releaseHeld(tr, t); continue; }

      int note = constrain(s.note + (tr == GM_DRUM_CHANNEL ? 0 : track.transpose), 0, 127);
      uint8_t vel = s.accent ? min(127, s.vel + 30) : s.vel;

      bool tieFromPrev = _held[tr].used && _held[tr].slide;
      if (tieFromPrev && _held[tr].note == note) {
        // Same note tied: just extend the gate
        _held[tr].offTick = t + gateTicks(s);
        _held[tr].slide = s.slide;
        continue;
      }
      uint8_t prevNote = _held[tr].note;
      bool hadPrev = _held[tr].used;

      router.noteOn(SRC_GMSEQ, tr, note, vel);
      // Medusa legato overlap: release the previous note AFTER the new one
      if (hadPrev) router.noteOff(SRC_GMSEQ, tr, prevNote);

      _held[tr].used = true;
      _held[tr].note = note;
      _held[tr].slide = s.slide;
      _held[tr].offTick = t + gateTicks(s);
    }

    uint32_t interval = TICKS_PER_16TH;
    if (p.swing && (_step & 1) == 0)      interval += (TICKS_PER_16TH * p.swing) / 100;
    else if (p.swing && (_step & 1) == 1) interval -= (TICKS_PER_16TH * p.swing) / 100;
    _nextStepTick += interval;
    _step = (_step + 1) & 15;

    // chain advance at bar boundaries (Medusa)
    if (_step == 0 && chainLen > 0) {
      _chainRepeat++;
      if (_chainRepeat >= chain[_chainPos].repeats) {
        _chainRepeat = 0;
        _chainPos = (_chainPos + 1) % chainLen;
        if (chain[_chainPos].patternIdx < 0) _chainPos = 0;
        int8_t next = chain[_chainPos].patternIdx;
        if (next >= 0 && next < GMSEQ_PATTERNS && next != patIdx) {
          patIdx = next;
          applyPrograms();
        }
      }
    }
  }

  // ── pattern ops ────────────────────────────────────────────────────────────
  void clearTrack(int tr) {
    GTrack& t = pat().tracks[tr];
    uint8_t prog = t.program, lvl = t.level, len = t.length;
    t = GTrack();
    t.program = prog; t.level = lvl; t.length = len;
  }
  void randomizeTrack(int tr) {
    GTrack& track = pat().tracks[tr];
    if (tr == GM_DRUM_CHANNEL) {
      static const uint8_t KIT[] = { 36, 38, 42, 46, 39, 45, 49 };
      for (int i = 0; i < GMSEQ_STEPS; i++) {
        GStep& s = track.steps[i];
        s.on = (i % 4 == 0) || random(100) < 30;
        s.note = KIT[random(7)];
        s.vel = 80 + random(40);
        s.gate = 50; s.prob = 100; s.slide = false;
        s.accent = (i % 8 == 0);
      }
    } else {
      static const uint8_t MINOR[] = { 0, 2, 3, 5, 7, 8, 10 };
      for (int i = 0; i < GMSEQ_STEPS; i++) {
        GStep& s = track.steps[i];
        s.on = random(100) < 45;
        s.note = 48 + 12 * random(2) + MINOR[random(7)];
        s.vel = 70 + random(50);
        s.gate = 40 + random(55);
        s.prob = 100;
        s.slide = random(100) < 12;
        s.accent = random(100) < 15;
      }
    }
  }
  void copyPattern(uint8_t src, uint8_t dst) {
    if (src < GMSEQ_PATTERNS && dst < GMSEQ_PATTERNS) patterns[dst] = patterns[src];
  }
  void setChain(const GChainEntry* c, uint8_t len) {
    chainLen = min(len, (uint8_t)GMSEQ_CHAIN_LEN);
    memcpy(chain, c, chainLen * sizeof(GChainEntry));
    _chainPos = _chainRepeat = 0;
  }

private:
  struct Held { uint32_t offTick; uint8_t note; bool slide; bool used; };
  Held _held[GMSEQ_TRACKS] = {};
  uint32_t _nextStepTick = 0;
  uint8_t  _step = 0;
  uint8_t  _chainPos = 0, _chainRepeat = 0;

  static uint32_t gateTicks(const GStep& s) {
    return max(1, (int)(TICKS_PER_16TH * s.gate / 100));
  }
  // An inactive/skipped step ends any note that was held over by a slide
  // (Medusa: sendNoteOff() on inactive steps).
  void releaseHeld(int tr, uint32_t t) {
    (void)t;
    if (_held[tr].used && _held[tr].slide) {
      router.noteOff(SRC_GMSEQ, tr, _held[tr].note);
      _held[tr].used = false;
    }
  }
  void flushOffs(bool all, uint32_t t = 0) {
    for (int tr = 0; tr < GMSEQ_TRACKS; tr++) {
      if (!_held[tr].used) continue;
      // slide holds the note into the next step; otherwise honour the gate
      if (all || ((int32_t)(t - _held[tr].offTick) >= 0 && !_held[tr].slide)) {
        router.noteOff(SRC_GMSEQ, tr, _held[tr].note);
        _held[tr].used = false;
      }
    }
  }
};

extern GMSeq gmSeq;

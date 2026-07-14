// WerewolfAmyS3 — arpeggiator engine
//
// The 50-pattern ZombieSynth arpeggiator, re-timed to the 48 PPQ master clock
// (with proper triplet/dotted/swing divisions) and routed through the MIDI
// router as SRC_ARP.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "arpeggiator_patterns.h"
#include "midi_router.h"

enum ArpDiv : uint8_t { DIV_8TH, DIV_16TH, DIV_32ND, DIV_8T, DIV_16T, DIV_8D, NUM_ARP_DIVS };
static const char* const ARP_DIV_NAMES[NUM_ARP_DIVS] =
  { "1/8", "1/16", "1/32", "1/8T", "1/16T", "1/8." };
static const uint8_t ARP_DIV_TICKS[NUM_ARP_DIVS] = { 24, 12, 6, 16, 8, 36 };

class ArpEngine {
public:
  Arpeggiator core;
  bool   enabled = false;
  bool   latch = false;
  ArpDiv division = DIV_16TH;
  uint8_t velocity = 100;

  // feed from router/keyboard when enabled
  void noteOn(uint8_t note) {
    if (latch && core.getNoteCount() == 0) core.allNotesOff();
    core.noteOn(note);
  }
  void noteOff(uint8_t note) {
    if (!latch) core.noteOff(note);
  }
  void clearLatch() { core.allNotesOff(); killNote(); }

  void setEnabled(bool en) {
    enabled = en;
    if (!en) { core.allNotesOff(); killNote(); }
  }

  void onTick(uint32_t t) {
    // gate off
    if (_curNote >= 0 && (int32_t)(t - _offTick) >= 0) killNote();
    if (!enabled || !core.getIsPlaying()) return;
    if ((int32_t)(t - _nextTick) < 0) return;

    uint8_t stepTicks = ARP_DIV_TICKS[division];
    // rhythm variants from the pattern list
    ArpPattern p = core.getPattern();
    if (p == ARP_TRIPLETS) stepTicks = 8;
    else if (p == ARP_DOTTED) stepTicks = 18;

    int note = core.stepNote();
    if (note >= 0 && note <= 127) {
      killNote();
      router.noteOn(SRC_ARP, router.localChannel - 1, note, velocity);
      _curNote = note;
      _offTick = t + max(1, stepTicks * core.getGateLength() / 100);
    }
    uint32_t interval = stepTicks;
    if (p == ARP_SWING) interval = (_swingPhase ^= 1) ? stepTicks + stepTicks / 3
                                                      : stepTicks - stepTicks / 3;
    _nextTick = t + interval;
  }

private:
  int32_t  _curNote = -1;
  uint32_t _offTick = 0, _nextTick = 0;
  uint8_t  _swingPhase = 0;

  void killNote() {
    if (_curNote >= 0) {
      router.noteOff(SRC_ARP, router.localChannel - 1, _curNote);
      _curNote = -1;
    }
  }
};

extern ArpEngine arp;

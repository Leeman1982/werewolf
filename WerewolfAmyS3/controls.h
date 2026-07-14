// WerewolfAmyS3 — hardware panel: 2x CD74HC4067 multiplexers
//
//   MUX A: 16 potentiometers → PIN_MUX_POTS (ADC1), oversampled + smoothed,
//          soft-takeover ("pickup") so pots don't jump values edited on the
//          touch screen or via MIDI.
//   MUX B: 16 momentary switches to GND → PIN_MUX_SW (INPUT_PULLUP), debounced.
//
// Pot → parameter and switch → action maps are fixed panel assignments
// (documented in WIRING.md / MIDI_MAP.md); pots also emit their mapped MIDI CC
// through the router, so the panel doubles as a MIDI controller.
#pragma once
#include <Arduino.h>
#include "config.h"

// What each pot controls (via WolfSynth::handleCC): panel CC map
static const uint8_t POT_CC_MAP[NUM_POTS] = {
  12,  // P1  OSC1 level
  13,  // P2  OSC2 level
  14,  // P3  OSC3 level
  15,  // P4  OSC1 pulse width
  74,  // P5  filter cutoff
  71,  // P6  resonance
  70,  // P7  filter env amount
  5,   // P8  glide
  73,  // P9  amp attack
  75,  // P10 amp decay
  79,  // P11 amp sustain
  72,  // P12 amp release
  76,  // P13 LFO rate
  77,  // P14 LFO → filter
  91,  // P15 reverb
  7,   // P16 master volume
};

static const char* const POT_NAMES[NUM_POTS] = {
  "OSC1 LVL", "OSC2 LVL", "OSC3 LVL", "OSC1 PW",
  "CUTOFF", "RESO", "ENV AMT", "GLIDE",
  "ATTACK", "DECAY", "SUSTAIN", "RELEASE",
  "LFO RATE", "LFO>FLT", "REVERB", "VOLUME",
};

enum SwitchAction : uint8_t {
  SW_OSC1_WAVE, SW_OSC2_WAVE, SW_OSC3_WAVE, SW_FILTER_TYPE,
  SW_LFO_WAVE,  SW_ARP_TOGGLE, SW_ARP_LATCH, SW_WSEQ_PLAY,
  SW_GMSEQ_PLAY, SW_TAP_TEMPO, SW_PRESET_PREV, SW_PRESET_NEXT,
  SW_MODE_SYNTH, SW_MODE_GMSEQ, SW_SHIFT, SW_PANIC,
};

static const char* const SWITCH_NAMES[NUM_SWITCHES] = {
  "OSC1 WAVE", "OSC2 WAVE", "OSC3 WAVE", "FILT TYPE",
  "LFO WAVE", "ARP ON", "ARP LATCH", "WSEQ PLAY",
  "GM PLAY", "TAP TEMPO", "PRESET -", "PRESET +",
  "SYNTH PG", "GM PG", "SHIFT", "PANIC",
};

class Panel {
public:
  void begin() {
    pinMode(PIN_MUX_S0, OUTPUT);
    pinMode(PIN_MUX_S1, OUTPUT);
    pinMode(PIN_MUX_S2, OUTPUT);
    pinMode(PIN_MUX_S3, OUTPUT);
    pinMode(PIN_MUX_SW, INPUT_PULLUP);
    analogReadResolution(12);
    analogSetPinAttenuation(PIN_MUX_POTS, ADC_11db);
    for (int i = 0; i < NUM_POTS; i++) { _potVal[i] = -1; _potSmooth[i] = 0; }
  }

  // Call at ~1 kHz max from the main loop; scans one channel per call to keep
  // the loop snappy (full scan = 16 calls ≈ 16 ms at 1 kHz).
  void scan() {
    select(_scanIdx);
    delayMicroseconds(5);                       // mux settle

    // pot: 2x oversample
    int raw = analogRead(PIN_MUX_POTS);
    raw += analogRead(PIN_MUX_POTS);
    raw >>= 1;
    _potSmooth[_scanIdx] = (_potSmooth[_scanIdx] * 3 + raw) >> 2;
    int v127 = _potSmooth[_scanIdx] >> 5;       // 12-bit → 0..127
    if (v127 > 127) v127 = 127;
    if (_potVal[_scanIdx] < 0) {
      _potVal[_scanIdx] = v127;                 // first read: sync, no event
    } else if (abs(v127 - _potVal[_scanIdx]) >= 2) {
      // pickup: if a pot went stale (param moved elsewhere), require crossing
      if (_potStale[_scanIdx]) {
        if (abs(v127 - _potTarget[_scanIdx]) <= 3) _potStale[_scanIdx] = false;
      }
      if (!_potStale[_scanIdx]) {
        _potVal[_scanIdx] = v127;
        _potMoved |= (1u << _scanIdx);
      } else {
        _potVal[_scanIdx] = v127;               // track silently
      }
    }

    // switch (active low), debounced per channel
    bool pressed = digitalRead(PIN_MUX_SW) == LOW;
    uint32_t now = millis();
    if (pressed != _swRaw[_scanIdx]) {
      _swRaw[_scanIdx] = pressed;
      _swTime[_scanIdx] = now;
    } else if ((now - _swTime[_scanIdx]) > 25 && pressed != _swState[_scanIdx]) {
      _swState[_scanIdx] = pressed;
      if (pressed) _swPressed |= (1u << _scanIdx);
      else         _swReleased |= (1u << _scanIdx);
    }

    _scanIdx = (_scanIdx + 1) & (NUM_POTS - 1);
  }

  // Mark a pot's parameter as changed elsewhere; pot must "pick up" the value.
  void stale(int pot, uint8_t currentValue127) {
    if (pot < 0 || pot >= NUM_POTS) return;
    _potStale[pot] = true;
    _potTarget[pot] = currentValue127;
  }
  void staleAll() {
    for (int i = 0; i < NUM_POTS; i++)
      if (_potVal[i] >= 0) { _potStale[i] = true; _potTarget[i] = _potVal[i] > 63 ? 0 : 127; }
  }

  // Consume events -------------------------------------------------------
  bool potMoved(int i, uint8_t* value127) {
    if (!(_potMoved & (1u << i))) return false;
    _potMoved &= ~(1u << i);
    *value127 = (uint8_t)_potVal[i];
    return true;
  }
  bool switchPressed(int i) {
    if (!(_swPressed & (1u << i))) return false;
    _swPressed &= ~(1u << i);
    return true;
  }
  bool shiftHeld() { return _swState[SW_SHIFT]; }

private:
  void select(uint8_t ch) {
    digitalWrite(PIN_MUX_S0, ch & 1);
    digitalWrite(PIN_MUX_S1, (ch >> 1) & 1);
    digitalWrite(PIN_MUX_S2, (ch >> 2) & 1);
    digitalWrite(PIN_MUX_S3, (ch >> 3) & 1);
  }

  uint8_t  _scanIdx = 0;
  int16_t  _potVal[NUM_POTS];
  int32_t  _potSmooth[NUM_POTS];
  bool     _potStale[NUM_POTS] = {};
  uint8_t  _potTarget[NUM_POTS] = {};
  volatile uint32_t _potMoved = 0;
  bool     _swRaw[NUM_SWITCHES] = {};
  bool     _swState[NUM_SWITCHES] = {};
  uint32_t _swTime[NUM_SWITCHES] = {};
  volatile uint32_t _swPressed = 0, _swReleased = 0;
};

extern Panel panel;

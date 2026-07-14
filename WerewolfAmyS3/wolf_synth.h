// WerewolfAmyS3 — the WEREWOLF synth: a fully controllable 3-oscillator
// polysynth voice built on the AMY engine.
//
// Voice architecture (5 AMY oscillators per voice, 8 voices):
//   rel osc 0  SILENT head — voice filter (LP/BP/HP/LP24), amp ADSR (EG0),
//              filter ADSR (EG1), LFO amp mod (tremolo)
//   rel osc 1  LFO (sine/tri/saw/square/S&H-noise) — mod source
//   rel osc 2  OSC 1 ─┐
//   rel osc 3  OSC 2 ─┼── chained into the head osc (share its filter + VCA)
//   rel osc 4  OSC 3 ─┘
//
// The whole voice is stored as an AMY memory patch (slot 1024) and
// instantiated as AMY synth #1 with 8 voices. Every parameter is also live-
// editable: edits are sent as synth-scoped events with a relative osc number,
// so they hit all voices at once without re-instantiating.
#pragma once
#include <Arduino.h>
#include "config.h"

extern "C" {
#include "amy.h"
}

// ── Parameter model ──────────────────────────────────────────────────────────
struct WolfOsc {
  uint8_t wave    = SAW_DOWN;  // SINE/PULSE/SAW_DOWN/SAW_UP/TRIANGLE/NOISE
  float   level   = 0.8f;      // 0..1
  int8_t  octave  = 0;         // -2..+2
  int8_t  semis   = 0;         // -12..+12
  float   fine    = 0.0f;      // -50..+50 cents
  float   duty    = 0.5f;      // pulse width (PULSE only)
};

struct WolfEnv {   // ms / ms / level / ms
  float a, d, s, r;
  WolfEnv() : a(5), d(200), s(0.7f), r(250) {}
  WolfEnv(float a_, float d_, float s_, float r_) : a(a_), d(d_), s(s_), r(r_) {}
};

struct WolfParams {
  WolfOsc osc[3];
  uint8_t filterType = FILTER_LPF24;   // FILTER_NONE/LPF/BPF/HPF/LPF24
  float   cutoff     = 4000.0f;        // Hz, 40..16000 (log UI)
  float   resonance  = 1.0f;           // 0.1..8
  float   keyTrack   = 0.5f;           // 0..1 filter key tracking
  float   envAmount  = 2.0f;           // filter EG depth, -6..+6 octaves
  WolfEnv ampEnv;
  WolfEnv filtEnv;
  uint8_t lfoWave    = TRIANGLE;
  float   lfoRate    = 4.0f;           // 0.05..25 Hz
  float   lfoToFilter= 0.0f;           // 0..1 → up to ±2 octaves
  float   lfoToPitch = 0.0f;           // 0..1 → up to ±2 semitones
  float   lfoToAmp   = 0.0f;           // 0..1 tremolo
  float   lfoToPwm   = 0.0f;           // 0..1 PWM depth
  uint16_t glideMs   = 0;              // portamento
  float   volume     = 0.8f;           // synth level (head osc amp const)
  // Global FX sends (AMY bus effects)
  float   reverb = 0.15f, chorus = 0.0f, echo = 0.0f;
};

class WolfSynth {
public:
  WolfParams p;
  volatile bool needsRedraw = false;   // UI hint after MIDI CC edits
  int currentPreset = 0;               // index into wolf preset table (UI)

  void begin() {
    defineSynth(true);
    sendEffects();
  }

  // ── Notes ─────────────────────────────────────────────────────────────────
  void noteOn(uint8_t note, uint8_t vel) {
    amy_event e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.midi_note = note;
    e.velocity = vel / 127.0f;
    amy_add_event(&e);
  }
  void noteOff(uint8_t note) {
    amy_event e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.midi_note = note;
    e.velocity = 0;
    amy_add_event(&e);
  }
  void allNotesOff() {
    amy_event e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.velocity = 0;
    amy_add_event(&e);
  }
  void pitchBend(int16_t bend14) {  // -8192..8191 → ±2 semitones
    amy_event e = amy_default_event();
    e.pitch_bend = (float)bend14 / (6.0f * 8192.0f);
    amy_add_event(&e);
  }

  // ── Whole-patch application ───────────────────────────────────────────────
  // Rebuild the memory patch from p and (re)instantiate synth #1.
  // `fullReset=false` keeps currently sounding notes from being cut only if
  // nothing structural changed; in practice we always re-instantiate.
  void defineSynth(bool startup = false) {
    if (!startup) allNotesOff();
    char patch[640];
    buildPatchString(patch, sizeof(patch));
    amy_event e = amy_default_event();
    e.patch_number = WOLF_PATCH_NUMBER;
    patches_store_patch(&e, patch);

    e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.patch_number = WOLF_PATCH_NUMBER;
    e.num_voices = WOLF_VOICES;
    e.grab_midi_notes = 0;      // the router decides what this synth hears
    amy_add_event(&e);
    _factoryPatch = -1;
  }

  // Load an AMY factory patch (0-127 Juno-106, 128-255 DX7, 256+ built-ins)
  // into synth #1 instead of the werewolf voice.
  void loadFactoryPatch(uint16_t patchNumber, uint8_t numVoices = 6) {
    allNotesOff();
    amy_event e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.patch_number = patchNumber;
    e.num_voices = numVoices;
    e.grab_midi_notes = 0;
    amy_add_event(&e);
    _factoryPatch = patchNumber;
  }
  bool isFactoryPatch() const { return _factoryPatch >= 0; }
  int  factoryPatch()  const { return _factoryPatch; }

  // ── Live parameter edits (target all voices of synth #1) ─────────────────
  void setOscWave(int i, uint8_t wave) {
    p.osc[i].wave = wave;
    amy_event e = synthEvent(2 + i);
    e.wave = wave;
    amy_add_event(&e);
  }
  void setOscLevel(int i, float lvl) {
    p.osc[i].level = lvl;
    amy_event e = synthEvent(2 + i);
    e.amp_coefs[COEF_CONST] = lvl;
    amy_add_event(&e);
  }
  void setOscTune(int i) {  // octave/semis/fine combined
    amy_event e = synthEvent(2 + i);
    e.freq_coefs[COEF_CONST] = oscBaseHz(i);
    amy_add_event(&e);
  }
  void setOscDuty(int i, float duty) {
    p.osc[i].duty = duty;
    amy_event e = synthEvent(2 + i);
    e.duty_coefs[COEF_CONST] = duty;
    amy_add_event(&e);
  }
  void setFilter() {
    amy_event e = synthEvent(0);
    e.filter_type = p.filterType;
    e.filter_freq_coefs[COEF_CONST] = p.cutoff;
    e.filter_freq_coefs[COEF_NOTE]  = p.keyTrack;
    e.filter_freq_coefs[COEF_EG1]   = p.envAmount;
    e.filter_freq_coefs[COEF_MOD]   = p.lfoToFilter * 2.0f;
    e.resonance = p.resonance;
    amy_add_event(&e);
  }
  void setAmpEnv() {
    amy_event e = synthEvent(0);
    fillEnv(e.eg0_times, e.eg0_values, p.ampEnv, 0.0f);
    amy_add_event(&e);
  }
  void setFiltEnv() {
    amy_event e = synthEvent(0);
    fillEnv(e.eg1_times, e.eg1_values, p.filtEnv, 0.0f);
    amy_add_event(&e);
  }
  void setLFO() {
    amy_event e = synthEvent(1);
    e.wave = p.lfoWave == NOISE ? NOISE : p.lfoWave;  // NOISE acts as S&H
    e.freq_coefs[COEF_CONST] = p.lfoRate;
    amy_add_event(&e);
    // depth routings live on the target oscs:
    setFilter();
    for (int i = 0; i < 3; i++) {
      amy_event t = synthEvent(2 + i);
      t.freq_coefs[COEF_MOD] = p.lfoToPitch / 6.0f;  // ±2 semitones max
      t.duty_coefs[COEF_MOD] = p.lfoToPwm * 0.45f;
      amy_add_event(&t);
    }
    amy_event h = synthEvent(0);
    h.amp_coefs[COEF_MOD] = p.lfoToAmp * 0.5f;
    amy_add_event(&h);
  }
  void setGlide(uint16_t ms) {
    p.glideMs = ms;
    for (int i = 0; i < 3; i++) {
      amy_event e = synthEvent(2 + i);
      e.portamento_ms = ms;
      amy_add_event(&e);
    }
  }
  void setVolume(float v) {
    p.volume = v;
    amy_event e = synthEvent(0);
    e.amp_coefs[COEF_CONST] = v;
    amy_add_event(&e);
  }
  void sendEffects() {
    if (AMY_HAS_REVERB) config_reverb(0, p.reverb * 1.5f, 0.85f, 0.5f, 3000.0f);
    if (AMY_HAS_CHORUS) config_chorus(0, p.chorus, 320, 0.5f, 0.5f);
    if (AMY_HAS_ECHO)   config_echo(0, p.echo * 0.8f, 375.0f, 1500.0f, p.echo * 0.6f, 0.0f);
  }

  // Apply everything from p without re-instantiating (no voice glitch)
  void refreshAll() {
    for (int i = 0; i < 3; i++) {
      setOscWave(i, p.osc[i].wave);
      setOscLevel(i, p.osc[i].level);
      setOscTune(i);
      setOscDuty(i, p.osc[i].duty);
    }
    setFilter(); setAmpEnv(); setFiltEnv(); setLFO();
    setGlide(p.glideMs); setVolume(p.volume); sendEffects();
  }

  // ── MIDI CC map (also reachable from the pots) ────────────────────────────
  void handleCC(uint8_t cc, uint8_t value) {
    float v = value / 127.0f;
    switch (cc) {
      case  7: setVolume(v); break;
      case 74: p.cutoff = 40.0f * powf(400.0f, v); setFilter(); break;  // 40..16k log
      case 71: p.resonance = 0.1f + v * 7.9f; setFilter(); break;
      case 70: p.envAmount = (v - 0.5f) * 12.0f; setFilter(); break;
      case 73: p.ampEnv.a = v * v * 4000; setAmpEnv(); break;
      case 75: p.ampEnv.d = v * v * 4000; setAmpEnv(); break;
      case 79: p.ampEnv.s = v; setAmpEnv(); break;
      case 72: p.ampEnv.r = v * v * 6000; setAmpEnv(); break;
      case 76: p.lfoRate = 0.05f + v * v * 25.0f; setLFO(); break;
      case 77: p.lfoToFilter = v; setLFO(); break;
      case 78: p.lfoToPitch = v; setLFO(); break;
      case 12: setOscLevel(0, v); break;
      case 13: setOscLevel(1, v); break;
      case 14: setOscLevel(2, v); break;
      case 15: p.osc[0].duty = 0.02f + v * 0.96f; setOscDuty(0, p.osc[0].duty); break;
      case  5: setGlide((uint16_t)(v * 500)); break;
      case 91: p.reverb = v; sendEffects(); break;
      case 93: p.chorus = v; sendEffects(); break;
      default: return;
    }
    needsRedraw = true;
  }

private:
  int _factoryPatch = -1;

  static amy_event synthEvent(uint16_t relOsc) {
    amy_event e = amy_default_event();
    e.synth = WOLF_SYNTH_NUM;
    e.osc = relOsc;
    return e;
  }

  float oscBaseHz(int i) const {
    const WolfOsc& o = p.osc[i];
    float semis = o.octave * 12.0f + o.semis + o.fine / 100.0f;
    return 440.0f * exp2f(semis / 12.0f);
  }

  static void fillEnv(uint32_t* times, float* vals, const WolfEnv& env, float endLevel) {
    times[0] = (uint32_t)fmaxf(env.a, 1);           vals[0] = 1.0f;
    times[1] = times[0] + (uint32_t)fmaxf(env.d, 1); vals[1] = env.s;
    times[2] = (uint32_t)fmaxf(env.r, 1);           vals[2] = endLevel;
  }

  // Serialize p as an AMY wire patch string (relative osc numbers).
  void buildPatchString(char* out, size_t cap) const {
    char egA[64], egB[64];
    snprintf(egA, sizeof(egA), "%u,1,%u,%.3f,%u,0",
             (unsigned)fmaxf(p.ampEnv.a, 1),
             (unsigned)(fmaxf(p.ampEnv.a, 1) + fmaxf(p.ampEnv.d, 1)),
             p.ampEnv.s, (unsigned)fmaxf(p.ampEnv.r, 1));
    snprintf(egB, sizeof(egB), "%u,1,%u,%.3f,%u,0",
             (unsigned)fmaxf(p.filtEnv.a, 1),
             (unsigned)(fmaxf(p.filtEnv.a, 1) + fmaxf(p.filtEnv.d, 1)),
             p.filtEnv.s, (unsigned)fmaxf(p.filtEnv.r, 1));

    int n = snprintf(out, cap,
      // LFO osc
      "v1w%df%.3fa1,,0,1Z"
      // SILENT head: chain->osc2, LFO is mod source, filter + both EGs
      "v0w%dc2L1G%d"
      "F%.1f,%.3f,,,%.3f,%.3fR%.3f"
      "a%.3f,,1,1,0,%.3f"
      "A%sB%sZ",
      p.lfoWave, p.lfoRate,
      SILENT, p.filterType,
      p.cutoff, p.keyTrack, p.envAmount, p.lfoToFilter * 2.0f, p.resonance,
      p.volume, p.lfoToAmp * 0.5f,
      egA, egB);

    // Three source oscillators; osc2 chains to 3, 3 chains to 4.
    for (int i = 0; i < 3 && n < (int)cap; i++) {
      const WolfOsc& o = p.osc[i];
      char chain[8] = "";
      if (i < 2) snprintf(chain, sizeof(chain), "c%d", 3 + i);
      n += snprintf(out + n, cap - n,
        "v%dw%d%s"
        "a%.3f,,0,0,,%.3f"
        "f%.3f,1,,,,%.3f,1"
        "d%.3f,,,,,%.3f"
        "m%uZ",
        2 + i, o.wave, chain,
        o.level, 0.0f,
        oscBaseHz(i), p.lfoToPitch / 6.0f,
        o.duty, p.lfoToPwm * 0.45f,
        (unsigned)p.glideMs);
    }
  }
};

extern WolfSynth wolf;

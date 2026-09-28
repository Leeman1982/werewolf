#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"
#include "ladder_filter.h"

// ============================================================================
//  moogmodular.h — Moog Modular System 55/III signal path, one voice.
//
//  The wildcard engine: not one of the three "never modelled upstream"
//  stubs (oberheim.h/jupiter8.h/system100.h alongside this one round out
//  those), an entirely new eleventh engine. Chosen because it's the actual
//  instrument behind BOTH of this build's two touchstones — John Carpenter's
//  Halloween (1978, programmed by Dan Wyman on a Moog Series III/System 55)
//  and Goblin's Suspiria (1977, programmed by Felice Fugazza on a Moog
//  System 55) — while the Minimoog already in this codebase is that same
//  filter/oscillator lineage's compact, pre-patched little sibling, never
//  itself the true modular rig those two scores were built on.
//
//  What makes this a modular system rather than a bigger Minimoog:
//
//  1. Three VCOs with real inter-modulation a compact synth's fixed panel
//     doesn't offer: OSC2 can hard-sync onto OSC1 (a real patch-cord trick,
//     not present on the Minimoog, which has none), and OSC3 can audio-rate
//     FM OSC1's frequency directly (genuine cross-modulation, not the
//     Minimoog's own OSC3-as-slow-modulator-only design).
//  2. A real ring modulator (OSC1 x OSC2) and a sample & hold fed from
//     noise — both genuine modular utility MODULES a compact synth's fixed
//     signal path has no room for, patched in here as permanent connections
//     rather than a literal cord matrix.
//  3. The filter envelope has its own dedicated amount knob into OSC PITCH
//     as well as the filter — "patch one envelope to two destinations at
//     once" is a basic modular technique a fixed-architecture synth's panel
//     can't express (every other engine's filter envelope here stays
//     dedicated to the filter alone).
//  4. STOCK filter is the shared LadderFilter (ladder_filter.h) — the SAME
//     struct the Minimoog engine uses, not a new one. That's deliberate,
//     not laziness: the real Moog Modular's own 904A filter module IS the
//     circuit that became the Minimoog's ladder, so sharing the model here
//     is the historically accurate choice, in contrast to every other
//     engine in this codebase, which gets its own filter precisely because
//     its real hardware filter actually differs.
// ============================================================================

#define MM2_VOICES 1

enum Mm2Wave : uint8_t { MM2W_TRI = 0, MM2W_SAW, MM2W_PULSE, MM2W_SINE };

struct Mm2Osc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
    float    baseHz = 220.0f;   // last commanded frequency, before audio-rate FM

    inline void setFreq(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        baseHz = hz;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    // Audio-rate FM retunes the phase increment every sample without
    // touching dt/invDt (those stay sized to the carrier's own base pitch,
    // which is what keeps PolyBLEP correction well-behaved under FM) --
    // same trick as jupiter8.h's Jp8Osc::setFmHz().
    inline void setFmHz(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        inc = freqToPhaseInc(hz);
    }
    inline void resetPhase() { phase = 0; }
    // Advances the phase AND returns this sample's waveform value in one
    // call (like cs80_voice.h's cs80OscNext) -- `wrapped`, if given, reports
    // whether this step crossed 0 (a full cycle), for hard sync.
    inline float next(uint8_t wave, bool* wrapped = nullptr) {
        uint32_t prev = phase;
        phase += inc;
        if (wrapped) *wrapped = (phase < prev);
        float t = phaseToFloat(phase);
        switch (wave) {
        case MM2W_TRI:   return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        case MM2W_PULSE: {
            float out = (t < 0.5f) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out;
        }
        case MM2W_SINE:  return sineFromPhase(phase);
        default:         return 2.0f * t - 1.0f - polyBlep(t, dt, invDt);
        }
    }
};

struct Mm2Noise { uint32_t seed = 0x5A11u;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

enum Mm2EnvStage : uint8_t { MM2_E_IDLE = 0, MM2_E_ATTACK, MM2_E_DECAY, MM2_E_SUSTAIN, MM2_E_RELEASE };
struct Mm2Env {
    uint8_t stage = MM2_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = MM2_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != MM2_E_IDLE) stage = MM2_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case MM2_E_IDLE: return 0.0f;
        case MM2_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = MM2_E_DECAY; }
            break;
        case MM2_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = MM2_E_SUSTAIN; }
            break;
        case MM2_E_SUSTAIN: level = sus; break;
        case MM2_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = MM2_E_IDLE; }
            break;
        }
        return level;
    }
};

struct Mm2Voice {
    Mm2Osc   osc1, osc2, osc3;
    Mm2Noise noise;
    LadderFilter filter;   // shared with the Minimoog engine -- see file banner point 4
    VarFilter    varFilt;  // alternate flavour -- see variable_filter.h
    Mm2Env   filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;

    // Free-running S&H clock, same pattern as this codebase's ARP 2600.
    uint32_t shPhase = 0;
    float    shValue = 0.0f;
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        mm2EngineInit();
void        mm2EngineActivate();
void        mm2ParamsInitE();
void        mm2SetParameterE(uint8_t id, float v);
float       mm2GetParameterE(uint8_t id);
void        mm2FormatParameterE(uint8_t id, char* buf, size_t n);
bool        mm2ParamIsLockableE(uint8_t id);
uint8_t     mm2NumParamsE();
const ParamInfo* mm2ParamInfoE(uint8_t id);
uint8_t     mm2NumPresetsE();
const char* mm2PresetNameE(uint8_t idx);
void        mm2LoadPresetE(uint8_t idx);
void        mm2LoadInitPatchE();
void        mm2SnapshotParamsE(uint8_t* out);
void        mm2RestoreParamsE(const uint8_t* in);
uint8_t     mm2NumVoicesE();
void        mm2VoiceInitE();
void        mm2NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        mm2NoteOffE(uint8_t voiceIdx);
void        mm2AllOffE();
void        mm2SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     mm2MasterParamIdE();
uint8_t     mm2FilterCutoffParamIdE();
uint8_t     mm2FilterResonanceParamIdE();
int         mm2RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     mm2NumPagesE();
const struct EnginePageDef* mm2PageE(uint8_t idx);

#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  system100.h — Roland System 100 (Model 101/102) signal path, one voice.
//
//  No ESP32-S3 source to port (see oberheim.h's file banner for why) — built
//  from scratch from the real 1975 semi-modular system's own published
//  architecture. The 101 was a standalone monophonic keyboard synth: one
//  VCO (with PWM), noise, a voltage-controlled lowpass filter PLUS a simple
//  non-voltage-controlled highpass tone control, a VCA, and one envelope
//  generator. The 102 expander duplicated most of that circuit sans keyboard
//  and swapped the 101's noise generator slot for a sample & hold and a ring
//  modulator. This engine models the combined 101+102 signal path a
//  performer actually patched together, on one voice — the real System 100
//  keyboard rig was strictly monophonic.
//
//  What makes this a System 100 rather than a generic 1-VCO mono synth:
//
//  1. Exactly ONE melodic oscillator — no second VCO to detune or sync
//     against. The "second voice" in the signal path is a dedicated small
//     ring-modulator oscillator (Sy1RingOsc below) whose own frequency
//     RATIO to the main VCO is a panel control, not a melodic voice in its
//     own right — matching how the 102's ring mod was actually used
//     (against the VCO, an LFO, or an external signal, never as a second
//     tunable "lead" voice the way this codebase's other 2-oscillator
//     engines use their second VCO).
//  2. The HPF is NOT voltage-controlled — a fixed one-pole tone knob with
//     no envelope or keyboard-tracking modulation reaching it at all, in
//     deliberate contrast to every other filter parameter in this codebase
//     (including this same engine's own resonant lowpass), which is exactly
//     how the real 101/102 wired it.
//  3. Sample & hold fed from noise, patchable to pitch and/or filter — the
//     102 expander's other signature addition, alongside the ring mod.
// ============================================================================

#define SY1_VOICES 1

struct Sy1Osc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;

    inline void setFreq(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    inline float advance() { phase += inc; return phaseToFloat(phase); }
    inline float tri(float t) const { return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t); }
    inline float saw(float t) const { return 2.0f * t - 1.0f - polyBlep(t, dt, invDt); }
    inline float pulse(float t, float pw) const {
        float out = (t < pw) ? 1.0f : -1.0f;
        out += polyBlep(t, dt, invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= polyBlep(t2, dt, invDt);
        return out;
    }
};

// The 102 expander's ring-mod operand -- not a melodic voice (see file
// banner). Free-running sine at a ratio of the main VCO's own frequency.
struct Sy1RingOsc {
    uint32_t phase = 0, inc = 0;
    inline void setFreq(float hz) { if (hz < 0.05f) hz = 0.05f; inc = freqToPhaseInc(hz); }
    inline float advance() { phase += inc; return sineFromPhase(phase); }
};

struct Sy1Noise { uint32_t seed = 0xC0FFEEu;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── The System 100's resonant lowpass ──────────────────────────────────────
// TPT state-variable, same proven topology as this codebase's other "real
// hardware" filters. Roland's analogue lowpasses of this vintage do reach
// self-oscillation at maximum resonance (the SH-101/System 100 "squelch"),
// so the damping floor here goes all the way to zero, same family of
// behaviour as the ARP 2600's filter elsewhere in this codebase.
struct Sy1Svf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        k = 2.0f * (1.0f - resonance01);
        if (k < 0.02f) k = 0.02f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1; a3 = g * a2;
    }
    inline float process(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};

// The 101's OTHER filter: a fixed, one-pole, non-voltage-controlled
// highpass -- a plain tone-shaping knob with nothing (no envelope, no
// keyboard tracking) modulating it. See file banner point 2.
struct Sy1FixedHpf {
    float z = 0.0f;
    inline float process(float x, float g) {
        z += g * (x - z);
        return x - z;   // input minus its own lowpass = highpass
    }
};

enum Sy1EnvStage : uint8_t { SY1_E_IDLE = 0, SY1_E_ATTACK, SY1_E_DECAY, SY1_E_SUSTAIN, SY1_E_RELEASE };
struct Sy1Env {
    uint8_t stage = SY1_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = SY1_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != SY1_E_IDLE) stage = SY1_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case SY1_E_IDLE: return 0.0f;
        case SY1_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = SY1_E_DECAY; }
            break;
        case SY1_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = SY1_E_SUSTAIN; }
            break;
        case SY1_E_SUSTAIN: level = sus; break;
        case SY1_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = SY1_E_IDLE; }
            break;
        }
        return level;
    }
};

struct Sy1Voice {
    Sy1Osc      osc;
    Sy1RingOsc  ringOsc;
    Sy1Noise    noise;
    Sy1Svf      filter;
    VarFilter   varFilt;   // alternate flavour -- see variable_filter.h
    Sy1FixedHpf hpf;
    Sy1Env      env;       // one envelope, shared by VCF (amount-scaled) and VCA

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
void        sy1EngineInit();
void        sy1EngineActivate();
void        sy1ParamsInitE();
void        sy1SetParameterE(uint8_t id, float v);
float       sy1GetParameterE(uint8_t id);
void        sy1FormatParameterE(uint8_t id, char* buf, size_t n);
bool        sy1ParamIsLockableE(uint8_t id);
uint8_t     sy1NumParamsE();
const ParamInfo* sy1ParamInfoE(uint8_t id);
uint8_t     sy1NumPresetsE();
const char* sy1PresetNameE(uint8_t idx);
void        sy1LoadPresetE(uint8_t idx);
void        sy1LoadInitPatchE();
void        sy1SnapshotParamsE(uint8_t* out);
void        sy1RestoreParamsE(const uint8_t* in);
uint8_t     sy1NumVoicesE();
void        sy1VoiceInitE();
void        sy1NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        sy1NoteOffE(uint8_t voiceIdx);
void        sy1AllOffE();
void        sy1SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     sy1MasterParamIdE();
uint8_t     sy1FilterCutoffParamIdE();
uint8_t     sy1FilterResonanceParamIdE();
int         sy1RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     sy1NumPagesE();
const struct EnginePageDef* sy1PageE(uint8_t idx);

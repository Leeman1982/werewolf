#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  arp2600.h — ARP 2600 signal path, one voice (the stock instrument is
//  monophonic; duophonic keyboard behaviour is offered as a per-track
//  KEYBOARD STYLE elsewhere in this codebase, not hardwired into the engine —
//  see keystyle.h — which is also historically the more accurate split: the
//  2600 itself never had a duo switch; the Odyssey engine derived from this
//  one does, and that switch is exactly the per-track style option).
//
//  Three VCOs into a mixer, a self-oscillating multimode state-variable
//  filter, a genuine 4-stage ADSR on both filter and amp, a sample & hold fed
//  from noise, and a ring modulator that multiplies two of the oscillators
//  directly. Two things make this an ARP rather than a generic 3-VCO synth:
//
//  1. The filter's damping floor reaches ZERO — in deliberate contrast to the
//     CS-80 elsewhere in this codebase, whose floor by design never does.
//     Same TPT state-variable topology (stable at any cutoff, no coefficient
//     blow-up), but here the resonance control is allowed to remove ALL the
//     damping, which is exactly what lets it self-oscillate and scream —
//     the 2600's filter is famous for exactly that.
//  2. LP / BP / HP come out of ONE pass through that same state-variable
//     core, selected by a mode switch, matching the real 4072 filter's
//     3-way switch — not three separate filter implementations.
//  3. Ring modulation multiplies OSC1 and OSC2 directly, no swept-sine
//     carrier — a plain, patchable ring-mod module, unlike the CS-80's own
//     ring modulator (also in this codebase), whose carrier is always a pure
//     sine swept by its own envelope. Same idea, authentically different
//     circuit.
// ============================================================================

// Array capacity — the stock 2600 is strictly monophonic (runtime count 1),
// but the derived Odyssey engine (odyssey.cpp) runs the same voice code with
// count 2: the Odyssey's panel really did have a switchable duophonic mode,
// which the 2600 never had.
#define A26_VOICES 2

enum A26Wave : uint8_t { A26W_SAW = 0, A26W_TRI, A26W_PULSE, A26W_SINE };

struct A26Osc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;

    inline void setFreq(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    inline float next(uint8_t wave) {
        phase += inc;
        float t = phaseToFloat(phase);
        switch (wave) {
        case A26W_TRI:  return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        case A26W_PULSE: {
            float out = (t < 0.5f) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out;
        }
        case A26W_SINE: return sineFromPhase(phase);
        default:        return 2.0f * t - 1.0f - polyBlep(t, dt, invDt);
        }
    }
};

struct A26Noise { uint32_t seed = 0xACE1u;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── Self-oscillating multimode state-variable filter ─────────────────────
// TPT (topology-preserving transform) integrator pair, exactly the CS-80's
// own math elsewhere in this codebase (see cs80_voice.h's Cs80Svf) — but
// here the damping `k` is allowed down to a true zero floor, which is what
// lets it self-oscillate. LP/BP/HP all come out of the same pass.
enum A26FiltMode : uint8_t { A26F_LP = 0, A26F_BP, A26F_HP };
struct A26Svf {
    float ic1 = 0, ic2 = 0;
    float k = 2.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        // No damping floor: at resonance01 == 1 this reaches ~0, and the
        // filter genuinely self-oscillates. That absence of a floor is the
        // entire point of building this filter separately from the CS-80's.
        k = 2.0f * (1.0f - resonance01);
        if (k < 0.02f) k = 0.02f;     // avoid an exact-zero divide, not a resonance ceiling
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void process(float x, float* lp, float* bp, float* hp) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        *lp = v2; *bp = v1; *hp = x - k * v1 - v2;
    }
};

enum A26EnvStage : uint8_t { A26E_IDLE = 0, A26E_ATTACK, A26E_DECAY, A26E_SUSTAIN, A26E_RELEASE };
struct A26Env {
    uint8_t stage = A26E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = A26E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != A26E_IDLE) stage = A26E_RELEASE; }
    inline float process() {
        switch (stage) {
        case A26E_IDLE: return 0.0f;
        case A26E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = A26E_DECAY; }
            break;
        case A26E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = A26E_SUSTAIN; }
            break;
        case A26E_SUSTAIN: level = sus; break;
        case A26E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = A26E_IDLE; }
            break;
        }
        return level;
    }
};

struct A26Voice {
    A26Osc  osc1, osc2, osc3;
    A26Noise noise;
    A26Svf  filter;
    VarFilter varFilt;   // alternate flavour — see variable_filter.h
    A26Env  filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;

    // Sample & hold: a free-running clock samples noise; the held value
    // rides on pitch and/or filter until the next clock edge.
    uint32_t shPhase = 0;
    float    shValue = 0.0f;
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
// a26SetPolyphonyE(1) / (2) is what turns this same implementation into the
// ARP 2600 (strictly mono) or the ARP Odyssey (duophonic) — see odyssey.cpp.
void        a26SetPolyphonyE(uint8_t n);
void        a26EngineInit();
void        a26EngineActivate();
void        a26ParamsInitE();
void        a26SetParameterE(uint8_t id, float v);
float       a26GetParameterE(uint8_t id);
void        a26FormatParameterE(uint8_t id, char* buf, size_t n);
bool        a26ParamIsLockableE(uint8_t id);
uint8_t     a26NumParamsE();
const ParamInfo* a26ParamInfoE(uint8_t id);
uint8_t     a26NumPresetsE();
const char* a26PresetNameE(uint8_t idx);
void        a26LoadPresetE(uint8_t idx);
void        a26LoadInitPatchE();
void        a26SnapshotParamsE(uint8_t* out);
void        a26RestoreParamsE(const uint8_t* in);
uint8_t     a26NumVoicesE();
void        a26VoiceInitE();
void        a26NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        a26NoteOffE(uint8_t voiceIdx);
void        a26AllOffE();
void        a26SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     a26MasterParamIdE();
uint8_t     a26FilterCutoffParamIdE();
uint8_t     a26FilterResonanceParamIdE();
int         a26RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     a26NumPagesE();
const struct EnginePageDef* a26PageE(uint8_t idx);

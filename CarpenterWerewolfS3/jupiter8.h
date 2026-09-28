#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  jupiter8.h — Roland Jupiter-8 signal path, one voice.
//
//  No ESP32-S3 source to port (see oberheim.h's file banner for why) — built
//  from scratch from the real 1981 flagship's own published architecture:
//  8 voices, 2 true analogue VCOs per voice (this predates Roland's own DCO
//  designs — Jupiter-6/Juno came later), a switchable 12dB/24dB resonant
//  lowpass filter built around the same IR3109 chip family as the Juno, full
//  ADSR on both filter and amp, and a CROSS MOD slider that runs VCO2 into
//  VCO1's frequency for the metallic, FM-adjacent tones the Jupiter-8 is
//  loved for beyond its lush pads.
//
//  What makes this a Jupiter-8 rather than a generic 2-VCO poly:
//
//  1. VCO2 can replace its square wave with NOISE and drop into a LOW FREQ
//     sub-audio range — real hardware's own switches, modelled here as
//     JP8_OSC2_LOWFREQ rather than a discrete "5th octave" the way this
//     codebase's Minimoog models its own LFO-speed oscillator option,
//     because the Jupiter's low-freq mode is a genuine range SWITCH, not an
//     octave step.
//  2. The filter's SLOPE switch (12dB/24dB) is modelled as literally
//     cascading a second identical lowpass stage for 24dB rather than a
//     different filter shape — that is exactly how a real switchable-slope
//     filter of this kind is built. Its damping floor never quite reaches
//     zero even at max RESONANCE, the "polite" IR3109 character shared with
//     this codebase's Juno filter FLAVOUR (variable_filter.h), in contrast
//     to the ARP 2600's filter elsewhere in this codebase, which can.
//  3. CROSS MOD (VCO2 -> VCO1 frequency) is genuine audio-rate FM, not a
//     slow LFO-style modulation — the source of the Jupiter-8's more
//     aggressive, bell-like/metallic textures.
// ============================================================================

#define JP8_VOICES 8

struct Jp8Osc {
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
    // Audio-rate cross-mod retunes the phase increment every sample without
    // touching dt/invDt (those stay sized to the carrier's own base pitch,
    // which is what keeps PolyBLEP correction well-behaved under FM).
    inline void setFmHz(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        inc = freqToPhaseInc(hz);
    }
    inline float advance(bool* wrapped = nullptr) {
        uint32_t prev = phase;
        phase += inc;
        if (wrapped) *wrapped = (phase < prev);
        return phaseToFloat(phase);
    }
    inline void resetPhase() { phase = 0; }
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

struct Jp8Noise { uint32_t seed = 0xBEEF1u;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── The Jupiter's own filter: switchable 12dB/24dB resonant lowpass ───────
// TPT integrator, same proven topology as this codebase's other "real
// hardware" filters. 24dB mode cascades a second identical stage in series,
// exactly how a real switchable-slope design gets its second pole pair.
struct Jp8Svf {
    float s1 = 0.0f, s2 = 0.0f, s1b = 0.0f, s2b = 0.0f;
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        // IR3109 "polite" damping law: never quite reaches self-oscillation,
        // matching this codebase's own Juno filter FLAVOUR (variable_filter.h)
        // rather than the ARP 2600's filter elsewhere, whose floor reaches
        // exactly zero.
        k = 0.16f + (1.0f - resonance01) * 1.9f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    static inline float lpStage(float x, float f1, float f2, float f3, float& s1, float& s2) {
        const float v3 = x - s2;
        const float v1 = f1 * s1 + f2 * v3;
        const float v2 = s2 + f2 * s1 + f3 * v3;
        s1 = 2.0f * v1 - s1; s2 = 2.0f * v2 - s2;
        return v2;
    }
    inline float process(float x, bool slope24) {
        float y = lpStage(x, a1, a2, a3, s1, s2);
        if (slope24) y = lpStage(y, a1, a2, a3, s1b, s2b);
        return y;
    }
};

enum Jp8EnvStage : uint8_t { JP8_E_IDLE = 0, JP8_E_ATTACK, JP8_E_DECAY, JP8_E_SUSTAIN, JP8_E_RELEASE };
struct Jp8Env {
    uint8_t stage = JP8_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = JP8_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != JP8_E_IDLE) stage = JP8_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case JP8_E_IDLE: return 0.0f;
        case JP8_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = JP8_E_DECAY; }
            break;
        case JP8_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = JP8_E_SUSTAIN; }
            break;
        case JP8_E_SUSTAIN: level = sus; break;
        case JP8_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = JP8_E_IDLE; }
            break;
        }
        return level;
    }
};

struct Jp8Voice {
    Jp8Osc   osc1, osc2;
    Jp8Noise noise;
    Jp8Svf   filter;
    VarFilter varFilt;   // alternate flavour -- see variable_filter.h
    Jp8Env   filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        jp8EngineInit();
void        jp8EngineActivate();
void        jp8ParamsInitE();
void        jp8SetParameterE(uint8_t id, float v);
float       jp8GetParameterE(uint8_t id);
void        jp8FormatParameterE(uint8_t id, char* buf, size_t n);
bool        jp8ParamIsLockableE(uint8_t id);
uint8_t     jp8NumParamsE();
const ParamInfo* jp8ParamInfoE(uint8_t id);
uint8_t     jp8NumPresetsE();
const char* jp8PresetNameE(uint8_t idx);
void        jp8LoadPresetE(uint8_t idx);
void        jp8LoadInitPatchE();
void        jp8SnapshotParamsE(uint8_t* out);
void        jp8RestoreParamsE(const uint8_t* in);
uint8_t     jp8NumVoicesE();
void        jp8VoiceInitE();
void        jp8NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        jp8NoteOffE(uint8_t voiceIdx);
void        jp8AllOffE();
void        jp8SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     jp8MasterParamIdE();
uint8_t     jp8FilterCutoffParamIdE();
uint8_t     jp8FilterResonanceParamIdE();
int         jp8RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     jp8NumPagesE();
const struct EnginePageDef* jp8PageE(uint8_t idx);

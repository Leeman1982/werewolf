#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  oberheim.h — Oberheim Four-Voice / SEM signal path, one voice.
//
//  This engine (and jupiter8.h/system100.h/moogmodular.h alongside it) has no
//  ESP32-S3 source to port: the upstream project stubbed all three "never
//  modelled" engines rather than building them. Built from scratch here,
//  following the same header-documents-the-real-circuit discipline as every
//  ported engine, from the real Four-Voice/SEM's own published architecture
//  (Tom Oberheim's 1974 SEM module, sold standalone and in banks of two/four
//  as the Two-Voice/Four-Voice): 2 VCOs (saw + variable pulse each) into a
//  2-pole multimode state-variable filter with a CONTINUOUSLY VARIABLE mode
//  control sweeping LP -> Notch -> BP -> HP, plus dedicated ADSR envelopes
//  for filter and amp.
//
//  Two things make this a Four-Voice/SEM rather than a generic 2-VCO synth:
//
//  1. The filter's MODE is a knob position, not a switched selection — real
//     hardware had one slider sweeping continuously through all four filter
//     responses, which is why SemSvf below blends LP/Notch/BP/HP by a 0..1
//     knob rather than picking one of a fixed enum the way every other
//     engine's own "native" filter does.
//  2. Four independent SEM cards were never perfectly matched: each voice
//     here carries its own small, FIXED (not random per boot) pitch/filter/
//     level offset, same technique as the CS-80 engine's per-voice drift
//     elsewhere in this codebase but more pronounced — that "four not-quite-
//     identical voices" chorus-like character is most of why a real
//     Four-Voice sounds the way it does, and is a well-known, embraced
//     feature of the real instrument rather than a flaw to correct.
// ============================================================================

#define OB_VOICES 4

struct ObOsc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;

    inline void setFreq(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    inline float advance() {
        phase += inc;
        return phaseToFloat(phase);
    }
    inline float saw(float t) const { return 2.0f * t - 1.0f - polyBlep(t, dt, invDt); }
    inline float pulse(float t, float pw) const {
        float out = (t < pw) ? 1.0f : -1.0f;
        out += polyBlep(t, dt, invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= polyBlep(t2, dt, invDt);
        return out;
    }
};

struct ObNoise { uint32_t seed = 0xF00Du;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── The SEM's own filter: 2-pole state-variable, continuously-variable mode ─
// TPT integrator pair, the same proven topology as this codebase's other
// "real hardware" filters (Cs80Svf/A26Svf) — stable at any cutoff, no
// coefficient blow-up. What's different is that the caller blends this
// filter's three simultaneous LP/BP/HP outputs by a 0..1 MODE knob (see
// semModeMorph below) rather than switching between them, since that
// continuous sweep is the actual panel control on real hardware.
struct SemSvf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        // Damping floor sits just above zero at max resonance: the real SEM
        // filter can be coaxed into a ragged near-self-oscillation at
        // maximum Q, somewhere between the CS-80's filter elsewhere in this
        // codebase (floor never reaches zero, cannot self-oscillate) and the
        // ARP 2600's (floor reaches exactly zero, screams cleanly).
        k = 2.0f * (1.0f - resonance01);
        if (k < 0.06f) k = 0.06f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline void process(float x, float* outLP, float* outBP, float* outHP) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1;
        ic2 = 2.0f * v2 - ic2;
        *outLP = v2; *outBP = v1; *outHP = x - k * v1 - v2;
    }
};

// Continuous LP -> Notch -> BP -> HP sweep across mode01 in [0,1]. Notch
// isn't a separate SVF output -- it's LP+HP summed, computed here, exactly
// how the real filter derives it internally.
static inline float semModeMorph(float lp, float bp, float hp, float mode01) {
    const float notch = lp + hp;
    if (mode01 < 1.0f / 3.0f) { float t = mode01 * 3.0f;               return lp    + (notch - lp) * t; }
    if (mode01 < 2.0f / 3.0f) { float t = (mode01 - 1.0f / 3.0f) * 3.0f; return notch + (bp - notch) * t; }
    float t = (mode01 - 2.0f / 3.0f) * 3.0f;                           return bp    + (hp - bp) * t;
}

enum ObEnvStage : uint8_t { OB_E_IDLE = 0, OB_E_ATTACK, OB_E_DECAY, OB_E_SUSTAIN, OB_E_RELEASE };
struct ObEnv {
    uint8_t stage = OB_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = OB_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != OB_E_IDLE) stage = OB_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case OB_E_IDLE: return 0.0f;
        case OB_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = OB_E_DECAY; }
            break;
        case OB_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = OB_E_SUSTAIN; }
            break;
        case OB_E_SUSTAIN: level = sus; break;
        case OB_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = OB_E_IDLE; }
            break;
        }
        return level;
    }
};

struct ObVoice {
    ObOsc    osc1, osc2;
    ObNoise  noise;
    SemSvf   filter;
    VarFilter varFilt;   // alternate flavour -- see variable_filter.h
    ObEnv    filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;
};

// Per-voice fixed offsets -- deliberately more pronounced than the CS-80's
// own (see file banner): four hand-tuned SEM cards, not four copies of one
// circuit. Spread deterministically so a patch sounds the same every
// power-up rather than randomly detuned each boot.
extern const float OB_VOICE_DETUNE[OB_VOICES];   // cents
extern const float OB_VOICE_FILT[OB_VOICES];     // fraction of an octave on cutoff
extern const float OB_VOICE_LEVEL[OB_VOICES];    // linear gain trim

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        obEngineInit();
void        obEngineActivate();
void        obParamsInitE();
void        obSetParameterE(uint8_t id, float v);
float       obGetParameterE(uint8_t id);
void        obFormatParameterE(uint8_t id, char* buf, size_t n);
bool        obParamIsLockableE(uint8_t id);
uint8_t     obNumParamsE();
const ParamInfo* obParamInfoE(uint8_t id);
uint8_t     obNumPresetsE();
const char* obPresetNameE(uint8_t idx);
void        obLoadPresetE(uint8_t idx);
void        obLoadInitPatchE();
void        obSnapshotParamsE(uint8_t* out);
void        obRestoreParamsE(const uint8_t* in);
uint8_t     obNumVoicesE();
void        obVoiceInitE();
void        obNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        obNoteOffE(uint8_t voiceIdx);
void        obAllOffE();
void        obSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     obMasterParamIdE();
uint8_t     obFilterCutoffParamIdE();
uint8_t     obFilterResonanceParamIdE();
int         obRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     obNumPagesE();
const struct EnginePageDef* obPageE(uint8_t idx);

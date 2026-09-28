#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "ladder_filter.h"
#include "variable_filter.h"

// ============================================================================
//  minimoog.h — Minimoog Model D signal path, one voice (it is monophonic).
//
//  Three oscillators -> mixer -> 4-pole transistor-ladder lowpass -> VCA, with
//  a modulation source (oscillator 3 or noise) that can hit the filter and/or
//  the other oscillators' frequency, exactly as the Model D's own panel wires
//  it. Two things make this a Minimoog rather than a generic 3-VCO synth:
//
//  1. The ladder filter SELF-OSCILLATES at high Emphasis, unlike the CS-80's
//     filter in this same codebase, which by design cannot. That contrast is
//     deliberate and is the point of building both: a Moog ladder gets its
//     resonance from positive feedback around four stages, and above a
//     threshold that feedback loop sustains its own sine tone with no input
//     at all. Modelled here as four one-pole stages, each shaped by tanh, with
//     the fourth stage fed back to the input scaled by Emphasis; the tanh
//     nonlinearity is exactly what keeps the self-oscillation's amplitude
//     bounded instead of blowing up once the loop gain crosses unity.
//  2. No dedicated Release control. The Model D's contour generators are
//     Attack/Decay/Sustain only — on key-up the envelope heads back to zero
//     at the DECAY rate, because the panel simply has no third time control.
//     Modelled as one coefficient reused for both stages, which is the
//     authentic behaviour, not a simplification.
// ============================================================================

#define MM_VOICES 1     // the real instrument has one

// ─── Oscillator: 6 waveforms, octave switch, PolyBLEP-corrected ────────────
enum MMWave : uint8_t { MMW_TRI = 0, MMW_TRISAW, MMW_SAW, MMW_SQNARROW, MMW_PULSE, MMW_PULSEWIDE };

struct MMOsc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
    uint8_t  wave = MMW_SAW;
    // The real Model D's waveform switch position 2 is NOT the same shape on
    // all three oscillators: Osc 1/2 get a Triangle/Sawtooth hybrid (the
    // asymmetric ramp below), but Osc 3's own panel legend names that same
    // position "Reverse Sawtooth" -- a plain downward ramp, mirrored from the
    // normal Sawtooth position rather than related to the triangle at all.
    // Set true only for the voice's osc3 instance (see MMVoice/mmEngineActivate).
    bool     isOsc3 = false;

    inline void setFreq(float hz) {
        if (hz < 0.5f) hz = 0.5f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }

    inline float next() {
        phase += inc;
        float t = phaseToFloat(phase);
        switch (wave) {
        case MMW_TRI: {
            // A triangle has no discontinuity in value, only in slope, so it
            // needs no BLEP correction — the corner is soft by construction.
            return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        }
        case MMW_TRISAW: {
            if (isOsc3) {
                // Osc 3's own "Reverse Sawtooth": a plain downward ramp, the
                // mirror image of MMW_SAW — discontinuous at the wrap (jumps
                // UP from -1 to +1), so it needs the same BLEP correction as
                // a normal saw, just with the opposite sign for the opposite-
                // polarity discontinuity.
                return (1.0f - 2.0f * t) + polyBlep(t, dt, invDt);
            }
            // Osc 1/2's Triangle/Sawtooth — halfway between triangle and saw,
            // an asymmetric ramp (rises over 3/4 of the cycle, falls back
            // over the last 1/4), as on the real waveform selector.
            // Continuous at both the 0.75 corner and the wrap, so — like
            // MMW_TRI — no BLEP is needed.
            return (t < 0.75f) ? ((8.0f / 3.0f) * t - 1.0f) : (7.0f - 8.0f * t);
        }
        case MMW_SAW:
            return 2.0f * t - 1.0f - polyBlep(t, dt, invDt);
        case MMW_SQNARROW: {
            const float pw = 0.15f;
            float out = (t < pw) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out - (2.0f * pw - 1.0f);
        }
        case MMW_PULSEWIDE: {
            const float pw = 0.70f;
            float out = (t < pw) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out - (2.0f * pw - 1.0f);
        }
        default: { // MMW_PULSE, 50%
            float out = (t < 0.5f) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out;
        }
        }
    }
};

struct MMNoise { uint32_t seed = 0x9e3779b9u;
    inline float white() {
        seed = seed * 1664525u + 1013904223u;
        return (float)((int32_t)seed) * 4.6566128752458e-10f;
    }
};

// The self-oscillating ladder itself (LadderFilter) now lives in
// ladder_filter.h, shared with the Prophet-5 engine's own VCF.

// ─── ADS contour: no Release knob, Decay time is reused on key-up ──────────
enum MMEnvStage : uint8_t { MME_IDLE = 0, MME_ATTACK, MME_DECAY, MME_SUSTAIN, MME_RELEASE };
struct MMEnv {
    uint8_t stage = MME_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f;

    inline void trigger(float atk, float dec, float s) {
        stage = MME_ATTACK; atkRate = atk; decCoef = dec; sus = s;
    }
    inline void release() { if (stage != MME_IDLE) stage = MME_RELEASE; }
    inline float process() {
        switch (stage) {
        case MME_IDLE: return 0.0f;
        case MME_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = MME_DECAY; }
            break;
        case MME_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = MME_SUSTAIN; }
            break;
        case MME_SUSTAIN:
            level = sus;
            break;
        case MME_RELEASE:
            // The authentic bit: this uses decCoef, the SAME rate as decay,
            // because the real panel has nothing else to give it.
            level -= level * decCoef;
            if (level < 0.0005f) { level = 0.0f; stage = MME_IDLE; }
            break;
        }
        return level;
    }
};

struct MMVoice {
    MMOsc  osc1, osc2, osc3;
    MMNoise noise;
    LadderFilter filter;
    // Alternate "filter flavour" — see variable_filter.h. Only ever run
    // instead of `filter` above, never alongside it (FILT TYPE selects
    // one or the other); kept here rather than reused as a temporary so
    // its state stays continuous across blocks if you switch flavours
    // back and forth mid-note.
    VarFilter varFilt;
    MMEnv   filtEnv, ampEnv;

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
void        mmEngineInit();
void        mmEngineActivate();
void        mmParamsInitE();
void        mmSetParameterE(uint8_t id, float v);
float       mmGetParameterE(uint8_t id);
void        mmFormatParameterE(uint8_t id, char* buf, size_t n);
bool        mmParamIsLockableE(uint8_t id);
uint8_t     mmNumParamsE();
const ParamInfo* mmParamInfoE(uint8_t id);
uint8_t     mmNumPresetsE();
const char* mmPresetNameE(uint8_t idx);
void        mmLoadPresetE(uint8_t idx);
void        mmLoadInitPatchE();
void        mmSnapshotParamsE(uint8_t* out);
void        mmRestoreParamsE(const uint8_t* in);
uint8_t     mmNumVoicesE();
void        mmVoiceInitE();
void        mmNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        mmNoteOffE(uint8_t voiceIdx);
void        mmAllOffE();
void        mmSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     mmMasterParamIdE();
uint8_t     mmFilterCutoffParamIdE();
uint8_t     mmFilterResonanceParamIdE();
int         mmRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     mmNumPagesE();
const struct EnginePageDef* mmPageE(uint8_t idx);

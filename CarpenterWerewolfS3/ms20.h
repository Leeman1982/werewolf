#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  ms20.h — Korg MS-20 signal path, one voice (it is monophonic).
//
//  Built from scratch (no ESP32-S3 source — never staged upstream) from the
//  real 1978 semi-modular's own documented architecture: 2 VCOs into a
//  6dB/octave HPF feeding a 12dB/octave LPF in series (NOT one multimode
//  filter — two separate, individually resonant sections, both able to
//  self-oscillate), and two IDENTICAL 5-stage envelope generators whose
//  DEFAULT patch-panel routing, not their circuitry, is what makes this
//  engine's architecture so different from every other engine here.
//
//  What makes this an MS-20 rather than a generic 2-VCO mono synth:
//
//  1. EG1 is normalled to PITCH (both VCOs), and EG2 is normalled to BOTH
//     the filter cutoffs AND the VCA simultaneously — confirmed directly
//     against the real patch panel's own default connections. That is a
//     categorically different envelope architecture from every other
//     engine in this codebase, which all give the filter and amp their own
//     independent envelope: here there is no "amp envelope" as a separate
//     concept at all — MS20_AENV_* IS the filter envelope, driving both
//     destinations from the exact same contour, exactly as the real
//     hardware's normalled patch bay does. A dedicated PITCH envelope
//     (MS20_PENV_*, feeding both VCOs) is something no other engine here
//     has at all.
//  2. Both envelope generators share an unusual real HOLD stage: releasing
//     the key does NOT begin the release phase immediately — the envelope
//     holds at whatever level it was at for a fixed HOLD TIME first (like
//     briefly pressing a sustain pedal on every note-off), and only then
//     releases. Modelled in Ms20Env below exactly that way, applied
//     identically to both EG1 and EG2 since they are the same circuit on
//     the real instrument.
//  3. The HPF and LPF are two separate resonant sections in series (6dB and
//     12dB/octave on the real panel respectively), not one multimode filter
//     the way this codebase's SEM-derived and IR3109-derived filters
//     elsewhere are — and both independently reach true self-oscillation on
//     the real instrument. See Ms20Hpf below for the one disclosed
//     simplification this requires. Legendarily aggressive at high PEAK
//     (this codebase's own damping floor reaches exactly zero on both
//     sections, the same "can genuinely scream" family as the ARP 2600 and
//     System 100 filters here, not the "polite" Juno/Jupiter-8 one).
//  4. VCO1 hard-syncs VCO2 — note the direction: every other sync-equipped
//     engine in this codebase has its SECOND oscillator sync the first;
//     the MS-20's patch panel wires it the other way around. VCO2's own
//     wave switch also has a RING MOD position that replaces its normal
//     output with VCO1 x VCO2 (modelled as a third wave-select option,
//     MS20_VCO2_WAVE, not a separate always-on mix knob, since that is
//     genuinely how the real switch works).
//  5. Deliberately NOT modelled: the real front panel's External Signal
//     Processor (ESP) — an audio-in jack with its own pitch-to-CV tracker,
//     envelope follower and trigger detector, letting an external signal
//     (guitar, drum machine, voice) drive the synth's own VCOs/filter/
//     envelopes. Genuinely one of the MS-20's most famous features, but
//     this is an all-digital/MIDI instrument with no audio input anywhere
//     in its signal path to feed it — the same kind of honest scope-down
//     this codebase's CS-80 already applies to its ribbon controller.
// ============================================================================

#define MS20_VOICES 1     // the real instrument has one

enum Ms20Vco2Wave : uint8_t { MS20_V2_SAW = 0, MS20_V2_SQUARE, MS20_V2_RING };

struct Ms20Osc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;

    inline void setFreq(float hz) {
        if (hz < 0.5f) hz = 0.5f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    inline float advance(bool* wrapped = nullptr) {
        uint32_t prev = phase;
        phase += inc;
        if (wrapped) *wrapped = (phase < prev);
        return phaseToFloat(phase);
    }
    inline void resetPhase() { phase = 0; }
    inline float saw(float t) const { return 2.0f * t - 1.0f - polyBlep(t, dt, invDt); }
    inline float pulse(float t, float pw) const {
        float out = (t < pw) ? 1.0f : -1.0f;
        out += polyBlep(t, dt, invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= polyBlep(t2, dt, invDt);
        return out;
    }
};

struct Ms20Noise { uint32_t seed = 0x2051977u;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── HPF feeding LPF (12dB/octave, two-pole TPT) in series ─────────────────
// Two genuinely separate resonant sections, matching the real series
// signal path — not one multimode filter. Both reach true self-oscillation
// (damping floor hits zero), the "can genuinely scream" family this
// codebase's ARP 2600/System 100 filters already belong to.
//
// A reasoned simplification, disclosed rather than hidden: the real HPF
// section is specified as 6dB/octave (a single real pole) yet is ALSO
// specified as reaching true self-oscillation -- and a single real pole
// caps out at 90 degrees of phase shift, never the ~180 a negative-feedback
// loop needs to sustain its own oscillation (the same physics this
// codebase's own LadderFilter/ladder_filter.h explains for why IT needs
// four stages, not one). Since the real circuit's exact feedback path isn't
// documented anywhere this research reached, this models the HPF as the
// same 2-pole TPT state-variable design as the LPF, tapping its highpass
// output -- genuinely self-oscillating, matching the one property that
// actually matters for how this filter is heard and played, at the cost of
// the nominal pole count matching the "6dB" panel label exactly.
struct Ms20Hpf {
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 15000.0f) fcHz = 15000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        k = 2.0f * (1.0f - resonance01);
        if (k < 0.0f) k = 0.0f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline float process(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return x - k * v1 - v2;   // highpass tap
    }
};
struct Ms20Lpf {
    float s1 = 0.0f, s2 = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 15000.0f) fcHz = 15000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        float k = 2.0f * (1.0f - resonance01);
        if (k < 0.0f) k = 0.0f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline float process(float x) {
        const float v3 = x - s2;
        const float v1 = a1 * s1 + a2 * v3;
        const float v2 = s2 + a2 * s1 + a3 * v3;
        s1 = 2.0f * v1 - s1; s2 = 2.0f * v2 - s2;
        return v2;
    }
};

// ─── The shared EG1/EG2 circuit: Hold / Attack / Decay / Sustain / Release ──
// See file banner point 2: HOLD is not a pre-attack stage, it is a POST-
// RELEASE one — letting go of the key freezes the envelope at whatever
// level it is currently at for holdSamples before release begins, exactly
// matching the real documented "like holding a sustain pedal for a couple
// of seconds" behaviour. Both EG1 (pitch) and EG2 (filter+amp) are this
// same struct, since they are the same circuit on the real instrument.
enum Ms20EnvStage : uint8_t { MS20_E_IDLE = 0, MS20_E_ATTACK, MS20_E_DECAY, MS20_E_SUSTAIN, MS20_E_HOLD, MS20_E_RELEASE };
struct Ms20Env {
    uint8_t  stage = MS20_E_IDLE;
    float    level = 0.0f;
    float    atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    uint32_t holdSamplesTotal = 0, holdSamplesLeft = 0;

    inline void trigger(float atk, float dec, float s, float rel, uint32_t holdSamples) {
        stage = MS20_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
        holdSamplesTotal = holdSamples;
    }
    inline void release() {
        if (stage == MS20_E_IDLE) return;
        stage = MS20_E_HOLD;
        holdSamplesLeft = holdSamplesTotal;
    }
    inline float process() {
        switch (stage) {
        case MS20_E_IDLE: return 0.0f;
        case MS20_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = MS20_E_DECAY; }
            break;
        case MS20_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = MS20_E_SUSTAIN; }
            break;
        case MS20_E_SUSTAIN:
            level = sus;
            break;
        case MS20_E_HOLD:
            if (holdSamplesLeft > 0) holdSamplesLeft--;
            else stage = MS20_E_RELEASE;
            break;
        case MS20_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = MS20_E_IDLE; }
            break;
        }
        return level;
    }
};

struct Ms20Voice {
    Ms20Osc  osc1, osc2;
    Ms20Noise noise;
    Ms20Hpf  hpf;
    Ms20Lpf  lpf;
    VarFilter varFilt;   // alternate flavour -- see variable_filter.h
    Ms20Env  pitchEnv;   // EG1
    Ms20Env  filtAmpEnv; // EG2

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
void        ms20EngineInit();
void        ms20EngineActivate();
void        ms20ParamsInitE();
void        ms20SetParameterE(uint8_t id, float v);
float       ms20GetParameterE(uint8_t id);
void        ms20FormatParameterE(uint8_t id, char* buf, size_t n);
bool        ms20ParamIsLockableE(uint8_t id);
uint8_t     ms20NumParamsE();
const ParamInfo* ms20ParamInfoE(uint8_t id);
uint8_t     ms20NumPresetsE();
const char* ms20PresetNameE(uint8_t idx);
void        ms20LoadPresetE(uint8_t idx);
void        ms20LoadInitPatchE();
void        ms20SnapshotParamsE(uint8_t* out);
void        ms20RestoreParamsE(const uint8_t* in);
uint8_t     ms20NumVoicesE();
void        ms20VoiceInitE();
void        ms20NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        ms20NoteOffE(uint8_t voiceIdx);
void        ms20AllOffE();
void        ms20SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     ms20MasterParamIdE();
uint8_t     ms20FilterCutoffParamIdE();
uint8_t     ms20FilterResonanceParamIdE();
int         ms20RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     ms20NumPagesE();
const struct EnginePageDef* ms20PageE(uint8_t idx);

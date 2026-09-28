#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "ladder_filter.h"
#include "envelopes.h"
#include "variable_filter.h"

// ============================================================================
//  prophet5.h — Sequential Circuits Prophet-5 signal path, one voice.
//
//  Five voices, each with two DCOs. What makes this a Prophet-5 rather than a
//  generic dual-DCO synth:
//
//  1. OSC A and OSC B each mix Sawtooth and Pulse simultaneously (independent
//     level knobs, not a wave SELECTOR) — the real panel has no "choose one"
//     switch, so a patch can run saw and pulse layered from the same
//     oscillator, which no CS-80/Minimoog-style single-waveform osc can do.
//     Oscillator B alone gets a third waveform the real panel gives it and
//     Oscillator A never had — Triangle (P5_OSCB_TRI) — mixed in alongside
//     its saw/pulse exactly the same way.
//  2. Oscillator B can hard-sync Oscillator A (a genuine sync, not a
//     frequency-modulation approximation: A's phase is forced to 0 every
//     time B wraps), and can also run at sub-audio "LO FREQ" rates to become
//     an LFO in its own right.
//  3. Poly-Mod: the FILTER ENVELOPE and OSCILLATOR B can each be routed, with
//     their own amount, to Osc A's frequency and/or the filter cutoff — a
//     second, independent modulation path on top of the Wheel-Mod LFO, and
//     unlike Wheel-Mod it is NOT scaled by the mod wheel; it is always live
//     at whatever amount is dialled in, which is what gives Poly-Mod patches
//     their edge even with the mod wheel at rest.
//  4. Full ADSR contours on both filter and amp — in deliberate contrast to
//     the Minimoog engine elsewhere in this codebase, whose contours have no
//     Release control at all. The Prophet's really do have four stages.
//  5. UNISON: all five voices can be stacked on one note. Implemented at the
//     engine level (see prophet5.cpp) rather than through the shared voice
//     allocator, since unison is fundamentally "one note plays every voice",
//     the opposite of what the allocator is for.
//  6. No velocity or aftertouch sensitivity anywhere in the audio path,
//     deliberately: the original keybed (Rev 1 through 3.x, everything this
//     engine models) had neither — every note plays at the same level no
//     matter how hard it's struck. Both are still accepted over MIDI (so a
//     step sequencer or a modern controller can drive it) but never reach
//     the amplitude or anything else; that sensitivity wasn't added until
//     the 2020 Rev 4 reissue, a different, later instrument.
// ============================================================================

// Sized for the Prophet-10 derived engine (two 5-voice banks); the plain
// Prophet-5 engine runs the same code with polyphony set to 5 at runtime —
// see p5SetPolyphonyE in prophet5.cpp and prophet10.cpp.
#define P5_VOICES 10

// ─── DCO: simultaneous saw + pulse, PolyBLEP-corrected ──────────────────────
struct P5Osc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
    bool     loFreq = false;      // OSC B "LO FREQ" switch — runs as an LFO

    inline void setFreq(float hz) {
        float lo = loFreq ? 0.05f : 0.5f;
        if (hz < lo) hz = lo;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    // Advances phase, returns the new phase's fraction 0..1, and reports
    // whether this step wrapped (for OSC A's hard-sync-from-B).
    inline float advance(bool* wrapped) {
        uint32_t p0 = phase;
        phase += inc;
        if (wrapped) *wrapped = (phase < p0);
        return phaseToFloat(phase);
    }
    inline void resetPhase() { phase = 0; }

    inline float saw(float t) const { return 2.0f * t - 1.0f - polyBlep(t, dt, invDt); }
    inline float pulse(float t, float pw) const {
        float out = (t < pw) ? 1.0f : -1.0f;
        out += polyBlep(t, dt, invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= polyBlep(t2, dt, invDt);
        return out - (2.0f * pw - 1.0f);
    }
    inline float triangle(float t) const { return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t); }
};

struct P5Noise { uint32_t seed = 0xC0FFEEu;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

struct P5Voice {
    P5Osc  oscA, oscB;
    P5Noise noise;
    LadderFilter filter;
    VarFilter varFilt;   // alternate flavour — see variable_filter.h
    ADSREnv filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;

    float    detuneCents = 0.0f;    // fixed per-voice offset, used only in UNISON
    float    aftertouch = 0.0f;
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
// p5SetPolyphonyE(5) / (10) is what turns this same implementation into the
// Prophet-5 or Prophet-10 engine — see prophet10.cpp.
void        p5SetPolyphonyE(uint8_t n);
void        p5EngineInit();
void        p5EngineActivate();
void        p5ParamsInitE();
void        p5SetParameterE(uint8_t id, float v);
float       p5GetParameterE(uint8_t id);
void        p5FormatParameterE(uint8_t id, char* buf, size_t n);
bool        p5ParamIsLockableE(uint8_t id);
uint8_t     p5NumParamsE();
const ParamInfo* p5ParamInfoE(uint8_t id);
uint8_t     p5NumPresetsE();
const char* p5PresetNameE(uint8_t idx);
void        p5LoadPresetE(uint8_t idx);
void        p5LoadInitPatchE();
void        p5SnapshotParamsE(uint8_t* out);
void        p5RestoreParamsE(const uint8_t* in);
uint8_t     p5NumVoicesE();
void        p5VoiceInitE();
void        p5NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        p5NoteOffE(uint8_t voiceIdx, int note);
void        p5AllOffE();
void        p5SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     p5MasterParamIdE();
uint8_t     p5FilterCutoffParamIdE();
uint8_t     p5FilterResonanceParamIdE();
int         p5RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     p5NumPagesE();
const struct EnginePageDef* p5PageE(uint8_t idx);

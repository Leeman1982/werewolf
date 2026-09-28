#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "envelopes.h"
#include "variable_filter.h"

// ============================================================================
//  prophetvs.h — Sequential Prophet-VS signal path, one voice.
//
//  Built from scratch (no ESP32-S3 source — never staged upstream) from the
//  real 1986 flagship's own documented architecture: the first production
//  synthesizer to use VECTOR SYNTHESIS — 4 digital oscillators (A/B/C/D)
//  per voice, each contributing to the output at a level set by ONE shared
//  X/Y position inside a diamond with the 4 oscillators at its corners,
//  moved live by a joystick — feeding a genuine analogue Curtis-chip
//  resonant filter. The one engine in this codebase whose main "mixer" is
//  a 2D position rather than 4 independent level knobs.
//
//  Scope note up front: the real VS reads each oscillator's waveform from
//  a table of 127 12-bit single-cycle samples (128th slot: noise) and
//  drives its vector position from a genuinely 5-stage, 12-parameter,
//  loopable envelope generator. This engine models 5 waveform TYPES per
//  oscillator (sine/triangle/saw/square/noise) rather than the full 127-
//  sample library, and a 2-waypoint, optionally-looping vector envelope
//  rather than the full 5-stage/12-parameter one — the same kind of
//  disclosed trade-off this codebase's PPG Wave and DX7 engines already
//  make for their own real, exotic hardware. What's preserved exactly is
//  the one property that actually defines vector synthesis: ONE position
//  driving all four oscillators' mix simultaneously, and that position
//  itself able to move on its own over time.
//
//  What makes this a Prophet-VS rather than a generic 4-oscillator synth:
//
//  1. The four oscillators are never mixed by four independent level
//     knobs — VS_VECTOR_X/Y (or the vector envelope moving them) sets all
//     four contributions at once via bilinear interpolation across the
//     diamond A/B/C/D corners, exactly matching the real joystick's own
//     "diamond" mixing law.
//  2. A dedicated VECTOR envelope (distinct from the filter and amp
//     envelopes every other engine here already has one or two of)
//     automates the X/Y position itself over time, with an optional loop
//     between its two waypoints — the real instrument's own third
//     envelope generator, "oscillator mix," doing a job no other envelope
//     in this codebase does: modulating a 2D position, not a single
//     scalar.
//  3. Genuine analogue filtering (a Curtis CEM3372-family resonant
//     lowpass) on fully digital oscillators — the same hybrid signal path
//     category as this codebase's PPG Wave, but a completely different,
//     unrelated real filter chip and a completely different oscillator
//     technique (bilinear vector-crossfaded fixed waveforms, not a
//     scanned wavetable).
// ============================================================================

#define VS_VOICES 8   // exact match to the real 8-voice instrument

enum VsWave : uint8_t { VS_W_SINE = 0, VS_W_TRIANGLE, VS_W_SAW, VS_W_SQUARE, VS_W_NOISE, VS_W_COUNT };

struct VsOsc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
    uint32_t noiseSeed = 0x5645531u;

    inline void setFreq(float hz) {
        if (hz < 0.5f) hz = 0.5f;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    inline float next(uint8_t wave) {
        if (wave == VS_W_NOISE) {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            return (float)((int32_t)noiseSeed) * 4.6566128752458e-10f;
        }
        phase += inc;
        float t = phaseToFloat(phase);
        switch (wave) {
        case VS_W_SINE:     return sineFromPhase(phase);
        case VS_W_TRIANGLE: return (t < 0.5f) ? (4.0f * t - 1.0f) : (3.0f - 4.0f * t);
        case VS_W_SQUARE: {
            float out = (t < 0.5f) ? 1.0f : -1.0f;
            out += polyBlep(t, dt, invDt);
            float t2 = t - 0.5f; if (t2 < 0.0f) t2 += 1.0f;
            out -= polyBlep(t2, dt, invDt);
            return out;
        }
        default: return 2.0f * t - 1.0f - polyBlep(t, dt, invDt);   // VS_W_SAW
        }
    }
};

// ─── The Curtis-family analogue resonant lowpass (CEM3372 lineage) ────────
// TPT SVF, the same proven topology as this codebase's other "real
// hardware" filters, with its own damping law: a self-oscillating floor,
// slightly more restrained than the ARP 2600/System 100 family's true zero
// but well past the Juno/Jupiter-8 "polite" one — the CEM3372's own
// documented middle-ground character.
struct VsFilter {
    float ic1 = 0.0f, ic2 = 0.0f;
    float k = 1.0f, a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;
    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        k = 0.10f + (1.0f - resonance01) * 3.6f;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
    }
    inline float process(float x) {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return v2;
    }
};

// ─── Vector envelope: two waypoints, optional loop -- see file banner
// point 2. Moves the SAME X/Y position VS_VECTOR_X/Y would otherwise hold
// still at, from wherever the voice started (the static joystick position
// at note-on) to waypoint 1, then to waypoint 2, then either stops there
// or loops back to waypoint 1 ─────────────────────────────────────────────
enum VsVecStage : uint8_t { VS_VEC_IDLE = 0, VS_VEC_TO1, VS_VEC_TO2, VS_VEC_DONE };
struct VsVecEnv {
    uint8_t stage = VS_VEC_IDLE;
    float   x = 0.0f, y = 0.0f;             // current position
    float   x1 = 0.0f, y1 = 0.0f, rate1 = 0.01f;
    float   x2 = 0.0f, y2 = 0.0f, rate2 = 0.01f;
    bool    loop = false;

    inline void trigger(float startX, float startY, float tx1, float ty1, float r1,
                        float tx2, float ty2, float r2, bool doLoop) {
        x = startX; y = startY;
        x1 = tx1; y1 = ty1; rate1 = r1;
        x2 = tx2; y2 = ty2; rate2 = r2;
        loop = doLoop;
        stage = VS_VEC_TO1;
    }
    static inline bool approach(float& v, float target, float rate) {
        float d = target - v;
        if (fabsf(d) < rate) { v = target; return true; }
        v += (d > 0.0f ? rate : -rate);
        return false;
    }
    inline void process() {
        switch (stage) {
        case VS_VEC_IDLE: return;
        case VS_VEC_TO1: {
            bool doneX = approach(x, x1, rate1), doneY = approach(y, y1, rate1);
            if (doneX && doneY) stage = VS_VEC_TO2;
            break;
        }
        case VS_VEC_TO2: {
            bool doneX = approach(x, x2, rate2), doneY = approach(y, y2, rate2);
            if (doneX && doneY) stage = loop ? VS_VEC_TO1 : VS_VEC_DONE;
            break;
        }
        case VS_VEC_DONE: default: break;
        }
    }
};

struct VsVoice {
    VsOsc  oscA, oscB, oscC, oscD;
    VsFilter filter;
    VarFilter varFilt;   // alternate flavour -- see variable_filter.h
    ADSREnv  filtEnv, ampEnv;
    VsVecEnv vecEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;
};

// Per-voice fixed offsets — 8 independent digital-oscillator/analogue-
// filter channels still drift on the analogue side, same technique as
// every other polyphonic engine in this codebase.
extern const float VS_VOICE_DETUNE[VS_VOICES];   // cents
extern const float VS_VOICE_FILT[VS_VOICES];     // fraction of an octave on cutoff
extern const float VS_VOICE_LEVEL[VS_VOICES];    // linear gain trim

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        vsEngineInit();
void        vsEngineActivate();
void        vsParamsInitE();
void        vsSetParameterE(uint8_t id, float v);
float       vsGetParameterE(uint8_t id);
void        vsFormatParameterE(uint8_t id, char* buf, size_t n);
bool        vsParamIsLockableE(uint8_t id);
uint8_t     vsNumParamsE();
const ParamInfo* vsParamInfoE(uint8_t id);
uint8_t     vsNumPresetsE();
const char* vsPresetNameE(uint8_t idx);
void        vsLoadPresetE(uint8_t idx);
void        vsLoadInitPatchE();
void        vsSnapshotParamsE(uint8_t* out);
void        vsRestoreParamsE(const uint8_t* in);
uint8_t     vsNumVoicesE();
void        vsVoiceInitE();
void        vsNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        vsNoteOffE(uint8_t voiceIdx);
void        vsAllOffE();
void        vsSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     vsMasterParamIdE();
uint8_t     vsFilterCutoffParamIdE();
uint8_t     vsFilterResonanceParamIdE();
int         vsRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     vsNumPagesE();
const struct EnginePageDef* vsPageE(uint8_t idx);

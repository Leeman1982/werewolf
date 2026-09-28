#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "envelopes.h"
#include "variable_filter.h"
#include "oberheim.h"   // SemSvf/semModeMorph — see the file banner below for why this is shared

// ============================================================================
//  obx.h — Oberheim OB-X signal path, one voice.
//
//  Built from scratch (no ESP32-S3 source — a Simonetti-credited instrument
//  never staged upstream) from the real 1979 flagship's own documented
//  architecture: up to 8 discrete analogue voice cards, each 2 VCOs (saw or
//  variable pulse) into a 12dB/octave resonant lowpass, 2 dedicated ADSR
//  contours (filter, amp), an "enhanced LFO" routed to pitch and/or cutoff,
//  and — the OB-X's own signature over every other Oberheim in this
//  codebase — a genuine polyphonic sample & hold and a documented,
//  transposable "unison chord" hold feature.
//
//  What makes this an OB-X rather than a generic 2-VCO poly, or a re-skin of
//  this codebase's own Four-Voice/SEM engine (oberheim.h/.cpp):
//
//  1. The filter genuinely IS the SEM's own 12dB/octave state-variable design
//     — period documentation describes the OB-X's VCF as literally "the SVF
//     from the SEM synthesizer," discrete-component, pre-dating the OB-Xa's
//     switch to Curtis CEM3320 ICs. So this engine reuses SemSvf verbatim
//     (see oberheim.h) rather than redeclaring an equivalent filter — the
//     same shared-hardware justification this codebase already uses for the
//     Minimoog/Moog Modular ladder. What's different from the Four-Voice
//     engine is the panel, not the circuit: the OB-X's front panel wires the
//     SVF's LOWPASS output only (OBX_FILTER_TYPE's STOCK option), with no
//     continuous mode-morph knob — that multimode CV-patch access was a
//     stand-alone-SEM-module trait the fully integrated OB-X voice card
//     doesn't expose.
//  2. CROSS MOD: a switch feeding VCO2 into VCO1's frequency at audio rate —
//     the original OB-X's own panel feature (later replaced on the OB-Xa by
//     a Filter-Envelope-to-OSC2-pitch option instead, which this engine does
//     NOT model, since it belongs to that later, different instrument).
//     Modelled as a continuous amount rather than the real on/off switch,
//     the same liberty this codebase's Jupiter-8 already takes with its own
//     cross-mod slider.
//  3. Polyphonic sample & hold: each voice gets its OWN noise-fed S&H
//     generator (not one shared global S&H the way this codebase's mono
//     engines model it) — a real, specifically-documented OB-X feature ("32
//     patches... automatic tuning... polyphonic sample/hold" per the
//     original owner's manual).
//  4. HOLD / "unison chord": the OB-X manual documents a transposable chord
//     hold as a signature performance feature — hold a chord, release it,
//     and it's memorized; the next single key replays that same chord
//     shape transposed to the new root. Modelled at the engine level (see
//     obx.cpp), entirely separate from the shared voice allocator, the same
//     way this codebase's Prophet-5 UNISON bypasses it. Distinct from plain
//     OBX_UNISON (all voices stacked on one held note, for "monster mono
//     lines" — the OB-X's OTHER, simpler documented unison mode).
//  5. Auto-Tune trims pitch, not everything: the real OB-X's Z-80 tuning
//     routine keeps its 8 discrete VCOs close in pitch, but doesn't touch
//     filter-cutoff or level trim card-to-card — so this engine's per-voice
//     drift (OBX_VOICE_*) uses a much smaller pitch spread than the
//     hand-tuned Four-Voice/SEM engine's, while keeping comparable filter/
//     level spread. See the file banner comparison in oberheim.h.
// ============================================================================

#define OBX_VOICES 8

struct ObxOsc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
    float    baseHz = 220.0f;   // last commanded frequency, before audio-rate cross-mod

    inline void setFreq(float hz) {
        if (hz < 0.05f) hz = 0.05f;
        baseHz = hz;
        inc = freqToPhaseInc(hz);
        dt  = hz * INV_SAMPLE_RATE;
        if (dt > 0.45f) dt = 0.45f;
        invDt = 1.0f / dt;
    }
    // Audio-rate CROSS MOD retunes only the phase increment, leaving dt/invDt
    // sized to the carrier's own base pitch — same technique as this
    // codebase's Jupiter-8 (Jp8Osc), for the same reason: keeps the PolyBLEP
    // correction well-behaved under FM instead of chasing the modulated rate.
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
    inline float saw(float t) const { return 2.0f * t - 1.0f - polyBlep(t, dt, invDt); }
    inline float pulse(float t, float pw) const {
        float out = (t < pw) ? 1.0f : -1.0f;
        out += polyBlep(t, dt, invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= polyBlep(t2, dt, invDt);
        return out;
    }
};

struct ObxNoise { uint32_t seed = 0xAB0Bu;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

struct ObxVoice {
    ObxOsc   osc1, osc2;
    ObxNoise noise;
    SemSvf   filter;      // the SEM's own 12dB SVF — LP tap only, see file banner
    VarFilter varFilt;    // alternate flavour -- see variable_filter.h
    ADSREnv  filtEnv, ampEnv;

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;

    // Polyphonic sample & hold: this voice's OWN noise-fed S&H, not one
    // shared global generator — see file banner point 3.
    float    shPhase = 0.0f;
    float    shValue = 0.0f;
};

// Per-voice fixed offsets. Pitch spread is deliberately smaller than the
// Four-Voice/SEM engine's own (OB_VOICE_DETUNE, oberheim.h) — the OB-X's
// Z-80 auto-tune routine keeps its 8 VCOs close in pitch — while filter/
// level spread stays comparable, since auto-tune never touches either.
extern const float OBX_VOICE_DETUNE[OBX_VOICES];   // cents
extern const float OBX_VOICE_FILT[OBX_VOICES];     // fraction of an octave on cutoff
extern const float OBX_VOICE_LEVEL[OBX_VOICES];    // linear gain trim

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        obxEngineInit();
void        obxEngineActivate();
void        obxParamsInitE();
void        obxSetParameterE(uint8_t id, float v);
float       obxGetParameterE(uint8_t id);
void        obxFormatParameterE(uint8_t id, char* buf, size_t n);
bool        obxParamIsLockableE(uint8_t id);
uint8_t     obxNumParamsE();
const ParamInfo* obxParamInfoE(uint8_t id);
uint8_t     obxNumPresetsE();
const char* obxPresetNameE(uint8_t idx);
void        obxLoadPresetE(uint8_t idx);
void        obxLoadInitPatchE();
void        obxSnapshotParamsE(uint8_t* out);
void        obxRestoreParamsE(const uint8_t* in);
uint8_t     obxNumVoicesE();
void        obxVoiceInitE();
void        obxNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        obxNoteOffE(uint8_t voiceIdx, int note);
void        obxAllOffE();
void        obxSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     obxMasterParamIdE();
uint8_t     obxFilterCutoffParamIdE();
uint8_t     obxFilterResonanceParamIdE();
int         obxRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     obxNumPagesE();
const struct EnginePageDef* obxPageE(uint8_t idx);

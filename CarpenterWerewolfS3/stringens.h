#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  stringens.h — ARP/Solina String Ensemble signal path.
//
//  Named stringens.h/.cpp rather than strings.h/.cpp deliberately: the
//  latter collides with the POSIX C library header <strings.h> (strcasecmp
//  etc.), which glibc's own <string.h> pulls in under _DEFAULT_SOURCE —
//  something Arduino.h's shim ends up triggering on a host build. With
//  this engine's own directory on the include path (-I .), a same-named
//  local header shadows the real system one and breaks every other
//  translation unit that transitively includes <string.h>, not just this
//  file — caught only once a second engine (prophetvs.cpp) exposed it.
//
//  Built from scratch (no ESP32-S3 source — never staged upstream) from the
//  real 1974 string machine's own documented architecture: 12 master
//  oscillators and organ-style octave dividers, not individually-tuned
//  VCOs — the ONE engine in this codebase built on divide-down "organ"
//  technology rather than a bank of independent per-voice oscillators. It
//  has no filter of any kind on the real instrument; all of its character
//  comes from six fixed, individually-switchable instrument "voicings" and
//  a three-line modulated-delay ensemble/chorus effect that IS the sound,
//  not an optional add-on the way this codebase's shared FX chain is for
//  every other engine.
//
//  What makes this a String Ensemble rather than a generic poly pad engine:
//
//  1. Six fixed instrument voicings — Violin, Viola, Trumpet, Horn, Cello,
//     Contrabass — each its own fixed one-pole tone colour (not a
//     player-adjustable filter) applied to the SAME shared oscillator per
//     note, individually level-controlled so several can layer at once
//     (real hardware's own on/off buttons, modelled continuous like every
//     other engine here to allow blending rather than only full on/off).
//  2. Cello and Contrabass are genuinely different from the other four:
//     the real instrument restricts them to the keyboard's lowest ~20 keys
//     and makes them MONOPHONIC, driven by whichever held note is lowest
//     (a bass coupler, not a normal voice) — modelled at the engine level,
//     entirely separate from the polyphonic voice pool, the same way this
//     codebase's Prophet-5 UNISON and OB-X HOLD already bypass the shared
//     voice allocator for a feature it can't represent on its own.
//  3. The simplest envelope in this codebase: just two stages, Attack
//     ("Crescendo") and Release ("Sustain Length") — no decay, no sustain
//     LEVEL knob at all, since the real panel doesn't have one either.
//  4. The ensemble/chorus effect (three phase-offset modulated delay
//     lines) is core to this instrument's identity, not a bypassable
//     post-effect — it runs in this engine's own render path, in addition
//     to (and before) the shared g_fxChorus/g_fxDelay/g_fxReverb chain
//     every engine already has access to.
//  5. Deliberately absent: any filter cutoff/resonance in the STOCK path
//     (real hardware has none) and glide (organ-style divide-down
//     instruments don't have portamento — there's no VCO to slew). A
//     non-STOCK FILT TYPE row is still offered as an optional enhancement
//     beyond the real hardware, the same trade this codebase's Mellotron
//     already makes for the identical reason: it costs nothing when left
//     on STOCK, and every existing preset stays untouched by it.
// ============================================================================

#define SE_VOICES 10       // polyphonic Violin/Viola/Trumpet/Horn pool
// Cello/Contrabass are a separate, single, monophonic bass layer -- see
// file banner point 2 -- restricted to notes at/below this, a reasoned
// approximation of "the lower ~20 keys" of the real 49-key F-to-F keyboard.
#define SE_BASS_SPLIT_NOTE 55

struct SeOsc {
    uint32_t phase = 0, inc = 0;
    float    dt = 0.01f, invDt = 100.0f;
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
        return 2.0f * t - 1.0f - polyBlep(t, dt, invDt);
    }
};

// One fixed one-pole tone colour per voicing -- NOT a player-adjustable
// filter (see file banner point 5), just the same handful of coefficients
// every note of that voicing always uses.
struct SeColorFilter {
    float z = 0.0f;
    inline float process(float x, float g) { z += g * (x - z); return z; }
};

// ─── The simplest envelope in this codebase: Attack then Release, nothing
// else -- see file banner point 3 ───────────────────────────────────────
enum SeEnvStage : uint8_t { SE_ENV_IDLE = 0, SE_ENV_ATTACK, SE_ENV_RELEASE };
struct SeEnv {
    uint8_t stage = SE_ENV_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, relRate = 0.01f;
    inline void trigger(float atk, float rel) { stage = SE_ENV_ATTACK; atkRate = atk; relRate = rel; }
    inline void release() { if (stage != SE_ENV_IDLE) stage = SE_ENV_RELEASE; }
    inline float process() {
        switch (stage) {
        case SE_ENV_IDLE: return 0.0f;
        case SE_ENV_ATTACK:
            // Holds at full once it arrives -- there is no sustain stage
            // to fall to; process() just keeps returning 1.0 here (still
            // nominally "in ATTACK") until release() is called from
            // outside on note-off.
            level += atkRate;
            if (level >= 1.0f) level = 1.0f;
            break;
        case SE_ENV_RELEASE:
            level -= relRate;
            if (level <= 0.0f) { level = 0.0f; stage = SE_ENV_IDLE; }
            break;
        }
        return level;
    }
};

struct SeVoice {
    SeOsc  osc;
    SeColorFilter violinF, violaF, trumpetF, hornF;
    SeEnv  env;

    int      note = -1;
    float    noteFreq = 220.0f;   // this note's plain frequency, pitch bend re-applied at control rate
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;
    float    aftertouch = 0.0f;
};

// The monophonic Cello/Contrabass bass layer -- one instance, entirely
// separate from the SeVoice pool above (see file banner point 2).
struct SeBassVoice {
    SeOsc  osc;
    SeColorFilter celloF, contraF;
    SeEnv  env;
    int    note = -1;
    float  noteFreq = 110.0f;
};

// ─── The three-line modulated-delay ensemble/chorus -- see file banner
// point 4. Three phase-offset (120 degrees apart) taps off one delay
// buffer, standing in for the real unit's dual-LFO/three-BBD-line design. ─
#define SE_ENS_BUF_SIZE 512   // ~10.6 ms at 48 kHz -- ample for a chorus tap
struct SeEnsemble {
    float    buf[SE_ENS_BUF_SIZE] = { 0.0f };
    uint32_t writePos = 0;
    uint32_t lfoPhase = 0;

    float process(float x, float rateHz, float depth);
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        seEngineInit();
void        seEngineActivate();
void        seParamsInitE();
void        seSetParameterE(uint8_t id, float v);
float       seGetParameterE(uint8_t id);
void        seFormatParameterE(uint8_t id, char* buf, size_t n);
bool        seParamIsLockableE(uint8_t id);
uint8_t     seNumParamsE();
const ParamInfo* seParamInfoE(uint8_t id);
uint8_t     seNumPresetsE();
const char* sePresetNameE(uint8_t idx);
void        seLoadPresetE(uint8_t idx);
void        seLoadInitPatchE();
void        seSnapshotParamsE(uint8_t* out);
void        seRestoreParamsE(const uint8_t* in);
uint8_t     seNumVoicesE();
void        seVoiceInitE();
void        seNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        seNoteOffE(uint8_t voiceIdx, int note);
void        seAllOffE();
void        seSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     seMasterParamIdE();
uint8_t     seFilterCutoffParamIdE();
uint8_t     seFilterResonanceParamIdE();
int         seRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     seNumPagesE();
const struct EnginePageDef* sePageE(uint8_t idx);

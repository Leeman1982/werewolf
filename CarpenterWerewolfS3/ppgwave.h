#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"

// ============================================================================
//  ppgwave.h — PPG Wave 2.3 signal path, one voice.
//
//  Built from scratch (no ESP32-S3 source — a Simonetti-credited instrument
//  never staged upstream) from the real 1984 flagship's own documented
//  architecture: 8 voices, 2 DIGITAL wavetable oscillators per voice feeding
//  one ANALOGUE SSM2044 4-pole (24dB/octave) resonant lowpass and a CEM3360
//  VCA — a hybrid digital/analogue signal path, the one thing that makes
//  this engine categorically different from every other engine in this
//  codebase, which are all analogue oscillator -> analogue filter designs.
//
//  Exact measurements this models directly, from the real instrument's own
//  documented wavetable format: each "wave" is a single-cycle waveform
//  PPG_WAVE_LEN (256) samples long; PPG_WAVES_PER_TABLE (64) of them make up
//  one wavetable, and the oscillator continuously reads through that
//  sequence at a WAVE POSITION (0..63) that can sweep in real time. The real
//  unit shipped 30 such tables; this engine models PPG_NUM_TABLES (3),
//  generated procedurally at boot into SRAM — same dsp_tables.h discipline
//  as this codebase's sine/tanh tables — rather than reproduced from the
//  actual ROM dump, which isn't available here. The three chosen cover the
//  format's best-known real table characters (a brightness sweep, a two-
//  formant vowel-ish sweep, and a pulse-width sweep) rather than attempting
//  a literal byte-for-byte factory-ROM copy. Stored as int8_t rather than
//  float — a deliberate 4x memory saving over this codebase's usual float
//  tables, and also what the real hardware's own ROM held in the first
//  place, so it costs nothing in authenticity.
//
//  What makes this a PPG Wave rather than a generic wavetable synth:
//
//  1. No anti-aliasing/mip-mapping of the wavetables, ever — deliberately,
//     the same design choice this codebase's CS-80 makes for its own reset
//     glitch (cs80_voice.h). The real PPG Wave's fixed 256-sample tables
//     alias at high pitches; that grainy, faintly-digital top end is a
//     well-known, discussed, embraced part of the instrument's character,
//     not a bug this engine works around.
//  2. THREE independent envelope generators, not two: two full ADSRs
//     (filter, amp — PPG_FENV_*/PPG_AENV_*) plus a dedicated, simpler AD
//     (attack/decay only, no sustain) envelope whose entire job is sweeping
//     the WAVE POSITION in real time (PpgAdEnv, "Env3" on the real panel) —
//     nothing else in this codebase has a modulation source dedicated
//     entirely to one destination like this.
//  3. WAVE KBD TRACK: the real panel can make the wave-sweep envelope's own
//     rate follow the keyboard, so the same sweep plays proportionally
//     faster the higher up the keyboard you play — the same keyboard-
//     tracked-timing convention this codebase already applies to filter
//     cutoff elsewhere, just applied to Env3's attack/decay rate instead.
//  4. Two independently-live, non-wheel-scaled modulation depths (Env3's own
//     amount, and the LFO's amount into wave position) standing in for the
//     real unit's documented DUAL modulation wheels — a second physical
//     performance control this codebase's single MIDI mod-wheel input can't
//     directly represent, so its EFFECT (an always-on depth alongside the
//     wheel-scaled one) is modelled instead of the missing second wheel
//     itself, the same kind of substitution this codebase's CS-80 already
//     makes for its ribbon controller.
// ============================================================================

#define PPG_VOICES 8
#define PPG_WAVE_LEN 256          // samples per single-cycle wave — exact, real
#define PPG_WAVES_PER_TABLE 64    // waves per wavetable — exact, real
#define PPG_NUM_TABLES 3          // real unit shipped 30; see file banner

extern const char* const PPG_TABLE_NAMES[PPG_NUM_TABLES];

// Builds the procedural wavetable ROM once — call from ppgEngineInit() only.
void ppgWavetablesInit();

struct PpgOsc {
    uint32_t phase = 0, inc = 0;
    uint8_t  table = 0;
    float    wavePos = 0.0f;   // 0..63, continuous — the real WAVES-SUB knob plus modulation

    inline void setFreq(float hz) {
        if (hz < 0.5f) hz = 0.5f;
        inc = freqToPhaseInc(hz);
    }
    // Bilinear read: interpolates both within one wave's 256 samples (from
    // the low 24 bits of phase) and between the two nearest wave positions
    // (from wavePos's fraction) — real hardware jumps between discrete
    // waves with no such crossfade, but interpolating avoids an audible
    // zipper/step on every scan and is the same trade-off modern digital
    // recreations of this format make.
    float next();
};

struct PpgNoise { uint32_t seed = 0x99667u;
    inline float white() { seed = seed * 1664525u + 1013904223u; return (float)((int32_t)seed) * 4.6566128752458e-10f; }
};

// ─── SSM2044: 4-pole (24dB/octave), voltage-controlled cutoff AND resonance ─
// TPT integrator cascade (two 2-pole stages in series, always both — the
// real chip has no switchable slope, unlike this codebase's Jupiter-8),
// with a damping floor that reaches near zero: period write-ups describe the
// SSM2044's resonance path as a "true open loop design" delivering a fat,
// aggressive character, closer in spirit to this codebase's ARP 2600/System
// 100 filter family (floor reaches zero, screams) than the polite Juno/
// Jupiter-8 one — self-oscillation itself isn't confirmed by the research
// behind this engine, so this is a reasoned choice, not a measured one.
struct PpgSvf {
    float s1 = 0.0f, s2 = 0.0f, s1b = 0.0f, s2b = 0.0f;
    float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f;

    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        float g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        float k = 2.0f * (1.0f - resonance01);
        if (k < 0.02f) k = 0.02f;
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
    inline float process(float x) {
        float y = lpStage(x, a1, a2, a3, s1, s2);
        return lpStage(y, a1, a2, a3, s1b, s2b);
    }
};

enum PpgEnvStage : uint8_t { PPG_E_IDLE = 0, PPG_E_ATTACK, PPG_E_DECAY, PPG_E_SUSTAIN, PPG_E_RELEASE };
struct PpgEnv {
    uint8_t stage = PPG_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;
    inline void trigger(float atk, float dec, float s, float rel) {
        stage = PPG_E_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != PPG_E_IDLE) stage = PPG_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case PPG_E_IDLE: return 0.0f;
        case PPG_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = PPG_E_DECAY; }
            break;
        case PPG_E_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = PPG_E_SUSTAIN; }
            break;
        case PPG_E_SUSTAIN: level = sus; break;
        case PPG_E_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = PPG_E_IDLE; }
            break;
        }
        return level;
    }
};

// Env3 — a plain one-shot Attack/Decay, no sustain, no release: it plays out
// on its own timeline once triggered, exactly like the real dedicated wave-
// sweep envelope, and is never re-gated by note-off — the amp envelope
// (PpgEnv, above) is what determines whether the voice is actually alive.
enum PpgAdStage : uint8_t { PPG_AD_IDLE = 0, PPG_AD_ATTACK, PPG_AD_DECAY };
struct PpgAdEnv {
    uint8_t stage = PPG_AD_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f;
    inline void trigger(float atk, float dec) { stage = PPG_AD_ATTACK; atkRate = atk; decCoef = dec; }
    inline float process() {
        switch (stage) {
        case PPG_AD_IDLE: return 0.0f;
        case PPG_AD_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = PPG_AD_DECAY; }
            break;
        case PPG_AD_DECAY:
            level -= level * decCoef;
            if (level < 0.0005f) { level = 0.0f; stage = PPG_AD_IDLE; }
            break;
        }
        return level;
    }
};

struct PpgVoice {
    PpgOsc   oscA, oscB;
    PpgNoise noise;
    PpgSvf   filter;
    VarFilter varFilt;   // alternate flavour -- see variable_filter.h
    PpgEnv   filtEnv, ampEnv;
    PpgAdEnv waveEnv;    // Env3 — see file banner point 2

    int      note = -1;
    uint8_t  velocity = 0;
    float    velNorm = 0.0f;
    bool     gate = false;

    float    targetFreq = 220.0f, currentFreq = 220.0f;
    bool     gliding = false;
    float    glideCoef = 1.0f;
    float    aftertouch = 0.0f;

    // LFO DELAY: the real panel's own knob for how long the LFO takes to
    // fade in after a key is struck (vibrato-on-a-held-note, not present at
    // the moment of attack) — tracked per voice, since each note's own
    // vibrato fades in from when THAT note started, not from some shared
    // global clock.
    float    lfoDelayGain = 0.0f;
};

// Per-voice fixed offsets — 8 independent analogue SSM2044/CEM3360 stages
// behind a shared digital front end still drift, same technique as every
// other polyphonic engine in this codebase.
extern const float PPG_VOICE_DETUNE[PPG_VOICES];   // cents
extern const float PPG_VOICE_FILT[PPG_VOICES];     // fraction of an octave on cutoff
extern const float PPG_VOICE_LEVEL[PPG_VOICES];    // linear gain trim

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        ppgEngineInit();
void        ppgEngineActivate();
void        ppgParamsInitE();
void        ppgSetParameterE(uint8_t id, float v);
float       ppgGetParameterE(uint8_t id);
void        ppgFormatParameterE(uint8_t id, char* buf, size_t n);
bool        ppgParamIsLockableE(uint8_t id);
uint8_t     ppgNumParamsE();
const ParamInfo* ppgParamInfoE(uint8_t id);
uint8_t     ppgNumPresetsE();
const char* ppgPresetNameE(uint8_t idx);
void        ppgLoadPresetE(uint8_t idx);
void        ppgLoadInitPatchE();
void        ppgSnapshotParamsE(uint8_t* out);
void        ppgRestoreParamsE(const uint8_t* in);
uint8_t     ppgNumVoicesE();
void        ppgVoiceInitE();
void        ppgNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        ppgNoteOffE(uint8_t voiceIdx);
void        ppgAllOffE();
void        ppgSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     ppgMasterParamIdE();
uint8_t     ppgFilterCutoffParamIdE();
uint8_t     ppgFilterResonanceParamIdE();
int         ppgRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     ppgNumPagesE();
const struct EnginePageDef* ppgPageE(uint8_t idx);

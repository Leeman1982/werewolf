#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"

// ============================================================================
//  cs80_voice.h — Yamaha CS-80 signal path, one voice.
//
//  Eight voices, each with TWO complete and independent channels (I and II).
//  Sixteen full signal paths in total; every note sounds both channels.  That
//  pairing is not a layering feature bolted onto a mono-timbral synth, it is
//  the instrument, and it is why a CS-80 sounds the way it does.
//
//  Per channel:
//      VCO (saw core + reset glitch) -> WSC (saw / pulse+PWM / sine) + noise
//        -> resonant HPF (12 dB) -> resonant LPF (12 dB) -> fixed 7.6 kHz pole
//        -> + unfiltered pure sine -> VCA (ADSR)
//  with an independent IL/AL/A/D/R envelope on the filters and an independent
//  PWM LFO.
//
//  Everything is header-inline so it lands inside the block renderer
//  (engine_cs80.cpp's cs80RenderBlockE) rather than requiring a separate call
//  per sample. On the original RP2350 build that renderer also carried a
//  __no_inline_not_in_flash_func attribute to keep it SRAM-resident and off
//  the flash-XIP path — an arduino-pico-specific concern this ESP32-S3 port
//  does not have, so it did not carry over.
// ============================================================================

// ─── Waveforms produced by the wave shape converter ─────────────────────────
#define CSW_SAW   0
#define CSW_PULSE 1
#define CSW_SINE  2

// ─── VCO: sawtooth core with the reset glitch ───────────────────────────────
struct Cs80Osc {
    uint32_t phase, inc;
    float    dt, invDt;
    float    glitchLeft;      // samples of reset pulse still to emit
    uint8_t  wave;
};

static inline void cs80OscInit(Cs80Osc* o) {
    o->phase = 0; o->inc = 0;
    o->dt = 0.01f; o->invDt = 100.0f;
    o->glitchLeft = 0.0f;
    o->wave = CSW_SAW;
}

static inline void cs80OscSetFreq(Cs80Osc* o, float hz) {
    if (hz < 0.5f) hz = 0.5f;
    o->inc = freqToPhaseInc(hz);
    o->dt  = hz * INV_SAMPLE_RATE;
    if (o->dt > 0.45f) o->dt = 0.45f;
    o->invDt = 1.0f / o->dt;
}

static inline float csPolyBlep(float t, float dt, float invDt) {
    if (t < dt)        { float x = t * invDt;          return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { float x = (t - 1.0f) * invDt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

// One sample.  `pw` is the pulse width after this channel's PWM LFO.
//
// The reset glitch is deliberately NOT band-limited.  It is a real
// discontinuity on the real instrument — a ~30 us one-shot fired by the saw
// core's reset comparator — and band-limiting it would remove exactly the
// brightness it exists to produce.  Its width is constant in TIME, so at a
// fixed sample rate it is a constant number of samples wide regardless of
// pitch; that is why it is counted down in samples rather than in phase.
static inline float cs80OscNext(Cs80Osc* o, float pw) {
    const uint32_t ph = o->phase;
    const uint32_t nph = ph + o->inc;
    const bool wrapped = (nph < ph);
    o->phase = nph;

    float out;
    switch (o->wave) {
    case CSW_PULSE: {
        float t = phaseToFloat(ph);
        out = (t < pw) ? 1.0f : -1.0f;
        out += csPolyBlep(t, o->dt, o->invDt);
        float t2 = t - pw; if (t2 < 0.0f) t2 += 1.0f;
        out -= csPolyBlep(t2, o->dt, o->invDt);
        out -= (2.0f * pw - 1.0f);            // DC-correct so PWM does not pump
        break;
    }
    case CSW_SINE:
        // The WSC derives the sine from the core, so it is phase-locked to it
        // — and it is shaped, not switched, so it carries no reset glitch.
        return sineFromPhase(ph);
    default: {
        float t = phaseToFloat(ph);
        out = 2.0f * t - 1.0f - csPolyBlep(t, o->dt, o->invDt);
        break;
    }
    }

    if (wrapped) o->glitchLeft = CS_GLITCH_SAMPLES;
    if (o->glitchLeft > 0.0f) {
        // Partial coverage on the last sample keeps the glitch's energy
        // constant instead of quantising it to whole samples.
        float amt = (o->glitchLeft > 1.0f) ? 1.0f : o->glitchLeft;
        out += CS_GLITCH_AMP * amt;
        o->glitchLeft -= 1.0f;
    }
    return out;
}

// ─── Noise, mixed in at VCO level ───────────────────────────────────────────
struct NoiseGen { uint32_t seed; float p0, p1, p2; };

static inline float noiseWhite(NoiseGen* n) {
    n->seed = n->seed * 1664525u + 1013904223u;
    return (float)((int32_t)n->seed) * 4.6566128752458e-10f;
}
static inline float noisePink(NoiseGen* n) {
    float w = noiseWhite(n);
    n->p0 = 0.99886f * n->p0 + w * 0.0555179f;
    n->p1 = 0.99332f * n->p1 + w * 0.0750759f;
    n->p2 = 0.96900f * n->p2 + w * 0.1538520f;
    return (n->p0 + n->p1 + n->p2 + w * 0.3104856f) * 0.28f;
}

// ─── 12 dB state-variable section with the CS damping law ───────────────────
// Topology-preserving transform, so it stays exact and stable at any cutoff.
// What makes it a CS rather than a generic SVF is the damping:
//
//   * k = 1/Q never reaches zero, so the filter CANNOT self-oscillate.  The
//     real circuit gets its resonance by reducing damping, not by feeding the
//     output back around the loop, and it never quite lets go.
//   * The damping floor RISES with cutoff, so Q falls as the filter opens.
//     This is why a CS-80 stays smooth when a ladder would scream.
struct Cs80Svf {
    float ic1, ic2;
    float k, a1, a2, a3;
};

static inline void cs80SvfInit(Cs80Svf* f) {
    f->ic1 = f->ic2 = 0.0f;
    f->k = CS_K_OPEN; f->a1 = 1.0f; f->a2 = f->a3 = 0.0f;
}

// cutIdx is a log-frequency table index; res is 0..1.  Control rate only.
static inline void cs80SvfSet(Cs80Svf* f, float cutIdx, float res) {
    const float g  = filterG(cutIdx);
    const float cn = cutIdx * (1.0f / (float)FILT_TAB_SIZE);   // 0..1
    const float kFloor = CS_K_FLOOR + CS_K_SLOPE * cn;
    float k = kFloor + (1.0f - res) * (CS_K_OPEN - kFloor);
    if (k < kFloor) k = kFloor;
    f->k  = k;
    f->a1 = 1.0f / (1.0f + g * (g + k));
    f->a2 = g * f->a1;
    f->a3 = g * f->a2;
}

// Both outputs come from one pass; the caller picks which it needs.
static inline void cs80SvfProcess(Cs80Svf* f, float x, float* lp, float* hp) {
    const float v3 = x - f->ic2;
    const float v1 = f->a1 * f->ic1 + f->a2 * v3;
    const float v2 = f->ic2 + f->a2 * f->ic1 + f->a3 * v3;
    f->ic1 = 2.0f * v1 - f->ic1;
    f->ic2 = 2.0f * v2 - f->ic2;
    *lp = v2;
    *hp = x - f->k * v1 - v2;
}

// ─── Fixed single-pole low-pass (~7.6 kHz) on the overall response ──────────
// g = 1 - exp(-2*pi * 7600 / 48000) = 1 - exp(-0.99484) = 0.63024
#define CS_FIXED_LP_G  0.63024f
struct Cs80OnePole { float z; };
static inline float cs80OnePole(Cs80OnePole* p, float x, float g) {
    p->z += g * (x - p->z);
    return p->z;
}

// ─── VCA envelope: plain ADSR ───────────────────────────────────────────────
enum CsAmpStage : uint8_t { CA_IDLE = 0, CA_ATTACK, CA_DECAY, CA_SUSTAIN, CA_RELEASE };

struct Cs80AmpEnv {
    uint8_t stage;
    float   level, atkRate, decCoef, sus, relCoef;
};

static inline void cs80AmpTrigger(Cs80AmpEnv* e, float atk, float dec, float sus, float rel) {
    e->stage = CA_ATTACK;
    e->atkRate = atk; e->decCoef = dec; e->sus = sus; e->relCoef = rel;
}
static inline void cs80AmpRelease(Cs80AmpEnv* e) {
    if (e->stage != CA_IDLE) e->stage = CA_RELEASE;
}
static inline float cs80AmpProcess(Cs80AmpEnv* e) {
    switch (e->stage) {
    case CA_IDLE: return 0.0f;
    case CA_ATTACK:
        e->level += e->atkRate;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = CA_DECAY; }
        break;
    case CA_DECAY:
        e->level += (e->sus - e->level) * e->decCoef;
        if (e->level - e->sus < 0.001f) { e->level = e->sus; e->stage = CA_SUSTAIN; }
        break;
    case CA_SUSTAIN:
        e->level = e->sus;
        break;
    case CA_RELEASE:
        e->level -= e->level * e->relCoef;
        if (e->level < 0.0005f) { e->level = 0.0f; e->stage = CA_IDLE; }
        break;
    }
    return e->level;
}

// ─── VCF envelope: IL / AL / A / D / R — the distinctive one ────────────────
//
//  Initial Level  — where the cutoff offset SITS at key-on, before anything
//                   moves.  Bipolar: it can start below the panel setting.
//  Attack Level   — where the attack ramp ARRIVES.  Also bipolar, and it may
//                   be lower than IL, in which case the "attack" is a downward
//                   lurch.
//  Decay          — falls from AL back toward zero (the panel setting).  When
//                   AL is below zero this stage rises, so decay behaves as a
//                   second, slower attack.  That single behaviour is most of
//                   the CS-80's brass and pluck character.
//  Release        — returns toward IL, not toward zero.
//
//  A conventional ADSR cannot express any of this, which is why it gets its
//  own type rather than being approximated with the amp envelope's.
enum CsFiltStage : uint8_t { CF_IDLE = 0, CF_ATTACK, CF_DECAY, CF_RELEASE };

struct Cs80FiltEnv {
    uint8_t stage;
    float   value;           // bipolar cutoff offset, -1 .. +1
    float   il, al;
    float   atkStep;         // linear per control tick, signed toward al
    float   decCoef, relCoef;
};

static inline void cs80FiltTrigger(Cs80FiltEnv* e, float il, float al,
                                   float atkStep, float dec, float rel) {
    e->stage = CF_ATTACK;
    e->value = il;
    e->il = il; e->al = al;
    e->atkStep = (al >= il) ? atkStep : -atkStep;
    e->decCoef = dec; e->relCoef = rel;
}
static inline void cs80FiltRelease(Cs80FiltEnv* e) {
    if (e->stage != CF_IDLE) e->stage = CF_RELEASE;
}
static inline float cs80FiltProcess(Cs80FiltEnv* e) {
    switch (e->stage) {
    case CF_IDLE: return 0.0f;
    case CF_ATTACK:
        e->value += e->atkStep;
        if ((e->atkStep >= 0.0f && e->value >= e->al) ||
            (e->atkStep <  0.0f && e->value <= e->al)) {
            e->value = e->al;
            e->stage = CF_DECAY;
        }
        break;
    case CF_DECAY:
        e->value -= e->value * e->decCoef;          // toward zero
        break;
    case CF_RELEASE:
        e->value += (e->il - e->value) * e->relCoef;
        break;
    }
    return e->value;
}

// ─── One channel (I or II) of one voice ─────────────────────────────────────
struct Cs80Channel {
    Cs80Osc     osc;
    uint32_t    pwmPhase, pwmInc;    // PWM has its own LFO, per channel
    Cs80Svf     hpf, lpf;
    Cs80OnePole fixedLp;
    Cs80AmpEnv  ampEnv;
    Cs80FiltEnv filtEnv;
};

// ─── One voice: two channels, shared pitch and expression ───────────────────
struct Cs80Voice {
    Cs80Channel ch[CS_CHANNELS];

    int      note;
    uint8_t  velocity;
    float    velNorm;
    float    aftertouch;             // per-note, 0..1
    bool     gate;
    uint32_t startTick;

    float    targetFreq, currentFreq, glideCoef;
    bool     gliding;

    // Velocity/aftertouch/drift trim folded into one factor, recomputed only
    // at control rate (see cs80RenderBlockE) since aftertouch is the only one
    // of its three inputs that can change inside a block, and even that only
    // changes on a control tick — recomputing it every sample was doing the
    // same multiply up to 8x more often than any input to it could move.
    float    mixScale;

    // Per-voice analogue offsets.  Real boards are never matched; a few cents
    // and a few percent of filter offset per voice is the difference between
    // "eight copies of one voice" and "an instrument".
    float    driftCents, driftFilt, driftLevel;
    float    driftPhase;             // slow random walk
};

void cs80VoiceInit(Cs80Voice* v, int index);

// Per-voice fixed offsets, spread deterministically so a patch sounds the same
// every power-up rather than randomly detuned each boot.
extern const float CS_VOICE_DETUNE[CS_VOICES];
extern const float CS_VOICE_FILT[CS_VOICES];
extern const float CS_VOICE_LEVEL[CS_VOICES];

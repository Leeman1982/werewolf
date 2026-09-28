#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
//  dsp_tables.h — every transcendental the audio path used to call per sample,
//  turned into a table lookup.
//
//  All tables are built once at boot into .bss, i.e. SRAM.  That is deliberate
//  and is itself an optimisation: a `const` table would live in flash and every
//  read would go through the XIP cache, which core0 is busy thrashing with the
//  display frame buffer and FatFS.  RAM tables are a fixed ~14 KB and never stall.
//
//  Indexing is by bit-mask/shift wherever the table size is a power of two, so
//  no modulo and no bounds branch on the hot path.
// ============================================================================

// ─── Sine ───────────────────────────────────────────────────────────────────
// 1024 entries + 1 wrap guard, addressed straight from a 32-bit phase
// accumulator: top 10 bits pick the entry, the next 16 are the interpolation
// fraction.  No range reduction, no branches.
#define SINE_BITS   10
#define SINE_SIZE   (1 << SINE_BITS)          // 1024
extern float SINE_TAB[SINE_SIZE + 1];

static inline float sineFromPhase(uint32_t phase) {
    uint32_t i = phase >> (32 - SINE_BITS);            // 0..1023
    float    f = (float)((phase >> 6) & 0xFFFFu) * (1.0f / 65536.0f);
    float    a = SINE_TAB[i];
    return a + (SINE_TAB[i + 1] - a) * f;
}

// ─── tanh (saturation) ──────────────────────────────────────────────────────
// The old polynomial cost a float divide (~14 cycles on M33) and was called up
// to six times per voice per sample.  A 1024-point table over +/-6 with linear
// interpolation is well under 0.1 % error and costs two loads and a multiply.
#define TANH_SIZE   1024
#define TANH_RANGE  6.0f
#define TANH_SCALE  ((float)TANH_SIZE / (2.0f * TANH_RANGE))
extern float TANH_TAB[TANH_SIZE + 1];

static inline float fastTanh(float x) {
    // Written as "is x greater than -RANGE" rather than "x <= -RANGE" so a
    // NaN input (every ordered comparison with NaN is false) falls into
    // this branch instead of reaching the table lookup below: (int)NaN is
    // undefined behavior, and on at least one real toolchain that has been
    // observed to produce an out-of-range index and crash. Every caller in
    // this codebase's own filters keeps state well-behaved by construction,
    // but the newer engines' less-tightly-bounded feedback topologies
    // (variable_filter.h, the self-oscillating ladder) can diverge under
    // extreme cutoff/resonance combinations — this is the backstop for
    // that, not a substitute for keeping those stable.
    if (!(x > -TANH_RANGE)) return -1.0f;
    if (!(x <  TANH_RANGE)) return  1.0f;
    float t = (x + TANH_RANGE) * TANH_SCALE;
    int   i = (int)t;
    float f = t - (float)i;
    // x < TANH_RANGE above only bounds x, not t: (x + TANH_RANGE) * TANH_SCALE
    // can still round up to exactly TANH_SIZE (or, in principle, one past it)
    // in float arithmetic even when x is a hair under TANH_RANGE, which reads
    // TANH_TAB[TANH_SIZE + 1] below — one past the end of a TANH_SIZE+1-sized
    // array. Never hit by this codebase's own filters (their inputs don't
    // land this close to the boundary), but the newer engines' less-tightly-
    // bounded topologies do reach it under stress-test parameters. Clamping i
    // is cheaper than reasoning about every caller's proximity to the edge.
    if (i < 0) i = 0; else if (i > TANH_SIZE - 1) i = TANH_SIZE - 1;
    float a = TANH_TAB[i];
    return a + (TANH_TAB[i + 1] - a) * f;
}

// ─── Filter coefficient table ───────────────────────────────────────────────
// The filter does not take a cutoff in Hz.  It takes a *log-frequency index*
// 0..FILT_TAB_SIZE-1 spanning FILT_HZ_MIN upward, and the prewarped integrator
// gain g = tan(pi*fc/fs) is read straight out of a table.
//
// That kills the expf()/tanf() the original called on every coefficient update,
// and it also makes every *modulation* cheap: an octave of envelope, keytracking
// or LFO is just += FILT_IDX_PER_OCT on an index.  Pitch-domain modulation with
// no pow() anywhere on the audio path.
#define FILT_TAB_SIZE   1024
#define FILT_HZ_MIN     20.0f
#define FILT_OCTAVES    10.0f                 // 20 Hz .. 20480 Hz
#define FILT_IDX_PER_OCT ((float)FILT_TAB_SIZE / FILT_OCTAVES)   // 102.4
extern float FILT_G[FILT_TAB_SIZE];           // tan(pi*fc/fs), TPT integrator gain
extern float FILT_HZ[FILT_TAB_SIZE];          // the same index, in Hz (display)

static inline float filterG(float idx) {
    int i = (int)idx;
    if (i < 0) i = 0; else if (i >= FILT_TAB_SIZE) i = FILT_TAB_SIZE - 1;
    return FILT_G[i];
}
static inline float filterHzFromIdx(float idx) {
    int i = (int)idx;
    if (i < 0) i = 0; else if (i >= FILT_TAB_SIZE) i = FILT_TAB_SIZE - 1;
    return FILT_HZ[i];
}

// ─── MIDI note -> frequency ─────────────────────────────────────────────────
extern float NOTE_FREQ[128];
static inline float noteToFreq(int note) {
    return NOTE_FREQ[(note < 0) ? 0 : (note > 127 ? 127 : note)];
}

// ─── 1/sqrt(n) voice-count normalisation ────────────────────────────────────
extern float INV_SQRT_N[MAX_VOICES + 2];

// ─── 2^x ────────────────────────────────────────────────────────────────────
// Still needed for detune ratios and pitch bend, but only at control rate now.
// Exponent injected straight into the IEEE-754 bit field; the mantissa comes
// from the ln(2)^k/k! series to sixth order.  A cubic is ~0.5% off, which is
// nine cents of pitch error — plainly audible on a detune or a bend, and not
// worth saving three multiplies for at control rate.
static inline float fastPow2(float x) {
    if (x < -8.0f) return 0.00390625f;
    if (x >  8.0f) return 256.0f;
    int i = (int)x;
    if (x < 0.0f) i--;
    float f = x - (float)i;
    float p = 1.0f + f * (0.69314718f + f * (0.24022651f + f * (0.05550411f +
              f * (0.00961813f + f * (0.00133335f + f * 0.00015403f)))));
    union { float f; uint32_t u; } v;
    v.f = p;
    v.u += (uint32_t)i << 23;
    return v.f;
}

// Fixed-point phase increment for a frequency in Hz (2^32 / SAMPLE_RATE).
#define PHASE_PER_HZ  (4294967296.0f / SAMPLE_RATE_F)
static inline uint32_t freqToPhaseInc(float freq) {
    return (uint32_t)(freq * PHASE_PER_HZ);
}
static inline float phaseToFloat(uint32_t phase) {
    return (float)phase * 2.328306437e-10f;   // 1 / 2^32
}

// ─── NaN / range guards ─────────────────────────────────────────────────────
// A denormal or NaN reaching the I2S buffer is an audible click at best and a
// stuck DC offset at worst; the filter feedback path is the usual source.
static inline float safeFloat(float x) {
    return (isnan(x) || isinf(x)) ? 0.0f : x;
}
static inline float safeClamp(float x, float lo, float hi) {
    if (isnan(x) || isinf(x)) return 0.0f;
    return (x < lo) ? lo : (x > hi ? hi : x);
}

// ─── Generic PolyBLEP corner ────────────────────────────────────────────────
// Shared by the newer engines' oscillators (Minimoog/Prophet-5). CS-80's own
// csPolyBlep in cs80_voice.h is untouched and kept separate on purpose —
// this is the same classic two-piece correction, just not CS-80-specific, so
// new engines don't each redeclare it.
static inline float polyBlep(float t, float dt, float invDt) {
    if (t < dt)        { float x = t * invDt;          return x + x - x * x - 1.0f; }
    if (t > 1.0f - dt) { float x = (t - 1.0f) * invDt; return x * x + x + x + 1.0f; }
    return 0.0f;
}

void dspTablesInit();

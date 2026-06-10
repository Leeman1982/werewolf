// Band-limited wavetable oscillator implementation for ESP32.
// Replaces polyBLEP with additive-synthesis wavetables that pre-limit harmonics to
// below Nyquist for each frequency region, eliminating aliasing with ~40% less CPU.
//
// 8 tables per waveform, each built with 2× more harmonics than the last:
//   table 0 → 2 harmonics  (alias-free up to 11 025 Hz)
//   ...
//   table 7 → 256 harmonics (alias-free up to    86 Hz)
// At runtime the oscillator picks the richest table that stays below Nyquist.
// _cachedTableIdx avoids re-scanning topFreqs every sample for sustained notes.

#include "synth_engine.h"
#include <math.h>

// ── Static member storage ─────────────────────────────────────────────────────
float BLWavetables::tables[3][BL_NUM_TABLES][BL_TABLE_SIZE];
float BLWavetables::topFreqs[BL_NUM_TABLES];
bool  BLWavetables::initialized = false;

// Harmonic counts for each table slot, ordered fewest → most.
// topFreqs[i] = sampleRate / (2 * harmonicCounts[i])
static const int kHarmonics[BL_NUM_TABLES] = { 2, 4, 8, 16, 32, 64, 128, 256 };

// ── Table builder ─────────────────────────────────────────────────────────────
void BLWavetables::buildTable(int waveIdx, int tableIdx, int numHarmonics) {
    float* tbl = tables[waveIdx][tableIdx];

    for (int i = 0; i < BL_TABLE_SIZE; i++) tbl[i] = 0.0f;

    const float TWO_PI_F = 6.28318530718f;

    switch (waveIdx) {
        case 0: {   // SAW  f(t) = (2/π) Σ (-1)^(n+1)/n · sin(2πnt)
            for (int n = 1; n <= numHarmonics; n++) {
                float amp       = ((n & 1) ? 1.0f : -1.0f) * (2.0f / (float)(PI * n));
                float phaseInc  = TWO_PI_F * n / BL_TABLE_SIZE;
                float ph        = 0.0f;
                for (int i = 0; i < BL_TABLE_SIZE; i++) {
                    tbl[i] += amp * sinf(ph);
                    ph      += phaseInc;
                }
            }
            break;
        }
        case 1: {   // SQUARE  f(t) = (4/π) Σ_{odd n} sin(2πnt)/n
            for (int n = 1; n <= numHarmonics; n += 2) {
                float amp       = 4.0f / (float)(PI * n);
                float phaseInc  = TWO_PI_F * n / BL_TABLE_SIZE;
                float ph        = 0.0f;
                for (int i = 0; i < BL_TABLE_SIZE; i++) {
                    tbl[i] += amp * sinf(ph);
                    ph      += phaseInc;
                }
            }
            break;
        }
        case 2: {   // TRIANGLE  f(t) = (8/π²) Σ_{odd n} (-1)^((n-1)/2) sin(2πnt)/n²
            int k = 0;
            for (int n = 1; n <= numHarmonics; n += 2) {
                float sign      = (k & 1) ? -1.0f : 1.0f;
                float amp       = sign * 8.0f / (float)(PI * PI * n * n);
                float phaseInc  = TWO_PI_F * n / BL_TABLE_SIZE;
                float ph        = 0.0f;
                for (int i = 0; i < BL_TABLE_SIZE; i++) {
                    tbl[i] += amp * sinf(ph);
                    ph      += phaseInc;
                }
                k++;
            }
            break;
        }
    }
}

// ── One-time initialisation ───────────────────────────────────────────────────
void BLWavetables::init(float sampleRate) {
    if (initialized) return;

    Serial.println("Initializing band-limited wavetables...");

    for (int i = 0; i < BL_NUM_TABLES; i++) {
        topFreqs[i] = sampleRate / (2.0f * kHarmonics[i]);
        buildTable(0, i, kHarmonics[i]);   // SAW
        buildTable(1, i, kHarmonics[i]);   // SQUARE
        buildTable(2, i, kHarmonics[i]);   // TRIANGLE
    }

    initialized = true;
    Serial.println("Wavetables ready!");
}

// ── Per-sample oscillator output ──────────────────────────────────────────────
// IRAM_ATTR: placed in instruction SRAM to avoid I-cache misses under flash
// contention when the audio task on Core 0 runs in a tight loop.
//
// Q32 fixed-point phase: phaseQ wraps modulo 2^32 for free.
// Top 10 bits → 1024-entry table index; lower 22 bits → fractional part.
//   idx  = phaseQ >> 22                          (range 0..1023)
//   frac = (phaseQ & 0x3FFFFF) * (1 / 2^22)      (range [0,1))
// Eliminates the per-sample float compare + subtract that wrapped the old
// float phase, and replaces floorf() with a single shift+mask.
IRAM_ATTR float BLOscillator::process() {
    const float kInv2p22 = 1.0f / 4194304.0f;     // 1 / 2^22
    const float kInv2p32 = 1.0f / 4294967296.0f;  // 1 / 2^32
    float sample = 0.0f;

    switch (waveform) {
        case WAVE_SAW:
        case WAVE_SQUARE:
        case WAVE_TRIANGLE:
        case WAVE_SMOOTHSQ: {
            // Resolve table tier on frequency change.  topFreqs is DESCENDING
            // (tier 0 = highest top-freq, tier 7 = lowest), so a 3-level
            // unrolled binary search picks the right tier in 3 compares
            // instead of up to 8 linear-scan compares.
            if (_tableCacheDirty) {
                const float* tf = BLWavetables::topFreqs;
                int idx;
                if (frequency <= tf[4]) {
                    if (frequency <= tf[6]) idx = (frequency <= tf[7]) ? 7 : 6;
                    else                    idx = (frequency <= tf[5]) ? 5 : 4;
                } else {
                    if (frequency <= tf[2]) idx = (frequency <= tf[3]) ? 3 : 2;
                    else                    idx = (frequency <= tf[1]) ? 1 : 0;
                }
                _cachedTableIdx  = idx;
                _tableCacheDirty = false;
            }

            // Index + fraction from Q32 phase via shift + mask (no floorf).
            const uint32_t TABLE_MASK = BL_TABLE_SIZE - 1;
            uint32_t i0 = (phaseQ >> 22) & TABLE_MASK;
            uint32_t i1 = (i0 + 1) & TABLE_MASK;
            float    frac = (float)(phaseQ & 0x3FFFFFu) * kInv2p22;

            // SMOOTHSQ reuses the SQUARE tables two tiers down (fewer
            // harmonics = rounded edges) — zero extra table memory.
            int wsel = (waveform == WAVE_SMOOTHSQ) ? (int)WAVE_SQUARE : (int)waveform;
            int tier = _cachedTableIdx;
            if (waveform == WAVE_SMOOTHSQ) tier = (tier + 2 > 7) ? 7 : tier + 2;

            const float* tbl = BLWavetables::tables[wsel][tier];
            sample = tbl[i0] + frac * (tbl[i1] - tbl[i0]);
            break;
        }

        case WAVE_SINE:
        case WAVE_COS: {
            // COS = sine with a +90° (0.25 cycle) phase offset.  Using a small
            // float offset (not a large 0x40000000 int literal) keeps this IRAM
            // function's literal pool small — avoids the l32r relocation error.
            float phaseF = (float)phaseQ * kInv2p32;
            if (waveform == WAVE_COS) phaseF += 0.25f;
            sample = sinf(TWO_PI * phaseF);
            break;
        }

        case WAVE_PULSE: {
            // Variable pulse width can't use a fixed wavetable; keep polyBLEP here.
            float phaseF = (float)phaseQ * kInv2p32;
            float dt     = (float)phaseIncQ * kInv2p32;
            sample  = (phaseF < pulseWidth) ? 1.0f : -1.0f;
            sample += polyBlep(phaseF, dt);
            float wrap = phaseF + (1.0f - pulseWidth);
            if (wrap >= 1.0f) wrap -= 1.0f;
            sample -= polyBlep(wrap, dt);
            break;
        }
    }

    // Phase advance wraps modulo 2^32 automatically — no branch.
    phaseQ += phaseIncQ;

    return sample;
}

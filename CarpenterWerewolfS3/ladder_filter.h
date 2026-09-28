#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"

// ============================================================================
//  ladder_filter.h — the self-oscillating 4-pole lowpass shared by every
//  engine here whose real instrument used a transistor-ladder-family VCF
//  (Minimoog's discrete ladder, Prophet-5's SSI2140/Curtis 4-pole). Both
//  genuinely self-oscillate at high resonance — the opposite of the CS-80's
//  filter elsewhere in this codebase, which by design cannot.
//
//  Four one-pole stages, each a TPT (trapezoidal/zero-delay-style) integrator
//  — the same prewarped-tan topology as the CS-80 and ARP 2600/Odyssey SVFs
//  elsewhere in this codebase, just one pole instead of two per section —
//  shaped by tanh, with stage 4 fed back to the input (from the PREVIOUS
//  sample, not solved zero-delay) scaled by `k`.
//
//  The choice of a true TPT integrator per stage, rather than a simpler EMA
//  one-pole, is not cosmetic: an EMA one-pole's phase lag asymptotes toward
//  90 degrees only as frequency approaches infinity, so four of them in
//  cascade can never accumulate the ~360 degrees of loop phase a real
//  self-oscillating ladder needs at any finite, audible cutoff — measured on
//  the bench here, that cascade simply would not sing. The TPT form's phase
//  reaches the needed range within the audible band, which is what makes
//  self-oscillation possible at musical cutoffs at all. Self-oscillation
//  onset is around k ~= 4.0 for this topology (measured, not assumed — see
//  the test suite); the tanh nonlinearity is what keeps the oscillation's
//  amplitude bounded once the loop gain crosses unity instead of blowing up.
// ============================================================================
struct LadderFilter {
    float g = 0.1f, G = 0.09f, k = 0.0f;
    float z1 = 0, z2 = 0, z3 = 0, z4 = 0;

    inline void setCutoffHz(float fcHz, float resonance01) {
        if (fcHz < 20.0f) fcHz = 20.0f; else if (fcHz > 18000.0f) fcHz = 18000.0f;
        g = tanf((float)M_PI * fcHz * INV_SAMPLE_RATE);
        G = g / (1.0f + g);
        k = resonance01 * 4.2f;
    }

    // One TPT one-pole lowpass stage: v is the (half-step) integrator input,
    // y the output, z the integrator's stored state for next sample.
    static inline float stage(float x, float& z, float G) {
        float v = (x - z) * G;
        float y = v + z;
        z = y + v;
        return y;
    }

    inline float process(float x) {
        float in1 = fastTanh(x - k * z4);
        float y1 = stage(in1, z1, G);
        float in2 = fastTanh(y1);
        float y2 = stage(in2, z2, G);
        float in3 = fastTanh(y2);
        float y3 = stage(in3, z3, G);
        float in4 = fastTanh(y3);
        float y4 = stage(in4, z4, G);
        return y4;
    }
};

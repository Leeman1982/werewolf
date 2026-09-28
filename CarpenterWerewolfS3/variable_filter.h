#pragma once
#include <math.h>
#include "config.h"
#include "dsp_tables.h"

// ============================================================================
//  variable_filter.h — 15 selectable "filter flavour" models, on top of
//  whichever real filter an engine already models.
//
//  Ported from a reference Python implementation (variable_synth_filter.py,
//  "Awesome Musical Variable Filter") supplied as a 15-mode filter pack.
//  Two things changed from that reference, both correctness issues rather
//  than style:
//
//  1. The reference resets its filter state on every call — fine for a
//     one-shot offline render, wrong for a real-time voice, where state has
//     to persist sample-to-sample across render blocks or every block
//     boundary would click. VarFilter carries its state in `s[]` instead,
//     exactly like this codebase's own LadderFilter/A26Svf.
//  2. Every mode is a naive one-pole/state-variable cascade with a single-
//     sample-delayed feedback path — the same design LadderFilter itself
//     used before being rewritten as a TPT integrator (see ladder_filter.h)
//     specifically because that naive form goes numerically unstable well
//     within the reference's own claimed cutoff range. A probe here found
//     several modes diverging to NaN as low as ~4-6 kHz at high resonance
//     (FORMANT as low as ~3.8 kHz — three resonant peaks compound loop
//     gain). setCutoff()'s per-mode ceilings below were measured the same
//     way LadderFilter's TPT fix was verified: sweep cutoff at maximum
//     resonance until state diverges, then take a safety margin under that.
//     process() also carries a defensive state reset as a backstop for any
//     modulation path that measurement didn't cover — see its comment.
//
//  Coefficients are computed at control rate and cached (setCutoff), same
//  split as LadderFilter's setCutoffHz()/process() — recomputing sinf() at
//  audio rate across up to 10 voices (Prophet-10) x up to 3 stages
//  (FORMANT) would be real CPU, not a rounding error.
//
//  This is a FLAVOUR selector, not a replacement for any engine's modelled
//  filter: mode 0 (STOCK) means "use the engine's own real-hardware filter,
//  unchanged" — see FILT TYPE on each engine's FILTER page. Modes 1-15 route
//  the same cutoff/resonance controls that already exist through one of
//  these instead, so nothing new has to be added to the panel to reach them
//  ("automatically goes to one of the analogue pots" — turning CUTOFF/
//  RESONANCE still does the same thing, whatever FILT TYPE you're on).
// ============================================================================

enum VarFilterMode : uint8_t {
    VF_STOCK = 0,
    VF_MOOG_LADDER,     // 1  24dB ladder, feedback-saturated
    VF_MS20,            // 2  Korg MS-20 style, aggressive 12dB-ish + bite
    VF_SEM_SVF,         // 3  Oberheim SEM state-variable, lowpass out
    VF_TB303,           // 4  TB-303 style, squelchy, mildly asymmetric
    VF_PROPHET,         // 5  Prophet-5 / SSM2040 character
    VF_JUNO,            // 6  Juno / IR3109, smooth and polite
    VF_ARP,             // 7  ARP 2600 / 4072, aggressive but controlled
    VF_STEINER,         // 8  Steiner-Parker
    VF_SALLEN_KEY,      // 9  Sallen-Key 12dB, clean and simple
    VF_CEM,             // 10 CEM3320, used in many classic polys
    VF_CS80_INSPIRED,   // 11 warm, slightly darker, elegant tilt
    VF_MS20_DUAL,       // 12 MS-20-style series highpass -> lowpass
    VF_FORMANT,         // 13 dual/triple-peak formant character
    VF_DIODE_LADDER,    // 14 darker diode-ladder, stronger low-end saturation
    VF_SVF_MORPH,       // 15 SEM SVF, RESONANCE control morphs LP->BP->HP
    VF_NUM_MODES
};

static const char* const VAR_FILTER_NAMES[VF_NUM_MODES] = {
    "STOCK", "MOOG LADDER", "MS-20 AGGR", "SEM/OB SVF", "TB-303",
    "PROPHET/SSM", "JUNO IR3109", "ARP 4072", "STEINER-PARK", "SALLEN-KEY",
    "CEM3320", "CS-80 INSP", "MS-20 DUAL", "FORMANT", "DIODE LADDER",
    "SVF MORPH",
};

// Per-mode cutoff ceiling in Hz — see the file header for how these were
// measured. Index by VarFilterMode; VF_STOCK's entry is unused (that mode
// never reaches this filter at all).
static const float VF_MAX_HZ[VF_NUM_MODES] = {
    0.0f,       // STOCK (unused)
    9000.0f,    // MOOG_LADDER
    11000.0f,   // MS20
    4000.0f,    // SEM_SVF
    18000.0f,   // TB303
    8000.0f,    // PROPHET
    5000.0f,    // JUNO
    9000.0f,    // ARP
    11000.0f,   // STEINER
    4000.0f,    // SALLEN_KEY
    8000.0f,    // CEM
    5500.0f,    // CS80_INSPIRED
    11000.0f,   // MS20_DUAL (outer cutHz; internal stages scale off this)
    2500.0f,    // FORMANT (outer cutHz; internal stages scale off this)
    18000.0f,   // DIODE_LADDER
    6500.0f,    // SVF_MORPH
};

struct VarFilter {
    // Generic scratch state, mapped differently per mode — never more than
    // 6 floats are needed (FORMANT: three independent SEM SVF low/band
    // pairs run in parallel). Cheap enough (24 bytes) that giving every
    // mode its own named fields would only cost readability, not RAM.
    float s[6] = { 0, 0, 0, 0, 0, 0 };

    // Cached at control rate by setCutoff(): up to 3 integrator
    // coefficients (FORMANT needs all three; MS20_DUAL needs two; every
    // other mode uses only coef[0]) plus the resonance value already
    // rescaled the way each mode's math expects.
    float coef[3] = { 0, 0, 0 };
    float resC = 0.0f;

    void reset() {
        for (int i = 0; i < 6; i++) s[i] = 0.0f;
        coef[0] = coef[1] = coef[2] = 0.0f;
        resC = 0.0f;
    }

    static inline float soft(float x, float drive) { return fastTanh(x * drive); }

    static inline float coefFromHz(float hz, float mult, float loHz, float hiHz) {
        if (hz < loHz) hz = loHz; else if (hz > hiHz) hz = hiHz;
        return mult * sinf((float)M_PI * hz * INV_SAMPLE_RATE);
    }

    // Control-rate: recompute the integrator coefficient(s) for `mode` from
    // cutoffHz, and cache the resonance in the form that mode's process
    // step expects. cutoffHz is whatever the engine already computed for
    // its own stock filter (same envelope/keyboard-tracking/LFO modulation
    // already applied) — this only adds the per-mode stability ceiling
    // on top, via VF_MAX_HZ.
    void setCutoff(float cutoffHz, float resonance01, uint8_t mode) {
        float res = resonance01;
        if (res < 0.0f) res = 0.0f; else if (res > 1.0f) res = 1.0f;
        resC = res * 1.4f;

        float hz = cutoffHz;
        if (mode > VF_STOCK && mode < VF_NUM_MODES) {
            float maxHz = VF_MAX_HZ[mode];
            if (hz > maxHz) hz = maxHz;
        }

        switch (mode) {
        case VF_MOOG_LADDER: coef[0] = coefFromHz(hz, 2.0f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_MS20:         coef[0] = coefFromHz(hz, 2.0f, 40.0f, VF_MAX_HZ[mode]); break;
        case VF_SEM_SVF:      coef[0] = coefFromHz(hz, 2.0f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_TB303:        coef[0] = coefFromHz(hz, 1.8f, 40.0f, VF_MAX_HZ[mode]); break;
        case VF_PROPHET:      coef[0] = coefFromHz(hz, 2.0f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_JUNO:         coef[0] = coefFromHz(hz, 1.7f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_ARP:          coef[0] = coefFromHz(hz, 2.1f, 40.0f, VF_MAX_HZ[mode]); break;
        case VF_STEINER:      coef[0] = coefFromHz(hz, 2.0f, 40.0f, VF_MAX_HZ[mode]); break;
        case VF_SALLEN_KEY:   coef[0] = coefFromHz(hz, 2.0f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_CEM:          coef[0] = coefFromHz(hz, 1.9f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_CS80_INSPIRED:coef[0] = coefFromHz(hz, 1.6f, 25.0f, VF_MAX_HZ[mode]); break;
        case VF_MS20_DUAL:
            coef[0] = coefFromHz(hz * 0.4f, 2.0f, 30.0f, SAMPLE_RATE_F * 0.4f);   // HP stage
            coef[1] = coefFromHz(hz,        2.0f, 40.0f, SAMPLE_RATE_F * 0.4f);   // LP stage
            break;
        case VF_FORMANT:
            coef[0] = coefFromHz(hz * 0.7f, 2.0f, 30.0f, SAMPLE_RATE_F * 0.42f);  // peak 1
            coef[1] = coefFromHz(hz * 1.7f, 2.0f, 30.0f, SAMPLE_RATE_F * 0.42f);  // peak 2
            coef[2] = coefFromHz(hz * 0.5f, 2.0f, 30.0f, SAMPLE_RATE_F * 0.42f);  // body
            break;
        case VF_DIODE_LADDER: coef[0] = coefFromHz(hz, 1.8f, 30.0f, VF_MAX_HZ[mode]); break;
        case VF_SVF_MORPH:    coef[0] = coefFromHz(hz, 2.0f, 30.0f, VF_MAX_HZ[mode]); break;
        default: break;
        }
    }

    // One SEM-style state-variable step. `low`/`band` are the two pieces of
    // persistent state (borrowed from `s[]` by the caller) so this can also
    // drive FORMANT's three independent instances.
    static inline float semSvfLp(float x, float& low, float& band, float f, float res) {
        float high = x - low - res * 1.6f * band;
        band += f * high;
        low  += f * band;
        return low;
    }
    static inline float semSvfBp(float x, float& low, float& band, float f, float res) {
        float high = x - low - res * 1.6f * band;
        band += f * high;
        low  += f * band;
        return band;
    }

    float moogLadder(float x) {
        float f = coef[0];
        float fb = resC * 4.2f * soft(s[3], 1.3f);
        float inp = x - fb;
        s[0] += f * (soft(inp, 1.1f) - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        s[3] += f * (s[2] - s[3]);
        return s[3];
    }
    float ms20(float x) {
        float f = coef[0];
        float fb = resC * 2.8f * soft(s[1], 1.8f);
        float inp = x - fb;
        s[0] += f * (soft(inp * 1.4f, 1.2f) - s[0]);
        s[1] += f * (s[0] - s[1]);
        return s[1];
    }
    float semSvf(float x) { return semSvfLp(x, s[0], s[1], coef[0], resC); }
    float tb303(float x) {
        float f = coef[0];
        float fb = resC * 3.5f * fastTanh(s[1] * 1.6f);
        float inp = x - fb;
        s[0] += f * (fastTanh(inp * 1.3f) - s[0]);
        s[1] += f * (s[0] - s[1]);
        return s[1] * 1.1f;
    }
    float prophet(float x) {
        float f = coef[0];
        float fb = resC * 2.9f * soft(s[2], 1.2f);
        float inp = x - fb;
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        return s[2];
    }
    float juno(float x) {
        float f = coef[0];
        float fb = resC * 2.2f * s[2];
        float inp = x - fb;
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        return s[2];
    }
    float arp(float x) {
        float f = coef[0];
        float fb = resC * 3.1f * soft(s[2], 1.4f);
        float inp = x - fb;
        s[0] += f * (soft(inp, 1.15f) - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        return s[2];
    }
    float steiner(float x) {
        float f = coef[0];
        float fb = resC * 2.6f * (s[1] + 0.3f * s[0]);
        float inp = x - soft(fb, 1.3f);
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        return s[1];
    }
    float sallenKey(float x) {
        float f = coef[0];
        float fb = resC * 1.9f * s[1];
        float inp = x - fb;
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        return s[1];
    }
    float cem(float x) {
        float f = coef[0];
        float fb = resC * 3.4f * soft(s[3], 1.25f);
        float inp = x - fb;
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        s[3] += f * (s[2] - s[3]);
        return s[3];
    }
    float cs80Inspired(float x) {
        float f = coef[0];
        float fb = resC * 2.4f * s[2];
        float inp = x - fb;
        s[0] += f * (inp - s[0]);
        s[1] += f * (s[0] - s[1]);
        s[2] += f * (s[1] - s[2]);
        return s[2] * 0.92f + s[1] * 0.08f;
    }
    // Series highpass (s[0],s[1]) into the ms20 lowpass (s[2],s[3]).
    float ms20Dual(float x) {
        float fHp = coef[0];
        float resHp = resC * 0.7f;
        float fbHp = resHp * 2.2f * s[1];
        float inpHp = x - fbHp;
        s[0] += fHp * (inpHp - s[0]);
        s[1] += fHp * (s[0] - s[1]);
        float hp = inpHp - s[1];

        float fLp = coef[1];
        float fbLp = resC * 2.8f * soft(s[3], 1.8f);
        float inpLp = hp - fbLp;
        s[2] += fLp * (soft(inpLp * 1.4f, 1.2f) - s[2]);
        s[3] += fLp * (s[2] - s[3]);
        return s[3];
    }
    // Three independent SEM SVF instances in parallel: body (wide, low Q),
    // and two resonant peaks — s[0]/s[1] = peak1, s[2]/s[3] = peak2,
    // s[4]/s[5] = body. Has the lowest stable cutoff ceiling of all fifteen
    // (three resonant peaks compound loop gain), which is also why it's a
    // formant/vocal character rather than a bright sweepable filter.
    float formant(float x) {
        float p1 = semSvfBp(x, s[0], s[1], coef[0], resC * 1.1f);
        float p2 = semSvfBp(x, s[2], s[3], coef[1], resC * 0.9f);
        float body = semSvfLp(x, s[4], s[5], coef[2], 0.3f);
        return 0.45f * body + 0.35f * p1 + 0.30f * p2;
    }
    float diodeLadder(float x) {
        float f = coef[0];
        float fb = resC * 3.8f * soft(s[3], 1.6f);
        float inp = soft(x - fb, 1.3f);
        float prev = inp;
        for (int i = 0; i < 4; i++) {
            s[i] += f * (prev - s[i]);
            s[i] = soft(s[i], 1.05f);
            prev = s[i];
        }
        return s[3];
    }
    // RESONANCE here isn't resonance — it's the LP->BP->HP morph position,
    // same trade the reference makes for this one mode (it's what makes
    // this mode a distinct flavour rather than another SEM SVF clone). The
    // raw 0..1 resonance knob value is passed straight through as `morph01`
    // (see process()) rather than through the *1.4-scaled resC.
    float svfMorph(float x, float morph01) {
        float f = coef[0];
        const float res = 0.7f;
        float high = x - s[0] - res * 1.6f * s[1];
        s[1] += f * high;
        s[0] += f * s[1];
        float low = s[0], band = s[1];
        float m = morph01; if (m < 0.0f) m = 0.0f; else if (m > 1.0f) m = 1.0f;
        if (m < 0.5f) return low * (1.0f - m * 2.0f) + band * (m * 2.0f);
        return band * (1.0f - (m - 0.5f) * 2.0f) + high * ((m - 0.5f) * 2.0f);
    }

    // Audio-rate. Call setCutoff() at control rate first (matching every
    // other filter in this codebase — LadderFilter/A26Svf both split their
    // coefficient update out from the per-sample process() the same way).
    float process(float x, uint8_t mode, float rawResonance01 = 0.0f) {
        x = fastTanh(x);   // pre-drive, matching the reference's input stage

        float y;
        switch (mode) {
        case VF_MOOG_LADDER:   y = moogLadder(x);              break;
        case VF_MS20:          y = ms20(x);                    break;
        case VF_SEM_SVF:       y = semSvf(x);                  break;
        case VF_TB303:         y = tb303(x);                   break;
        case VF_PROPHET:       y = prophet(x);                 break;
        case VF_JUNO:           y = juno(x);                   break;
        case VF_ARP:            y = arp(x);                    break;
        case VF_STEINER:        y = steiner(x);                break;
        case VF_SALLEN_KEY:     y = sallenKey(x);              break;
        case VF_CEM:             y = cem(x);                   break;
        case VF_CS80_INSPIRED:   y = cs80Inspired(x);          break;
        case VF_MS20_DUAL:       y = ms20Dual(x);               break;
        case VF_FORMANT:         y = formant(x);                break;
        case VF_DIODE_LADDER:    y = diodeLadder(x);           break;
        case VF_SVF_MORPH:       y = svfMorph(x, rawResonance01); break;
        default:                 y = x;                        break;
        }

        // These are naive one-pole/state-variable cascades, not the TPT-
        // integrator designs the rest of this codebase's filters use — see
        // the file header. setCutoff()'s per-mode ceilings keep every mode
        // inside its verified-stable range under static and modulated
        // cutoff alike; this is the backstop for anything that measurement
        // didn't cover. A silent one-sample reset is inaudible; a filter
        // stuck outputting a fixed +-1 (fastTanh's own NaN guard, which it
        // can never recover from on its own since every future sample folds
        // that NaN back in) is not.
        for (int i = 0; i < 6; i++)
            if (!(s[i] > -64.0f) || !(s[i] < 64.0f)) s[i] = 0.0f;

        return fastTanh(y * 0.9f) * 0.95f;   // matches the reference's output stage
    }
};

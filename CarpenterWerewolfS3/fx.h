#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"

// ============================================================================
//  fx.h — the master effect chain: bitcrusher -> chorus -> delay -> reverb.
//
//  Every ring buffer here is a power of two so the read pointer is a bit-mask
//  (`& MASK`) instead of a modulo or a wrap branch — one AND on the sample path
//  where the original had a compare-and-subtract per tap.
//
//  All state lives in .bss (SRAM) and every method is inline so it lands inside
//  the SRAM-resident block renderer.
// ============================================================================

// ─── Bit / sample-rate crusher ──────────────────────────────────────────────
struct Crusher {
    uint32_t counter = 0;
    float    heldL = 0.0f, heldR = 0.0f;

    inline void process(float& l, float& r, uint32_t hold, uint8_t shift) {
        if (hold == 0) return;                       // bypassed
        if (++counter >= hold) {
            counter = 0;
            if (shift) {
                int32_t mask = ~((1 << shift) - 1);
                heldL = (float)(((int32_t)(l * 32768.0f)) & mask) * (1.0f / 32768.0f);
                heldR = (float)(((int32_t)(r * 32768.0f)) & mask) * (1.0f / 32768.0f);
            } else {
                heldL = l; heldR = r;
            }
        }
        l = heldL; r = heldR;
    }
};

// ─── Stereo chorus / flanger ────────────────────────────────────────────────
struct Chorus {
    int16_t  bufL[CHORUS_BUF_LEN];
    int16_t  bufR[CHORUS_BUF_LEN];
    uint32_t wpos = 0;
    uint32_t lfoPhase = 0, lfoInc = 0;

    void begin() {
        for (uint32_t i = 0; i < CHORUS_BUF_LEN; i++) { bufL[i] = 0; bufR[i] = 0; }
        wpos = 0; lfoPhase = 0; lfoInc = 0;
    }
    inline void setRate(float hz) { lfoInc = (uint32_t)(hz * PHASE_PER_HZ); }

    // Fractional tap with linear interpolation; `delaySamp` is in samples.
    static inline float tap(const int16_t* buf, uint32_t wpos, float delaySamp) {
        uint32_t d  = (uint32_t)delaySamp;
        float    fr = delaySamp - (float)d;
        uint32_t i0 = (wpos - d)     & CHORUS_BUF_MASK;
        uint32_t i1 = (wpos - d - 1) & CHORUS_BUF_MASK;
        float a = (float)buf[i0], b = (float)buf[i1];
        return (a + (b - a) * fr) * (1.0f / 32768.0f);
    }

    inline void process(float& l, float& r, float depth, float mix) {
        bufL[wpos] = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32767.0f);
        bufR[wpos] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32767.0f);

        lfoPhase += lfoInc;
        // The two channels sit a quarter cycle apart, which is what makes a
        // chorus wide instead of just detuned.
        float mL = sineFromPhase(lfoPhase);
        float mR = sineFromPhase(lfoPhase + 0x40000000u);

        const float base  = 12.0f * (SAMPLE_RATE_F / 48000.0f);   // ~12 samples
        const float sweep = depth * 340.0f;                        // up to ~7 ms
        float dL = base + sweep * (0.5f + 0.5f * mL);
        float dR = base + sweep * (0.5f + 0.5f * mR);

        float wetL = tap(bufL, wpos, dL);
        float wetR = tap(bufR, wpos, dR);
        wpos = (wpos + 1) & CHORUS_BUF_MASK;

        l += (wetL - l) * mix;
        r += (wetR - r) * mix;
    }
};

// ─── Stereo delay with tempo sync and ping-pong ─────────────────────────────
struct StereoDelay {
    int16_t  bufL[DELAY_BUF_LEN];
    int16_t  bufR[DELAY_BUF_LEN];
    uint32_t wpos = 0;
    float    lenL = 12000.0f, lenR = 12000.0f;   // current (slewed) tap distance
    float    tgtL = 12000.0f, tgtR = 12000.0f;   // requested tap distance

    void begin() {
        for (uint32_t i = 0; i < DELAY_BUF_LEN; i++) { bufL[i] = 0; bufR[i] = 0; }
        wpos = 0;
    }

    // Target time in ms.  The tap is slewed toward it rather than jumped, so a
    // knob sweep pitch-shifts the tail like a tape delay instead of clicking.
    inline void setTimeMs(float ms, bool pingPong) {
        float s = ms * (SAMPLE_RATE_F / 1000.0f);
        if (s < 8.0f) s = 8.0f;
        if (s > (float)(DELAY_BUF_LEN - 4)) s = (float)(DELAY_BUF_LEN - 4);
        tgtL = s;
        // A touch of offset on the right tap widens a plain stereo delay; in
        // ping-pong the two taps must match or the bounce loses its grid.
        tgtR = pingPong ? s : s * 1.18f;
        if (tgtR > (float)(DELAY_BUF_LEN - 4)) tgtR = (float)(DELAY_BUF_LEN - 4);
    }

    static inline float tap(const int16_t* buf, uint32_t wpos, float delaySamp) {
        uint32_t d  = (uint32_t)delaySamp;
        float    fr = delaySamp - (float)d;
        uint32_t i0 = (wpos - d)     & DELAY_BUF_MASK;
        uint32_t i1 = (wpos - d - 1) & DELAY_BUF_MASK;
        float a = (float)buf[i0], b = (float)buf[i1];
        return (a + (b - a) * fr) * (1.0f / 32768.0f);
    }

    inline void process(float& l, float& r, float fb, float mix, bool pingPong) {
        lenL += (tgtL - lenL) * 0.0004f;
        lenR += (tgtR - lenR) * 0.0004f;

        float wetL = tap(bufL, wpos, lenL);
        float wetR = tap(bufR, wpos, lenR);

        float inL, inR;
        if (pingPong) {
            inL = l + wetR * fb;      // cross-feed: left line is fed by the right tap
            inR = r + wetL * fb;
        } else {
            inL = l + wetL * fb;
            inR = r + wetR * fb;
        }
        bufL[wpos] = (int16_t)(safeClamp(inL, -1.0f, 1.0f) * 32767.0f);
        bufR[wpos] = (int16_t)(safeClamp(inR, -1.0f, 1.0f) * 32767.0f);
        wpos = (wpos + 1) & DELAY_BUF_MASK;

        l += wetL * mix;
        r += wetR * mix;
    }
};

// ─── Reverb (Schroeder/Freeverb-lite) ───────────────────────────────────────
// One mono comb bank feeding two different allpass chains, so the tail is
// genuinely stereo for the cost of two extra short buffers rather than a second
// full reverb.
struct Reverb {
    static const int NCOMB = 4;
    static const int NAP   = 2;
    static const uint16_t CLEN[NCOMB];
    static const uint16_t ALEN[NAP];
    static const uint16_t ALEN_R[NAP];

    // Sized to the longest entry in CLEN/ALEN below, not to the average.
    float    cbuf[NCOMB][1700];
    float    abuf[NAP][610];
    float    abufR[NAP][610];
    uint16_t cpos[NCOMB], apos[NAP], aposR[NAP];
    float    cfilt[NCOMB];
    float    feedback = 0.84f, damp = 0.4f;

    void begin() {
        for (int i = 0; i < NCOMB; i++) {
            for (int j = 0; j < 1700; j++) cbuf[i][j] = 0.0f;
            cpos[i] = 0; cfilt[i] = 0.0f;
        }
        for (int i = 0; i < NAP; i++) {
            for (int j = 0; j < 610; j++) { abuf[i][j] = 0.0f; abufR[i][j] = 0.0f; }
            apos[i] = 0; aposR[i] = 0;
        }
    }
    inline void setRoom(float size, float dmp) { feedback = size; damp = dmp; }

    inline float comb(int i, float in) {
        float y = cbuf[i][cpos[i]];
        cfilt[i] = y * (1.0f - damp) + cfilt[i] * damp;
        cbuf[i][cpos[i]] = in + cfilt[i] * feedback;
        if (++cpos[i] >= CLEN[i]) cpos[i] = 0;
        return y;
    }
    inline float allpass(float* buf, uint16_t& pos, uint16_t len, float in) {
        float y   = buf[pos];
        float out = y - in;
        buf[pos] = in + y * 0.5f;
        if (++pos >= len) pos = 0;
        return out;
    }

    inline void process(float inMono, float& outL, float& outR) {
        float c = 0.0f;
        for (int i = 0; i < NCOMB; i++) c += comb(i, inMono);
        // Each comb settles at 1/(1-feedback), which is 33x at the largest room
        // size — summing four of them and handing that to the mix control would
        // clip the output the moment the send left zero.  Averaging the combs
        // and then trimming by (1-feedback) normalises the tail to roughly unity
        // at any room size, so REV MIX behaves like a wet/dry control.
        c *= (1.0f / (float)NCOMB) * (1.0f - feedback) * 1.25f;
        float l = c, r = c;
        for (int i = 0; i < NAP; i++) {
            l = allpass(abuf[i],  apos[i],  ALEN[i],   l);
            r = allpass(abufR[i], aposR[i], ALEN_R[i], r);
        }
        outL = l; outR = r;
    }
};

// ─── The one shared effect chain ────────────────────────────────────────────
// Chorus is 8KB, StereoDelay 128KB (two DELAY_BUF_LEN int16_t channels —
// config.h's DELAY_BUF_LEN comment gives the PER-CHANNEL 64KB; both bufL and
// bufR carry that, so the struct total is double it, not equal to it), Reverb
// 36KB — ~172KB per instance. Every
// engine could in principle own its own private trio, but only one engine
// ever renders at a time (see engine.h), so there is no correctness reason
// for separate copies — unlike each engine's own param array, which is
// deliberately session-persistent so switching engines preserves your
// tweaks, effect *buffers* are transient audio processing state a user has
// no expectation of surviving an engine switch. Every engine calls .begin()
// on this shared instance on every activation, so the audible behaviour
// (effects reset on switch) is exactly what owning a private trio would have
// given anyway — this just doesn't pay for it six times over in SRAM.
extern Chorus      g_fxChorus;
extern StereoDelay g_fxDelay;
extern Reverb      g_fxReverb;

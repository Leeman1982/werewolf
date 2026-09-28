#include "Arduino.h"
#include "dsp_tables.h"
#include "cs80_params.h"
#include "cs80_voice.h"
#include "fx.h"
#include "sequencer.h"
#include "scales.h"
#include "voicealloc.h"
#include "engine.h"
#include "engine_cs80.h"
#include "minimoog.h"
#include "prophet5.h"
#include "prophet10.h"
#include "arp2600.h"
#include "odyssey.h"
#include "mellotron.h"
#include "oberheim.h"
#include "jupiter8.h"
#include "system100.h"
#include "moogmodular.h"
#include "obx.h"
#include "ppgwave.h"
#include "ms20.h"
#include "dx7.h"
#include "dx7_rom.h"
#include "stringens.h"
#include "prophetvs.h"
#include "stub_engine.h"
#include "ladder_filter.h"

// Every engine is real here, matching engine.cpp's dispatch switch. The
// keyboard-style layer is staged for a follow-up pass and is deliberately
// not linked or tested yet.

static unsigned long g_us = 0;
unsigned long micros() { return g_us; }
unsigned long millis() { return g_us / 1000; }
void hostAdvanceUs(unsigned long us) { g_us += us; }

// Every new engine module extern's this — the shared cross-engine voice
// allocator (see voicealloc.h). Only one instance exists anywhere.
VoiceAlloc va;

static int failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); \
    printf("   [%s:%d]\n", __FILE__, __LINE__); failures++; } } while (0)

static NoiseGen ng;
static Reverb rev;
static StereoDelay dly;
static Chorus cho;

// Sequencer hooks (unused here, but the translation unit needs them).
static int onCount, offCount;
static void tOn(uint8_t, uint8_t, uint8_t, uint8_t, bool, bool) { onCount++; }
static void tOff(uint8_t, uint8_t, uint8_t) { offCount++; }
static void tLock(uint8_t p, uint8_t v) { setParameter(p, v / 255.0f); }
void seqEmitClock() {} void seqEmitStart() {} void seqEmitStop() {} void seqEmitContinue() {}

// Measured peak of a filter's impulse response tail, which is what tells us
// whether it rings, decays, or runs away.
static float impulseTail(Cs80Svf* f, int mode /*0=lp 1=hp*/, int skip, int len) {
    float peak = 0;
    for (int i = 0; i < len; i++) {
        float lp, hp;
        cs80SvfProcess(f, (i == 0) ? 1.0f : 0.0f, &lp, &hp);
        float y = mode ? hp : lp;
        if (!std::isfinite(y)) return 1e9f;
        if (i > skip) peak = std::max(peak, fabsf(y));
    }
    return peak;
}

// ═══ 0. Shared DSP tables — engine-agnostic, dsp_tables.cpp is common to
//        every engine in this codebase ═══════════════════════════════════
static void testTables() {
    printf("\n-- tables --\n");
    CHECK(fabsf(sineFromPhase(0)) < 1e-3f, "sine(0) = %f, want 0\n", sineFromPhase(0));
    CHECK(fabsf(sineFromPhase(0x40000000u) - 1.0f) < 1e-3f, "sine(90deg) = %f, want 1\n", sineFromPhase(0x40000000u));
    CHECK(fabsf(sineFromPhase(0xC0000000u) + 1.0f) < 1e-3f, "sine(270deg) = %f, want -1\n", sineFromPhase(0xC0000000u));

    float worst = 0;
    for (float x = -8.0f; x <= 8.0f; x += 0.01f)
        worst = std::max(worst, fabsf(fastTanh(x) - tanhf(x)));
    CHECK(worst < 0.002f, "fastTanh worst error %f\n", worst);

    worst = 0;
    for (float x = -6.0f; x <= 6.0f; x += 0.01f)
        worst = std::max(worst, fabsf(fastPow2(x) - powf(2.0f, x)) / powf(2.0f, x));
    CHECK(worst < 0.002f, "fastPow2 worst relative error %f\n", worst);

    CHECK(fabsf(noteToFreq(69) - 440.0f) < 0.01f, "A4 = %f\n", noteToFreq(69));
    CHECK(fabsf(noteToFreq(60) - 261.626f) < 0.01f, "C4 = %f\n", noteToFreq(60));

    // Filter index really is one octave per FILT_IDX_PER_OCT.
    float hz0 = filterHzFromIdx(200.0f);
    float hz1 = filterHzFromIdx(200.0f + FILT_IDX_PER_OCT);
    CHECK(fabsf(hz1 / hz0 - 2.0f) < 0.02f, "one index-octave = %.3fx, want 2x\n", hz1 / hz0);
    for (int i = 1; i < FILT_TAB_SIZE; i++) {
        CHECK(std::isfinite(FILT_G[i]) && FILT_G[i] > 0.0f, "FILT_G[%d] = %f\n", i, FILT_G[i]);
        CHECK(FILT_G[i] >= FILT_G[i - 1], "FILT_G is not monotonic at %d\n", i);
    }
}

// ═══ 0b. Effects — engine-agnostic, fx.cpp is shared by every engine (see
//         g_fxChorus/g_fxDelay/g_fxReverb in fx.h) ═══════════════════════
static void testEffects() {
    printf("\n-- effects --\n");

    // Reverb: biggest room, unit impulse, must decay rather than diverge.
    rev.begin();
    rev.setRoom(0.97f, 0.4f);
    float peakEarly = 0, peakLate = 0;
    for (int i = 0; i < 480000; i++) {          // 10 s
        float l, r;
        rev.process(i == 0 ? 1.0f : 0.0f, l, r);
        CHECK(std::isfinite(l) && std::isfinite(r), "reverb went non-finite at %d\n", i);
        if (i < 48000)       peakEarly = std::max(peakEarly, fabsf(l));
        else if (i > 432000) peakLate  = std::max(peakLate,  fabsf(l));
    }
    CHECK(peakEarly < 2.0f, "reverb impulse response peaked at %.2f (needs output trim)\n", peakEarly);
    CHECK(peakLate < peakEarly, "reverb tail is growing: early %.4f, late %.4f\n", peakEarly, peakLate);

    // Sustained input at the largest room must not run away either.
    rev.begin(); rev.setRoom(0.97f, 0.4f);
    ng.seed = 99; ng.p0 = ng.p1 = ng.p2 = 0;
    float wetPeak = 0;
    for (int i = 0; i < 240000; i++) {
        float l, r; rev.process(noiseWhite(&ng) * 0.7f, l, r);
        if (i > 48000) wetPeak = std::max(wetPeak, fabsf(l));
    }
    CHECK(wetPeak < 3.0f, "reverb wet level %.2f on sustained input (would clip)\n", wetPeak);

    // Delay: maximum feedback must stay bounded.
    dly.begin(); dly.setTimeMs(250.0f, false);
    float dPeak = 0;
    for (int i = 0; i < 480000; i++) {
        float l = (i < 4800) ? 0.8f : 0.0f, r = l;
        dly.process(l, r, 0.95f, 1.0f, false);
        CHECK(std::isfinite(l) && std::isfinite(r), "delay went non-finite at %d\n", i);
        dPeak = std::max(dPeak, std::max(fabsf(l), fabsf(r)));
    }
    CHECK(dPeak < 4.0f, "delay peaked at %.2f with fb=0.95\n", dPeak);

    // Ping-pong must actually alternate sides.
    dly.begin(); dly.setTimeMs(100.0f, true);
    for (int i = 0; i < 20000; i++) { float l = 0, r = 0; dly.process(l, r, 0.6f, 1.0f, true); }
    dly.begin(); dly.setTimeMs(100.0f, true);
    float sumL = 0, sumR = 0;
    for (int i = 0; i < 48000; i++) {
        float l = (i < 100) ? 1.0f : 0.0f, r = 0.0f;   // impulse on LEFT only
        dly.process(l, r, 0.6f, 1.0f, true);
        if (i > 200) { sumL += fabsf(l); sumR += fabsf(r); }
    }
    CHECK(sumR > 0.05f * sumL, "ping-pong never crossed to the right channel (L=%.2f R=%.2f)\n", sumL, sumR);

    // Chorus: bounded, and audibly different from the dry signal. A plain
    // phase-accumulated saw stands in for the old single-engine build's Osc
    // helper (retired with it) -- chorus doesn't care what the waveform is,
    // only that it's periodic and non-trivial.
    cho.begin(); cho.setRate(1.0f);
    float phase = 0.0f, phaseInc = 220.0f / SAMPLE_RATE_F;
    float cPeak = 0, diff = 0;
    for (int i = 0; i < 96000; i++) {
        float dry = (2.0f * phase - 1.0f) * 0.5f;
        phase += phaseInc; if (phase >= 1.0f) phase -= 1.0f;
        float l = dry, r = dry;
        cho.process(l, r, 0.6f, 0.5f);
        CHECK(std::isfinite(l), "chorus went non-finite at %d\n", i);
        cPeak = std::max(cPeak, fabsf(l));
        if (i > 48000) diff += fabsf(l - dry);
    }
    CHECK(cPeak < 2.0f, "chorus peaked at %.2f\n", cPeak);
    CHECK(diff > 100.0f, "chorus output is identical to dry (buffer never engaged)\n");

    // Crusher: bypassed at hold 0, and quantising when engaged.
    Crusher cr;
    float a = 0.4321f, b = 0.4321f;
    cr.process(a, b, 0, 0);
    CHECK(a == 0.4321f, "crusher altered the signal while bypassed\n");
    int changes = 0; float last = 999;
    for (int i = 0; i < 100; i++) {
        float x = sinf(i * 0.1f), y = x;
        cr.process(x, y, 8, 0);
        if (x != last) { changes++; last = x; }
    }
    CHECK(changes > 5 && changes < 30, "crusher hold=8 gave %d distinct values in 100 samples\n", changes);
}

// ═══ 0c. Sequencer — engine-agnostic (drives whichever engine's setParameter
//         is wired to seqLock; CS-80's here, same as production) ══════════
static int noteOnCount, noteOffCount;
static struct { unsigned long t; uint8_t note; uint8_t track; } fired[4096];
static int firedN;

static void tNoteOn(uint8_t tr, uint8_t ch, uint8_t note, uint8_t vel, bool ac, bool sl) {
    (void)ch;
    (void)vel; (void)ac; (void)sl;
    if (firedN < 4096) { fired[firedN].t = micros(); fired[firedN].note = note; fired[firedN].track = tr; firedN++; }
    noteOnCount++;
}
static void tNoteOff(uint8_t tr, uint8_t ch, uint8_t note) { (void)tr; (void)ch; (void)note; noteOffCount++; }

static void runSeq(Sequencer& s, unsigned long totalUs, unsigned long stepUs = 200) {
    for (unsigned long t = 0; t < totalUs; t += stepUs) { hostAdvanceUs(stepUs); s.update(); }
}

static void testSequencer() {
    printf("\n-- sequencer --\n");
    seqNoteOn = tNoteOn; seqNoteOff = tNoteOff; seqLock = tLock;

    static Sequencer s;
    s.begin(0);
    s.setBPM(120);
    Pattern& p = s.currentPatternRef();
    p.length = 16; p.scaleIdx = 0;
    for (int i = 0; i < 16; i++) { p.steps[i].flags = ST_ACTIVE; p.steps[i].notes[0] = 60; p.steps[i].noteCount = 1; }

    // At 120 BPM a 16th is 125 ms; one bar of 16 steps is 2 s.
    CHECK(s.stepIntervalUs() == 125000, "step interval %lu us at 120 BPM, want 125000\n", s.stepIntervalUs());

    noteOnCount = noteOffCount = firedN = 0;
    s.play();
    runSeq(s, 1990000UL);
    s.stop();
    CHECK(noteOnCount == 16, "one bar fired %d notes, want 16\n", noteOnCount);

    // Inter-step jitter must be within the polling resolution.
    long worst = 0;
    for (int i = 1; i < firedN; i++) {
        long dt = (long)(fired[i].t - fired[i - 1].t);
        worst = std::max(worst, labs(dt - 125000));
    }
    CHECK(worst < 1000, "worst step jitter %ld us\n", worst);

    // Swing: odd steps late, even early, pairs still spanning two steps.
    p.swing = 50;
    noteOnCount = firedN = 0;
    s.play(); runSeq(s, 2000000UL); s.stop();
    CHECK(firedN >= 15, "swing run fired only %d notes\n", firedN);
    if (firedN >= 5) {
        long d0 = (long)(fired[1].t - fired[0].t);
        long d1 = (long)(fired[2].t - fired[1].t);
        CHECK(d0 < d1, "swing did not lengthen the off-beat (%ld then %ld)\n", d0, d1);
        CHECK(labs((d0 + d1) - 250000) < 2000, "swung pair spans %ld us, want 250000\n", d0 + d1);
    }
    p.swing = 0;

    // Ratchets multiply the hits inside one step.
    for (int i = 0; i < 16; i++) p.steps[i].flags = 0;
    p.steps[0].flags = ST_ACTIVE; p.steps[0].ratchet = 4;
    noteOnCount = firedN = 0;
    s.play(); runSeq(s, 1900000UL); s.stop();
    CHECK(noteOnCount == 4, "ratchet x4 fired %d notes, want 4\n", noteOnCount);
    if (firedN >= 4) {
        long r = (long)(fired[1].t - fired[0].t);
        CHECK(labs(r - 31250) < 1500, "ratchet spacing %ld us, want 31250\n", r);
    }
    p.steps[0].ratchet = 1;

    // Micro-timing nudges a step off the grid by the requested percentage.
    p.steps[4].flags = ST_ACTIVE; p.steps[4].micro = 25;   // +25% of a step
    noteOnCount = firedN = 0;
    s.play(); runSeq(s, 1000000UL); s.stop();
    CHECK(firedN >= 2, "micro run fired only %d notes\n", firedN);
    if (firedN >= 2) {
        long gap = (long)(fired[1].t - fired[0].t);
        CHECK(labs(gap - (4 * 125000 + 31250)) < 1500,
              "micro +25%% landed at %ld us after step 0, want %d\n", gap, 4 * 125000 + 31250);
    }
    p.steps[4].micro = 0; p.steps[4].flags = 0;

    // Probability 0 must never fire; 100 must always fire.
    p.steps[0].probability = 0;
    noteOnCount = 0;
    s.play(); runSeq(s, 4000000UL); s.stop();
    CHECK(noteOnCount == 0, "probability 0 still fired %d times\n", noteOnCount);
    p.steps[0].probability = 100;

    // Chords: one step, four notes, four note-ons.
    p.steps[0].noteCount = 4;
    p.steps[0].notes[0] = 60; p.steps[0].notes[1] = 64;
    p.steps[0].notes[2] = 67; p.steps[0].notes[3] = 71;
    noteOnCount = firedN = 0;
    s.play(); runSeq(s, 1900000UL); s.stop();
    CHECK(noteOnCount == 4, "4-note chord fired %d note-ons\n", noteOnCount);
    p.steps[0].noteCount = 1;

    // Every note must be released — no stuck notes after a full run.
    for (int i = 0; i < 16; i++) { p.steps[i].flags = ST_ACTIVE; p.steps[i].gate = 50; }
    noteOnCount = noteOffCount = 0;
    s.play(); runSeq(s, 4000000UL); s.stop();
    runSeq(s, 500000UL);
    CHECK(noteOnCount == noteOffCount, "%d note-ons but %d note-offs (stuck notes)\n", noteOnCount, noteOffCount);

    // Scale quantisation must pull every note into the scale.
    p.rootNote = 0; p.scaleIdx = 2;                       // C minor
    for (int i = 0; i < 16; i++) p.steps[i].notes[0] = (uint8_t)(60 + i);
    noteOnCount = firedN = 0;
    s.play(); runSeq(s, 2000000UL); s.stop();
    for (int i = 0; i < firedN; i++)
        CHECK(inScale(0, 2, fired[i].note), "quantiser emitted %d, out of C minor\n", fired[i].note);

    // Pattern length is respected: 4 steps at 120 BPM = 500 ms per lap.
    p.length = 4;
    noteOnCount = 0;
    s.play(); runSeq(s, 1990000UL); s.stop();
    CHECK(noteOnCount == 16, "4-step loop over 2 s fired %d, want 16\n", noteOnCount);
    p.length = 16;

    // Euclid distributes hits evenly and hits the requested count.
    for (int hits = 1; hits <= 16; hits++) {
        s.euclidFill(0, (uint8_t)hits, 0);
        int on = 0;
        for (int i = 0; i < 16; i++) if (p.steps[i].active()) on++;
        CHECK(on == hits, "euclid(%d,16) produced %d hits\n", hits, on);
    }
    s.euclidFill(0, 4, 0);
    CHECK(p.steps[0].active() && p.steps[4].active() && p.steps[8].active() && p.steps[12].active(),
          "euclid(4,16) did not land on the quarters\n");

    // External sync: 6 clock ticks per 16th step.
    static Sequencer x;
    x.begin(1);
    Pattern& xp = x.currentPatternRef();
    xp.length = 16;
    for (int i = 0; i < 16; i++) { xp.steps[i].flags = ST_ACTIVE; xp.steps[i].notes[0] = 60; xp.steps[i].noteCount = 1; }
    x.setExternalSync(true);
    noteOnCount = 0;
    x.extStart();
    for (int t = 0; t < 96; t++) { hostAdvanceUs(20833); x.extTick(); x.update(); }
    CHECK(noteOnCount == 16, "96 clock ticks (4 beats) fired %d steps, want 16\n", noteOnCount);
    x.extStop();
}

// ═══ 1. The filter must not self-oscillate, at any cutoff or resonance ═════
static void testNoSelfOscillation() {
    printf("\n-- filter: no self-oscillation --\n");
    for (int idx = 0; idx < FILT_TAB_SIZE; idx += 32) {
        for (int mode = 0; mode < 2; mode++) {
            Cs80Svf f; cs80SvfInit(&f);
            cs80SvfSet(&f, (float)idx, 1.0f);          // resonance at maximum
            CHECK(f.k > 0.0f, "damping reached zero at idx %d (would self-oscillate)\n", idx);

            // Excite, then look at a much later window: a self-oscillating
            // filter sustains, a CS filter does not.
            float early = impulseTail(&f, mode, 0, 2000);
            float late  = impulseTail(&f, mode, 20000, 24000);
            CHECK(std::isfinite(early) && early < 100.0f,
                  "idx %d mode %d blew up (peak %.2f)\n", idx, mode, early);
            CHECK(late < early * 0.02f + 1e-4f,
                  "idx %d mode %d still ringing after 0.5 s (early %.4g, late %.4g)"
                  " - that is self-oscillation\n", idx, mode, early, late);
        }
    }
}

// ═══ 2. Q must FALL as cutoff rises — the CS filter's defining manner ══════
static void testQFallsWithCutoff() {
    printf("\n-- filter: Q falls as cutoff rises --\n");
    float prevK = -1.0f;
    for (int idx = 0; idx < FILT_TAB_SIZE; idx += 64) {
        Cs80Svf f; cs80SvfInit(&f);
        cs80SvfSet(&f, (float)idx, 1.0f);
        CHECK(f.k >= prevK - 1e-6f,
              "damping fell going up the range at idx %d (%.3f after %.3f)\n", idx, f.k, prevK);
        prevK = f.k;
    }
    Cs80Svf lo, hi;
    cs80SvfInit(&lo); cs80SvfSet(&lo, 100.0f, 1.0f);
    cs80SvfInit(&hi); cs80SvfSet(&hi, 950.0f, 1.0f);
    CHECK(hi.k > lo.k * 1.5f,
          "damping barely moved across the range (%.3f low vs %.3f high) - Q is not"
          " frequency dependent\n", lo.k, hi.k);
    // Q at the bottom should still be genuinely resonant.
    CHECK(1.0f / lo.k > 3.0f, "max Q at low cutoff is only %.2f\n", 1.0f / lo.k);
    // ...and tamed, not flat, at the top.
    CHECK(1.0f / hi.k > 1.0f && 1.0f / hi.k < 2.5f,
          "Q at high cutoff is %.2f, want roughly 1-2.5\n", 1.0f / hi.k);
}

// ═══ 3. The sawtooth reset glitch ══════════════════════════════════════════
static void testSawGlitch() {
    printf("\n-- oscillator: the reset glitch --\n");

    // Its width is fixed in TIME, so it must span the same number of samples
    // whatever the pitch — that frequency independence is the whole point.
    for (float hz : {55.0f, 220.0f, 880.0f}) {
        Cs80Osc a, b;
        cs80OscInit(&a); a.wave = CSW_SAW; cs80OscSetFreq(&a, hz);
        cs80OscInit(&b); b.wave = CSW_SAW; cs80OscSetFreq(&b, hz);

        // Reference without the glitch: same oscillator, glitch suppressed.
        int n = (int)(SAMPLE_RATE_F / hz) * 3;
        int glitchSamples = 0;
        float glitchEnergy = 0;
        for (int i = 0; i < n; i++) {
            float withG = cs80OscNext(&a, 0.5f);
            // Reconstruct the clean saw at the same phase.
            uint32_t ph = b.phase;
            float t = phaseToFloat(ph);
            float clean = 2.0f * t - 1.0f - csPolyBlep(t, b.dt, b.invDt);
            b.phase += b.inc;
            float diff = withG - clean;
            if (fabsf(diff) > 1e-4f) { glitchSamples++; glitchEnergy += fabsf(diff); }
        }
        int perCycle = glitchSamples / 3;
        CHECK(perCycle >= 1 && perCycle <= 3,
              "at %.0f Hz the glitch spans %d samples per cycle, want ~%.1f\n",
              hz, perCycle, (double)CS_GLITCH_SAMPLES);
        CHECK(glitchEnergy > 0.0f, "no glitch at all at %.0f Hz\n", hz);
    }

    // What the glitch contributes, measured on its own.
    //
    // A tempting test is "the glitched saw has more energy above 10 kHz than a
    // clean one".  It does not, and that is not a bug: at 110 Hz the saw's own
    // 91st harmonic is around 0.007, while the glitch — a 0.09 pulse at 0.3 %
    // duty — puts roughly 0.0006 into each harmonic.  The saw wins on raw level
    // everywhere.
    //
    // The glitch earns its keep because it is BROADBAND.  A pulse of width tau
    // has a sinc spectrum whose first null is at 1/tau = 33 kHz, so across the
    // whole audio band it is nearly flat — which is why it still injects a click
    // of energy at every cycle once the low-pass has removed the saw's
    // harmonics, and that is how the instrument is actually played.
    //
    // Measured with an exact DFT rather than a filter, so the result is the
    // glitch's spectrum and not the analyser's.
    {
        // 100 Hz gives exactly 480 samples per cycle at 48 kHz, so every
        // harmonic lands on a bin centre and there is no leakage to argue about.
        const int N = 24000;
        static float diff[24000];
        Cs80Osc a, b;
        cs80OscInit(&a); a.wave = CSW_SAW; cs80OscSetFreq(&a, 100.0f);
        cs80OscInit(&b); b.wave = CSW_SAW; cs80OscSetFreq(&b, 100.0f);
        for (int i = 0; i < N; i++) {
            float g = cs80OscNext(&a, 0.5f);
            float t = phaseToFloat(b.phase);
            float c = 2.0f * t - 1.0f - csPolyBlep(t, b.dt, b.invDt);
            b.phase += b.inc;
            diff[i] = g - c;                       // the glitch, isolated
        }
        auto binMag = [&](double hz) {
            double k = hz * N / (double)SAMPLE_RATE;
            double re = 0, im = 0;
            for (int i = 0; i < N; i++) {
                double w = 2.0 * M_PI * k * i / N;
                re += diff[i] * cos(w);
                im -= diff[i] * sin(w);
            }
            return 2.0 * sqrt(re * re + im * im) / N;
        };
        double m1k = binMag(1000.0), m8k = binMag(8000.0), m15k = binMag(15000.0);
        printf("   glitch spectrum: %.5f @1k  %.5f @8k  %.5f @15k\n", m1k, m8k, m15k);
        CHECK(m1k > 1e-5, "no glitch energy at all\n");
        // sinc(pi*f*tau) at 15 kHz with tau = 30 us predicts about 0.70 of the
        // level at 1 kHz; anything near that confirms a genuinely wide pulse.
        CHECK(m15k > m1k * 0.5,
              "the glitch is not broadband: 15 kHz is only %.0f%% of 1 kHz, so the"
              " pulse is too wide and would thicken rather than brighten\n",
              100.0 * m15k / m1k);
        CHECK(m8k > m1k * 0.7, "spectrum sags at 8 kHz (%.0f%% of 1 kHz)\n",
              100.0 * m8k / m1k);
    }

    // And its amplitude must match the specified 0.45 V of a 10 V pk-pk saw.
    {
        Cs80Osc a, b;
        cs80OscInit(&a); a.wave = CSW_SAW; cs80OscSetFreq(&a, 220.0f);
        cs80OscInit(&b); b.wave = CSW_SAW; cs80OscSetFreq(&b, 220.0f);
        float maxDiff = 0;
        for (int i = 0; i < 4800; i++) {
            float g = cs80OscNext(&a, 0.5f);
            float t = phaseToFloat(b.phase);
            float c = 2.0f * t - 1.0f - csPolyBlep(t, b.dt, b.invDt);
            b.phase += b.inc;
            maxDiff = std::max(maxDiff, fabsf(g - c));
        }
        CHECK(fabsf(maxDiff - CS_GLITCH_AMP) < 0.005f,
              "glitch amplitude is %.4f, want %.4f\n", maxDiff, (double)CS_GLITCH_AMP);
    }

    // The sine is shaped by the WSC rather than switched, so it carries no glitch.
    Cs80Osc s; cs80OscInit(&s); s.wave = CSW_SINE; cs80OscSetFreq(&s, 220.0f);
    float worst = 0;
    for (int i = 0; i < 4800; i++) {
        float v = cs80OscNext(&s, 0.5f);
        worst = std::max(worst, fabsf(v));
    }
    CHECK(worst <= 1.001f, "sine output exceeded unity (%.4f) - glitch leaked in\n", worst);
}

// ═══ 4. The IL / AL filter envelope ════════════════════════════════════════
static void testFilterEnvelope() {
    printf("\n-- filter envelope: IL / AL / A / D / R --\n");
    const float ctrlRate = SAMPLE_RATE_F / CONTROL_RATE_DIV;
    auto coef = [&](float t) { return 1.0f - expf(-6.9f / (t * ctrlRate)); };

    // Ordinary shape: starts at IL, rises to AL, decays toward zero.
    {
        Cs80FiltEnv e{};
        float il = -0.3f, al = 0.8f;
        cs80FiltTrigger(&e, il, al, fabsf(al - il) / (0.05f * ctrlRate), coef(0.2f), coef(0.3f));
        CHECK(fabsf(e.value - il) < 1e-6f, "envelope did not start at IL\n");
        float peak = e.value;
        int   peakAt = 0;
        for (int i = 0; i < (int)ctrlRate; i++) {
            float v = cs80FiltProcess(&e);
            if (v > peak) { peak = v; peakAt = i; }
        }
        CHECK(fabsf(peak - al) < 0.02f, "attack reached %.3f, want AL = %.3f\n", peak, al);
        CHECK(fabsf(peakAt / ctrlRate - 0.05f) < 0.01f,
              "attack took %.3f s, want 0.05 s\n", peakAt / ctrlRate);
        CHECK(fabsf(e.value) < 0.1f, "decay did not settle toward zero (%.3f)\n", e.value);
    }

    // AL below IL: the "attack" is a downward lurch.  A conventional ADSR
    // cannot do this, and it is half of the CS-80's percussive character.
    {
        Cs80FiltEnv e{};
        float il = 0.6f, al = -0.5f;
        cs80FiltTrigger(&e, il, al, fabsf(al - il) / (0.03f * ctrlRate), coef(0.2f), coef(0.3f));
        float first = cs80FiltProcess(&e);
        CHECK(first < il, "with AL below IL the attack must fall, but it rose\n");
        for (int i = 0; i < (int)(ctrlRate * 0.05f); i++) cs80FiltProcess(&e);
        CHECK(e.value < 0.0f, "never reached the negative attack level (%.3f)\n", e.value);
        // ...and then the decay RISES back toward zero: a second attack.
        float before = e.value;
        for (int i = 0; i < (int)(ctrlRate * 0.1f); i++) cs80FiltProcess(&e);
        CHECK(e.value > before,
              "decay did not act as a second attack (%.3f then %.3f)\n", before, e.value);
    }

    // Release returns toward IL, not toward zero.
    {
        Cs80FiltEnv e{};
        float il = -0.4f, al = 0.5f;
        cs80FiltTrigger(&e, il, al, fabsf(al - il) / (0.01f * ctrlRate), coef(0.05f), coef(0.05f));
        for (int i = 0; i < (int)(ctrlRate * 0.3f); i++) cs80FiltProcess(&e);
        cs80FiltRelease(&e);
        for (int i = 0; i < (int)(ctrlRate * 0.5f); i++) cs80FiltProcess(&e);
        CHECK(fabsf(e.value - il) < 0.05f,
              "release settled at %.3f, want IL = %.3f\n", e.value, il);
    }
}

// ═══ 5. Hz/V pitch: modulation is a frequency offset, not an interval ══════
static void testHzPerVolt() {
    printf("\n-- Hz/V pitch architecture --\n");
    setParameter(P_LFO_PITCH, 1.0f);
    float devHz = cs.lfoPitchHz;
    CHECK(devHz > 1.0f, "LFO pitch depth decoded to %.3f Hz\n", devHz);

    // The same modulation voltage on a low note and a high note must produce
    // the same FREQUENCY deviation, which means a much larger musical interval
    // down low.  That asymmetry is the architecture, not a bug.
    float lowHz = noteToFreq(36), highHz = noteToFreq(84);
    float lowCents  = 1200.0f * log2f((lowHz  + devHz) / lowHz);
    float highCents = 1200.0f * log2f((highHz + devHz) / highHz);
    CHECK(lowCents > highCents * 4.0f,
          "vibrato is nearly equal-interval across the keyboard (%.0f vs %.0f cents)"
          " - that is V/oct behaviour, not Hz/V\n", lowCents, highCents);
    printf("   +%.1f Hz = %.0f cents at C2, %.0f cents at C6\n", devHz, lowCents, highCents);
}

// ═══ 6. Filter cutoff floor and the fixed pole ═════════════════════════════
static void testCutoffRange() {
    printf("\n-- cutoff range --\n");
    setParameter(C1_LPF_CUT, 0.0f);
    float lowHz = filterHzFromIdx(cs.ch[0].lpIdx);
    CHECK(lowHz >= CS_MIN_CUTOFF_HZ * 0.9f && lowHz <= CS_MIN_CUTOFF_HZ * 1.6f,
          "minimum cutoff is %.1f Hz, want about %.0f\n", lowHz, CS_MIN_CUTOFF_HZ);
    setParameter(C1_LPF_CUT, 1.0f);
    CHECK(filterHzFromIdx(cs.ch[0].lpIdx) > 10000.0f,
          "maximum cutoff only reaches %.0f Hz\n", filterHzFromIdx(cs.ch[0].lpIdx));

    // The gentle fixed pole must actually attenuate above ~7.6 kHz.
    Cs80OnePole p{0.0f};
    auto amp = [&](float hz) {
        p.z = 0.0f;
        float peak = 0;
        uint32_t ph = 0, inc = (uint32_t)(hz * PHASE_PER_HZ);
        for (int i = 0; i < 8000; i++) {
            float y = cs80OnePole(&p, sineFromPhase(ph), CS_FIXED_LP_G);
            ph += inc;
            if (i > 2000) peak = std::max(peak, fabsf(y));
        }
        return peak;
    };
    float a1k = amp(1000.0f), a16k = amp(16000.0f);
    CHECK(a1k > 0.85f, "fixed pole is eating the midrange (%.3f at 1 kHz)\n", a1k);
    CHECK(a16k < a1k * 0.75f, "fixed pole barely attenuates (%.3f at 16 kHz vs %.3f at 1 kHz)\n",
          a16k, a1k);
    printf("   fixed pole: %.2f at 1 kHz, %.2f at 16 kHz\n", a1k, a16k);
}

// ═══ 7. Every preset renders through a complete two-channel voice ══════════
static Cs80Voice tv;

static float renderTestVoice(Cs80Voice* v, int n, float* peakOut) {
    double sumSq = 0; float peak = 0;
    for (int i = 0; i < n; i++) {
        if ((i & (CONTROL_RATE_DIV - 1)) == 0) {
            for (int c = 0; c < CS_CHANNELS; c++) {
                const Cs80ChanParams& p = cs.ch[c];
                Cs80Channel& ch = v->ch[c];
                ch.osc.wave = p.wave;
                cs80OscSetFreq(&ch.osc, v->currentFreq * p.feet * p.tune);
                ch.pwmInc = p.pwmInc;
                float env = cs80FiltProcess(&ch.filtEnv);
                float common = cs.brilliance + env + v->velNorm * p.velBri;
                float hp = constrain(p.hpIdx + common, 0.0f, (float)(FILT_TAB_SIZE - 1));
                float lp = constrain(p.lpIdx + common, 0.0f, (float)(FILT_TAB_SIZE - 1));
                cs80SvfSet(&ch.hpf, hp, std::min(1.0f, p.hpRes + cs.resonance));
                cs80SvfSet(&ch.lpf, lp, std::min(1.0f, p.lpRes + cs.resonance));
            }
        }
        float noise = noiseWhite(&ng);
        float out = 0;
        for (int c = 0; c < CS_CHANNELS; c++) {
            const Cs80ChanParams& p = cs.ch[c];
            Cs80Channel& ch = v->ch[c];
            float pw = p.pw;
            if (p.wave == CSW_PULSE && p.pwmDepth > 0.001f) {
                ch.pwmPhase += ch.pwmInc;
                pw = constrain(pw + sineFromPhase(ch.pwmPhase) * p.pwmDepth, 0.05f, 0.95f);
            }
            float x = cs80OscNext(&ch.osc, pw);
            if (p.noise > 0.004f) x += noise * p.noise;
            float l, h;
            cs80SvfProcess(&ch.hpf, x, &l, &h);
            cs80SvfProcess(&ch.lpf, h, &l, &h);
            float y = cs80OnePole(&ch.fixedLp, l, CS_FIXED_LP_G);
            if (p.sineLvl > 0.004f) y += sineFromPhase(ch.osc.phase) * p.sineLvl;
            out += y * cs80AmpProcess(&ch.ampEnv) * p.level;
        }
        if (!std::isfinite(out)) return -1.0f;
        peak = std::max(peak, fabsf(out));
        sumSq += (double)out * out;
    }
    *peakOut = peak;
    return (float)sqrt(sumSq / n);
}

static void testPresets() {
    printf("\n-- presets through a full two-channel voice --\n");
    int silent = 0;
    for (int i = 0; i < NUM_PRESETS; i++) {
        loadPreset(i);
        cs80VoiceInit(&tv, 0);
        tv.note = 60; tv.velNorm = 0.9f; tv.velocity = 115;
        tv.currentFreq = tv.targetFreq = noteToFreq(60);
        for (int c = 0; c < CS_CHANNELS; c++) {
            const Cs80ChanParams& p = cs.ch[c];
            cs80AmpTrigger(&tv.ch[c].ampEnv, p.ampAtk, p.ampDec, p.ampSus, p.ampRel);
            cs80FiltTrigger(&tv.ch[c].filtEnv, p.vcfIL, p.vcfAL,
                            fabsf(p.vcfAL - p.vcfIL) * p.vcfAtkStep, p.vcfDec, p.vcfRel);
        }
        ng.seed = 7777; ng.p0 = ng.p1 = ng.p2 = 0;
        float peak = 0;
        float rms = renderTestVoice(&tv, (int)SAMPLE_RATE_F, &peak);
        CHECK(rms >= 0.0f, "preset %d (%s) produced non-finite audio\n", i, PRESETS[i].name);
        CHECK(peak < 12.0f, "preset %d (%s) peaked at %.2f before the master stage\n",
              i, PRESETS[i].name, peak);
        if (peak < 0.01f) { printf("   note: %-12s is near-silent (peak %.4f)\n",
                                   PRESETS[i].name, peak); silent++; }
    }
    CHECK(silent == 0, "%d preset(s) produced no audible output\n", silent);
}

// ═══ 8. Panel table integrity ══════════════════════════════════════════════
static void testPanel() {
    printf("\n-- panel table --\n");
    for (int i = 0; i < NUM_PARAMETERS; i++) {
        uint8_t cc = PARAM_INFO[i].cc;
        CHECK(cc != 0 && cc != 32 && cc != 6 && cc != 38 && cc != 64 &&
              !(cc >= 98 && cc <= 101) && cc < 120,
              "param %d (%s) uses reserved CC %d\n", i, PARAM_INFO[i].name, cc);
        for (int j = i + 1; j < NUM_PARAMETERS; j++)
            CHECK(PARAM_INFO[j].cc != cc, "CC %d shared by %s and %s\n",
                  cc, PARAM_INFO[i].name, PARAM_INFO[j].name);
        CHECK(strlen(PARAM_INFO[i].name) <= 10,
              "param name '%s' is too wide for the display\n", PARAM_INFO[i].name);
    }
    // The two channel blocks must be identical in layout — the engine indexes
    // them with one offset, so any drift here would silently mis-decode.
    for (int k = 0; k < CS_CH_PARAMS; k++)
        CHECK(strcmp(PARAM_INFO[k].name, PARAM_INFO[CS_CH_PARAMS + k].name) == 0 &&
              PARAM_INFO[k].kind == PARAM_INFO[CS_CH_PARAMS + k].kind,
              "channel blocks diverge at offset %d\n", k);

    for (int p = 0; p < NUM_PRESETS; p++)
        for (int d = 0; d < PRESETS[p].n; d++)
            CHECK(PRESETS[p].deltas[d].param < NUM_PARAMETERS,
                  "preset %s references parameter %d\n", PRESETS[p].name,
                  PRESETS[p].deltas[d].param);

    loadPreset(2);
    uint8_t a[NUM_PARAMETERS]; snapshotParams(a);
    loadPreset(5); restoreParams(a);
    uint8_t b[NUM_PARAMETERS]; snapshotParams(b);
    for (int i = 0; i < NUM_PARAMETERS; i++)
        CHECK(abs((int)a[i] - (int)b[i]) <= 1, "param %d round-tripped %d -> %d\n", i, a[i], b[i]);
}

// ═══ 9. Both channels really are independent ═══════════════════════════════
static void testDualChannel() {
    printf("\n-- channel independence --\n");
    loadInitPatch();
    setParameter(C1_WAVE, 0.0f);            // channel I: saw
    setParameter(C2_WAVE, 0.9f);            // channel II: sine
    CHECK(cs.ch[0].wave == CSW_SAW && cs.ch[1].wave == CSW_SINE,
          "waveform did not decode per channel (%d, %d)\n", cs.ch[0].wave, cs.ch[1].wave);

    setParameter(C1_LPF_CUT, 0.2f);
    setParameter(C2_LPF_CUT, 0.9f);
    CHECK(cs.ch[1].lpIdx > cs.ch[0].lpIdx * 2.0f,
          "channel filters are not independent (%.0f vs %.0f)\n", cs.ch[0].lpIdx, cs.ch[1].lpIdx);

    setParameter(C1_PWM_RATE, 0.1f);
    setParameter(C2_PWM_RATE, 0.9f);
    CHECK(cs.ch[1].pwmInc > cs.ch[0].pwmInc * 4,
          "PWM LFOs are not independent per channel\n");

    setParameter(C1_VCA_A, 0.05f);
    setParameter(C2_VCA_A, 0.8f);
    CHECK(cs.ch[0].ampAtk > cs.ch[1].ampAtk * 4.0f,
          "envelopes are not independent per channel\n");
}

// ═══ 10. The Minimoog/Prophet-5 ladder DOES self-oscillate ═════════════════
// The opposite property from the CS-80's own filter above: a real transistor
// ladder gets resonance from positive feedback, so above a threshold it
// sustains its own sine tone with zero input. Below that threshold, silence
// in should still mean silence out.
static void testLadderSelfOscillation() {
    printf("\n-- ladder filter (Minimoog/Prophet-5): self-oscillation --\n");
    // A single-sample kick, then silence: exactly zero input forever would
    // leave a noiseless digital simulation sitting at the unstable
    // equilibrium (zero) forever, which real hardware never does — thermal
    // noise nudges it. This is that nudge.
    LadderFilter f;
    f.setCutoffHz(1000.0f, 1.0f);        // max resonance
    float peak = 0.0f;
    for (int i = 0; i < 4000; i++) {
        float y = f.process((i == 0) ? 1.0f : 0.0f);
        if (i > 500) peak = std::max(peak, fabsf(y));
    }
    CHECK(peak > 0.05f, "max-resonance ladder did not sustain oscillation after a kick (peak %.4f)\n", peak);

    LadderFilter g;
    g.setCutoffHz(1000.0f, 0.3f);         // well below the self-osc threshold
    float peak2 = 0.0f;
    for (int i = 0; i < 4000; i++) {
        float y = g.process((i == 0) ? 1.0f : 0.0f);
        if (i > 500) peak2 = std::max(peak2, fabsf(y));
    }
    CHECK(peak2 < 0.001f, "low-resonance ladder rang on after a single kick (peak %.6f)\n", peak2);
}

// ═══ 11. The ARP 2600/Odyssey filter's damping floor reaches zero ══════════
// In deliberate contrast to the CS-80's Cs80Svf (same TPT topology, damping
// floor that never reaches zero, tested above): this one's floor DOES reach
// (near) zero at max resonance, which is what lets it self-oscillate.
static void testArpSelfOscillation() {
    printf("\n-- ARP 2600/Odyssey filter: damping floor reaches zero --\n");
    A26Svf f;
    f.setCutoffHz(1000.0f, 1.0f);
    CHECK(f.k < 0.05f, "max-resonance A26Svf damping did not approach zero (k=%.4f)\n", f.k);

    float peak = 0.0f;
    for (int i = 0; i < 4000; i++) {
        float lp, bp, hp;
        f.process((i == 0) ? 1.0f : 0.0f, &lp, &bp, &hp);
        if (i > 500) peak = std::max(peak, fabsf(bp));
    }
    CHECK(peak > 0.05f, "max-resonance A26Svf did not sustain oscillation after a kick (peak %.4f)\n", peak);
}

// ═══ 12. Every engine's generic dispatch surface is internally consistent ══
static void testEngineDispatch() {
    printf("\n-- engine dispatch: every mode is safe to select --\n");
    for (uint8_t e = 0; e < NUM_ENGINES; e++) {
        engineSwitchTo((EngineId)e);
        CHECK(currentEngine == e, "engineSwitchTo(%d) did not take\n", e);

        uint8_t n = engineNumParams();
        if (ENGINE_IS_STUB[e]) {
            CHECK(n == 0, "%s: stub engine reports %d params, expected 0\n", ENGINE_NAMES[e], n);
        } else {
            CHECK(n >= ARP_TAIL_COUNT, "%s: %d params is narrower than just the ARP tail\n", ENGINE_NAMES[e], n);
            uint8_t base = engineArpBase();
            CHECK(base + ARP_TAIL_COUNT == n, "%s: ARP tail is not the last %d params\n", ENGINE_NAMES[e], ARP_TAIL_COUNT);
            uint8_t mp = engineMasterParamId();
            CHECK(mp < n, "%s: master param id %d is out of range (%d params)\n", ENGINE_NAMES[e], mp, n);
            CHECK(engineNumVoices() >= 1, "%s: reports zero voices\n", ENGINE_NAMES[e]);
            CHECK(engineNumVoices() <= MAX_ENGINE_VOICES, "%s: %d voices exceeds MAX_ENGINE_VOICES\n", ENGINE_NAMES[e], engineNumVoices());
            // Every declared preset's name must be a real string, and every
            // page's [first, first+count) range must stay inside numParams.
            for (uint8_t p = 0; p < engineNumPresets(); p++)
                CHECK(enginePresetName(p) != nullptr, "%s: preset %d has a null name\n", ENGINE_NAMES[e], p);
            for (uint8_t pg = 0; pg < engineNumPages(); pg++) {
                const EnginePageDef* def = enginePage(pg);
                CHECK((int)def->first + (int)def->count <= n,
                      "%s: page '%s' [%d, %d) runs past %d params\n",
                      ENGINE_NAMES[e], def->name, def->first, def->first + def->count, n);
            }
        }

        // An out-of-range id (as a step lock recorded under a wider engine
        // would produce) must degrade safely, never read out of bounds.
        const ParamInfo* oob = engineParamInfo(250);
        CHECK(oob != nullptr, "%s: out-of-range paramInfo(250) returned null\n", ENGINE_NAMES[e]);
    }
    engineSwitchTo(ENGINE_CS80);   // leave it as the other tests expect
}

// ═══ 15. Filters stay finite even at out-of-range resonance ════════════════
// The panel clamps resonance to 0..1, but the underlying structs don't
// re-clamp it — a caller bug (or a future engine) that passes something
// outside that range must not produce NaN/Inf that would then poison a
// filter's internal state for every sample after.
static void testFilterFiniteUnderExtremeParams() {
    printf("\n-- ladder filter stays finite under out-of-range resonance --\n");
    LadderFilter lf;
    lf.setCutoffHz(20000.0f, 5.0f);      // 5x past the panel's 0..1 range
    bool finite = true;
    for (int i = 0; i < 10000 && finite; i++) {
        float x = (i % 500 == 0) ? 1.0f : (float)((i % 7) - 3) * 0.3f;
        float y = lf.process(x);
        if (!std::isfinite(y)) finite = false;
    }
    CHECK(finite, "LadderFilter produced non-finite output under extreme resonance\n");

    // ARP 2600/Odyssey's A26Svf: same idea, but its resonance floor is
    // allowed to reach true zero by design (see arp2600.h) -- push it well
    // past resonance01==1 instead.
    A26Svf svf;
    svf.setCutoffHz(20000.0f, 5.0f);
    finite = true;
    for (int i = 0; i < 10000 && finite; i++) {
        float x = (i % 500 == 0) ? 1.0f : (float)((i % 7) - 3) * 0.3f;
        float lp, bp, hp;
        svf.process(x, &lp, &bp, &hp);
        if (!std::isfinite(lp) || !std::isfinite(bp) || !std::isfinite(hp)) finite = false;
    }
    CHECK(finite, "A26Svf produced non-finite output under extreme resonance\n");
}

// ═══ 16. Every real engine's voices actually return to the pool ════════════
// Not "sounds right" — assert the render-reported active-voice count reaches
// exactly zero after release, even with every envelope/time parameter maxed
// (the slowest, worst-case release the panel allows). A stuck envelope state
// machine is a permanently-dead voice slot that is nearly impossible to
// notice by ear and easy to notice here.
static void testVoicesReturnToPoolAfterRelease() {
    printf("\n-- every engine's voices return to idle after release --\n");
    for (uint8_t e = 0; e < NUM_ENGINES; e++) {
        if (ENGINE_IS_STUB[e]) continue;
        engineSwitchTo((EngineId)e);
        uint8_t n = engineNumParams();
        for (uint8_t i = 0; i < n; i++) engineSetParameter(i, 1.0f);   // slowest possible envelopes

        uint8_t nv = engineNumVoices();
        for (uint8_t v = 0; v < nv; v++) engineNoteOn(v, 40 + v, 100, true);
        for (uint8_t v = 0; v < nv; v++) engineNoteOff(v, 40 + v);

        int16_t buf[AUDIO_BLOCK * 2];
        int active = -1;
        int blocks = 0;
        for (; blocks < 20000; blocks++) {
            active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
            if (active == 0) break;
        }
        CHECK(active == 0, "%s: still %d active voice(s) after %d blocks (~%.1fs) of release\n",
              ENGINE_NAMES[e], active, blocks, blocks * (float)AUDIO_BLOCK / SAMPLE_RATE_F);
    }
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 17. Voice allocation never exceeds the polyphony ceiling ══════════════
static void testVoiceAllocationCeiling() {
    printf("\n-- voice allocation never exceeds the polyphony ceiling --\n");
    engineSwitchTo(ENGINE_PROPHET5);   // 5 voices — a clean, small ceiling to test against
    va.reset();
    va.limit = engineNumVoices();
    bool ok = true;
    for (int n = 60; n < 60 + 12; n++) {     // ask for 12 notes; only 5 voices exist
        int vi = va.noteOn(n);
        if (vi < 0 || vi >= (int)engineNumVoices()) ok = false;
    }
    CHECK(ok, "voice allocator handed out an index beyond the %d-voice ceiling\n", engineNumVoices());
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 18. Zero-length / edge-case calls do not crash, on any engine ═════════
static void testZeroLengthAndEdgeCases() {
    printf("\n-- zero-length / edge-case calls --\n");
    for (uint8_t e = 0; e < NUM_ENGINES; e++) {
        engineSwitchTo((EngineId)e);
        engineNoteOff(0, -1);                 // note-off with nothing on
        engineAllOff();                      // all-off with nothing on
        engineSetVoiceAftertouch(0, 0);      // aftertouch with nothing on

        int16_t buf[AUDIO_BLOCK * 2];
        int a = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);   // render with nothing playing
        CHECK(a == 0, "%s: renderBlock with nothing playing reported %d active\n", ENGINE_NAMES[e], a);

        // Out-of-range voice indices (as a mis-set voice queue message would
        // produce) must be bounds-checked, not just "usually" in range.
        engineNoteOn(250, 60, 100, true);
        engineNoteOff(250, 60);
        engineSetVoiceAftertouch(250, 100);
    }
    engineSwitchTo(ENGINE_CS80);
    printf("   (no crash across all %d engines x edge cases)\n", (int)NUM_ENGINES);
}

// ═══ 19. Prophet-5/10 UNISON: releasing an earlier note must not silence a
//         later one still held ═════════════════════════════════════════════
// UNISON fans one note across every internal voice (see p5NoteOnE), which
// makes it monophonic — last-note priority, like a MONO_LAST keyboard-style
// track. Bug this guards: play note A, then note B while still holding A
// (B is what's audible now, exactly as real unison hardware behaves), then
// release A. Naively treating every note-off as "silence everything" kills
// B too, even though the player never let go of it.
static void testProphet5UnisonOverlappingNotes() {
    printf("\n-- Prophet-5 UNISON: release of an earlier note doesn't cut a held later one --\n");
    engineSwitchTo(ENGINE_PROPHET5);
    engineLoadPreset(2);   // "UNISON LEAD" — the only stock preset with UNISON on
    CHECK(engineNumVoices() > 0, "Prophet-5 reported zero voices\n");

    engineNoteOn(0, 60, 100, true);
    engineNoteOn(0, 64, 100, true);   // B pressed while A still held -> B is what's sounding
    engineNoteOff(0, 60);             // release A only; B is still down

    int16_t buf[AUDIO_BLOCK * 2];
    int active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active > 0, "releasing the earlier of two overlapping UNISON notes silenced the voice still held\n");

    engineNoteOff(0, 64);   // now release the note that's actually sounding
    int blocks = 0;
    for (; blocks < 20000; blocks++) {
        active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
        if (active == 0) break;
    }
    CHECK(active == 0, "Prophet-5 UNISON voice never returned to idle after the held note was released (%d blocks)\n", blocks);

    engineAllOff();
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 19b. OB-X HOLD / "unison chord": a released chord transposes and
//          replays from a single new key, and does so more than once ═══════
// See obx.h/obx.cpp for the real, documented feature this models: hold a
// chord with HOLD engaged, release it, and the shape is memorized; the next
// single key replays the whole shape transposed to that key as root.
// Bug this guards: a naive implementation might consume the memorized shape
// after one replay, or fail to release every voice a replay triggered.
static void testObxHoldChordTranspose() {
    printf("\n-- OB-X HOLD: a released chord transposes and replays from one key --\n");
    engineSwitchTo(ENGINE_OBX);
    engineLoadPreset(23);   // "CHORD STAB" — the stock preset with HOLD on
    CHECK(engineNumVoices() > 0, "OB-X reported zero voices\n");

    int16_t buf[AUDIO_BLOCK * 2];

    // Hold and release a C major triad -- this is the shape that gets
    // memorized.
    engineNoteOn(0, 60, 100, true);
    engineNoteOn(1, 64, 100, true);
    engineNoteOn(2, 67, 100, true);
    engineNoteOff(0, 60);
    engineNoteOff(1, 64);
    engineNoteOff(2, 67);

    int active = 0, blocks = 0;
    for (; blocks < 20000; blocks++) {
        active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
        if (active == 0) break;
    }
    CHECK(active == 0, "OB-X voices from the recorded chord never returned to idle (%d blocks)\n", blocks);

    // One new key should replay the whole memorized shape transposed to it.
    engineNoteOn(3, 65, 100, true);
    active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active >= 3, "OB-X HOLD: a single key after a captured chord didn't replay all 3 voices (active=%d)\n", active);

    engineNoteOff(3, 65);
    for (blocks = 0; blocks < 20000; blocks++) {
        active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
        if (active == 0) break;
    }
    CHECK(active == 0, "OB-X HOLD: transposed chord voices never returned to idle after release (%d blocks)\n", blocks);

    // The memorized shape must survive being used once -- a second
    // one-finger stab should replay it again, not fall back to a single
    // plain voice.
    engineNoteOn(4, 68, 100, true);
    active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active >= 3, "OB-X HOLD: the memorized chord shape wasn't reusable for a second one-finger stab (active=%d)\n", active);
    engineNoteOff(4, 68);

    engineAllOff();
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 20. String Ensemble's mono bass coupler tracks whichever held note
//         (restricted to the low split) is currently lowest ═══════════════
// See stringens.h file banner point 2 for the real feature this models:
// Cello/Contrabass are monophonic, driven by whichever held note under
// SE_BASS_SPLIT_NOTE is lowest, entirely separate from the polyphonic
// SeVoice pool -- the same held-note-stack technique as this codebase's
// Prophet-5 UNISON/OB-X HOLD. Calling seNoteOnE/seNoteOffE (via the
// generic engine dispatch) with voiceIdx == SE_VOICES -- one past the
// polyphonic pool's own range -- exercises the bass coupler in total
// isolation: seNoteOnE/seNoteOffE only touch the indexed polyphonic voice
// when voiceIdx is in range, but always update the bass coupler regardless
// of voiceIdx, so this is exactly analogous to testObxHoldChordTranspose
// driving OB-X's own engine-level HOLD logic directly.
// Bug this guards: a naive implementation might drop the bass to silence
// when the currently-sounding note releases even though a lower note is
// still held (rather than re-targeting it), or fail to silence it once
// every bass-range note has let go, or wrongly let a note above the split
// engage the bass at all.
static void testStringEnsembleBassCoupler() {
    printf("\n-- String Ensemble bass coupler: tracks the lowest held note --\n");
    engineSwitchTo(ENGINE_STRINGENS);
    engineLoadInitPatch();
    int16_t buf[AUDIO_BLOCK * 2];

    // A note above the split must never engage the bass at all.
    engineNoteOn(SE_VOICES, 60, 100, true);
    int active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active == 0, "String Ensemble: a note above SE_BASS_SPLIT_NOTE engaged the bass coupler (active=%d)\n", active);
    engineNoteOff(SE_VOICES, 60);

    // Hold three bass-range notes out of pitch order; the bass should lock
    // onto the lowest (36) regardless of arrival order.
    engineNoteOn(SE_VOICES, 40, 100, true);
    engineNoteOn(SE_VOICES, 36, 100, true);
    engineNoteOn(SE_VOICES, 43, 100, true);
    active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active >= 1, "String Ensemble: bass coupler didn't engage with 3 bass-range notes held (active=%d)\n", active);

    // Release the currently-assigned lowest note (36) while 40 and 43 are
    // still held -- the bass must re-target 40, not drop to silence.
    engineNoteOff(SE_VOICES, 36);
    active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active >= 1, "String Ensemble: bass coupler went silent when the lowest note released instead of re-targeting the next-lowest held note\n");

    // Release the rest; the bass must actually return to idle, not latch on
    // forever.
    engineNoteOff(SE_VOICES, 40);
    engineNoteOff(SE_VOICES, 43);
    int blocks = 0;
    for (; blocks < 20000; blocks++) {
        active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
        if (active == 0) break;
    }
    CHECK(active == 0, "String Ensemble: bass coupler never returned to idle after every bass-range note released (%d blocks)\n", blocks);

    engineAllOff();
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 21. Master is a pure linear gain after the saturator, not a drive
//         knob into it ═══════════════════════════════════════════════════
// Bug this guards: every real engine used to multiply Master straight into
// the argument of the final tanh saturator, so turning the volume up also
// drove more distortion/compression instead of just getting louder — the
// exact same structural gain-staging bug found (and fixed the same way,
// with a fixed per-engine drive constant + Master as post-FX linear gain)
// in this project's sibling builds. Minimoog is used here because
// mmEngineActivate() fully re-default-constructs MMVoice (including its
// noise generator) on every engine switch, so two fresh switches produce
// bit-identical starting state — a clean before/after comparison, unlike
// engines whose noise/LFO state free-runs across switches.
static void testMasterIsLinearPostSaturationGain() {
    printf("\n-- Master is a pure linear gain, not baked into the saturator --\n");
    auto peakAt = [](uint8_t preset, float master) -> float {
        engineSwitchTo(ENGINE_MINIMOOG);
        engineLoadPreset(preset);
        engineSetParameter(engineMasterParamId(), master);
        va.reset();
        va.limit = engineNumVoices();
        engineNoteOn(0, 48, 127, true);
        int16_t buf[AUDIO_BLOCK * 2];
        float peak = 0.0f;
        int blocks = (int)(1.3f * SAMPLE_RATE_F / AUDIO_BLOCK);
        for (int b = 0; b < blocks; b++) {
            engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
            for (int n = 0; n < AUDIO_BLOCK; n++) {
                float s = fabsf((float)buf[n * 2] / 32000.0f);
                if (s > peak) peak = s;
            }
        }
        engineAllOff();
        return peak;
    };
    for (uint8_t p = 0; p < mmNumPresetsE(); p++) {
        float full = peakAt(p, 1.0f);
        float half = peakAt(p, 0.5f);
        float ratio = (full > 1e-6f) ? (half / full) : 1.0f;
        CHECK(ratio > 0.47f && ratio < 0.53f,
              "%s: Master(0.5)/Master(1.0) peak ratio was %.3f, expected ~0.5 -- "
              "Master may be coupling into the saturator again\n", mmPresetNameE(p), ratio);
    }
    engineSwitchTo(ENGINE_CS80);
}

// ═══ 22. Every "filter flavour" is finite and returns voices to idle, on
//         every engine that offers one ═════════════════════════════════════
// FILT TYPE (variable_filter.h) swaps a voice's filter for one of 15
// alternate models when it isn't STOCK. This exercises all 15 on every
// engine that has the param, at maximum resonance and a cutoff sweep across
// each mode's full stable range (see variable_filter.h's own instability
// writeup) — the same class of check as testFilterFiniteUnderExtremeParams
// and testVoicesReturnToPoolAfterRelease, extended to this new surface.
static int findFilterTypeParam(EngineId e) {
    engineSwitchTo(e);
    uint8_t n = engineNumParams();
    for (uint8_t id = 0; id < n; id++) {
        const ParamInfo* pi = engineParamInfo(id);
        if (pi && pi->name && strcmp(pi->name, "FILT TYPE") == 0) return id;
    }
    return -1;
}
static void testVariableFilterFlavours() {
    printf("\n-- every filter flavour is finite and idles cleanly, on every engine that has one --\n");
    static const EngineId engines[] = {
        ENGINE_MINIMOOG, ENGINE_PROPHET5, ENGINE_PROPHET10,
        ENGINE_ARP2600, ENGINE_ODYSSEY, ENGINE_MELLOTRON,
        ENGINE_OBSEM, ENGINE_JUPITER8, ENGINE_SYSTEM100, ENGINE_MOOGMODULAR,
        ENGINE_OBX, ENGINE_PPGWAVE,
        // DX7 deliberately has no filter of any kind (real hardware doesn't
        // either -- see dx7.h) so it's excluded here, unlike the other
        // three new engines.
        ENGINE_MS20, ENGINE_STRINGENS, ENGINE_PROPHETVS,
    };
    for (EngineId e : engines) {
        int filtId = findFilterTypeParam(e);
        CHECK(filtId >= 0, "%s: no FILT TYPE param found\n", ENGINE_NAMES[e]);
        if (filtId < 0) continue;

        for (uint8_t mode = 0; mode < VF_NUM_MODES; mode++) {
            engineSwitchTo(e);
            uint8_t n = engineNumParams();
            for (uint8_t id = 0; id < n; id++) engineSetParameter(id, 1.0f);   // worst-case resonance/env/etc, matching testFilterFiniteUnderExtremeParams
            engineSetParameter((uint8_t)filtId, ((float)mode + 0.5f) / (float)VF_NUM_MODES);

            uint8_t nv = engineNumVoices();
            for (uint8_t v = 0; v < nv; v++) engineNoteOn(v, 45 + v, 100, true);

            // 2000 blocks (~2.7s) with every param maxed and this mode's
            // cutoff/resonance pushed as hard as the panel allows: if a
            // mode's stability margin (see variable_filter.h) were wrong,
            // ASan/UBSan would already abort the whole test binary here —
            // reaching the release check below is itself part of what this
            // proves, not just decoration around it.
            int16_t buf[AUDIO_BLOCK * 2];
            for (int b = 0; b < 2000; b++) engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);

            for (uint8_t v = 0; v < nv; v++) engineNoteOff(v, 45 + v);
            int active = -1, blocks = 0;
            for (; blocks < 20000; blocks++) {
                active = engineRenderBlock(buf, 0.0f, 0.0f, 0.0f);
                if (active == 0) break;
            }
            CHECK(active == 0, "%s FILT TYPE=%s (%d): still %d active voice(s) after %d blocks of release\n",
                  ENGINE_NAMES[e], VAR_FILTER_NAMES[mode], mode, active, blocks);
        }
    }
    engineSwitchTo(ENGINE_CS80);
    printf("   (%d engines x %d filter flavours, all finite and idling cleanly)\n", (int)(sizeof(engines)/sizeof(engines[0])), (int)VF_NUM_MODES);
}


// ── DX7 (Dexed/MSFA) ─────────────────────────────────────────────────────────
// Renders one note on the currently-selected DX7 voice and returns the peak
// absolute sample.  Deliberately drains the release tail afterwards, so the
// NEXT voice measured cannot be flattered by this one still ringing.
static float dx7PeakOfCurrentVoice(int note, int vel, int blocks) {
    int16_t buf[AUDIO_BLOCK * 2];
    dxAllOffE();
    for (int b = 0; b < 8; b++) dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    dxNoteOnE(0, note, (uint8_t)vel, true);
    float peak = 0.0f;
    for (int b = 0; b < blocks; b++) {
        dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
        for (int i = 0; i < AUDIO_BLOCK * 2; i++) {
            float a = fabsf((float)buf[i]);
            if (a > peak) peak = a;
        }
    }
    dxNoteOffE(0);
    for (int b = 0; b < 40; b++) dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    return peak;
}

// THE regression test for this engine.
//
// MSFA's Env::update() jumps a held note's envelope straight to its sustain
// level ("for now we simply reset ourselves at level 3", upstream, verbatim),
// and Dexed runs that refresh at the top of getSamples() whenever a voice load
// or a parameter write has armed it.  A note started in that window therefore
// skips its attack and decay and BEGINS at its sustain level -- which for
// every percussive DX7 patch (piano, bass, clav, harpsichord, bell, pluck) is
// zero, i.e. the note is silent.
//
// Before DxAdapter::settle() this was 93 of the 256 factory voices producing
// no output whatsoever.  If this test ever starts failing again, that is what
// has come back.
static void testDx7EveryRomVoiceSounds() {
    printf("\n-- DX7: every one of the 256 factory ROM voices actually sounds --\n");
    engineSwitchTo(ENGINE_DX7);
    dxLoadInitPatchE();          // known output/FX/master state, whatever ran before
    int silent = 0, nonFinite = 0;
    float lo = 1e30f, hi = 0.0f; double total = 0.0;
    for (int bank = 0; bank < DX7_ROM_BANKS; bank++) {
        for (int prog = 0; prog < DX7_VOICES_PER_BANK; prog++) {
            dxSelectRomVoice((uint8_t)bank, (uint8_t)prog);
            float peak = dx7PeakOfCurrentVoice(60, 110, 400);
            if (!isfinite(peak)) { nonFinite++; continue; }
            if (peak <= 0.0f) {
                silent++;
                if (silent <= 4)
                    printf("   SILENT: %s voice %d '%s'\n",
                           DX7_ROM_BANK_NAMES[bank], prog + 1, dxCurrentVoiceName());
            }
            if (peak < lo) lo = peak;
            if (peak > hi) hi = peak;
            total += peak;
        }
    }
    CHECK(nonFinite == 0, "%d DX7 ROM voices rendered a non-finite peak\n", nonFinite);
    CHECK(silent == 0, "%d of 256 DX7 ROM voices are silent\n", silent);
    // Nothing should be pinned against the 32000 full-scale ceiling either:
    // an engine trim hot enough to slam every patch into the saturator would
    // pass the "not silent" check while throwing away the library's dynamics.
    CHECK(hi < 30000.0f, "loudest DX7 ROM voice peaks at %.0f -- the engine trim is too hot\n",
          (double)hi);
    printf("   (256 voices, %d silent; peak min %.0f / mean %.0f / max %.0f of 32000)\n",
           silent, (double)lo, total / 256.0, (double)hi);
}

// The panel says OP1..OP6; Dexed indexes operators in DATA order, which starts
// at OP6.  Getting that reversal wrong edits the wrong operator and is
// completely silent about it -- the patch just sounds wrong.  Pin it down by
// muting one operator at a time and checking that the level the panel reports
// back is the one the panel just set.
static void testDx7OperatorNumbering() {
    printf("\n-- DX7: panel OP1..OP6 map to the operators the DX7 calls OP1..OP6 --\n");
    engineSwitchTo(ENGINE_DX7);
    dxSelectRomVoice(0, 0);
    // Panel rows 2..7 are OP1 LEVEL..OP6 LEVEL (see DxParamId).
    const uint8_t OP1_LEVEL = 2;
    for (int op = 0; op < 6; op++) {
        dxSetParameterE((uint8_t)(OP1_LEVEL + op), 0.0f);
        char buf[16];
        dxFormatParameterE((uint8_t)(OP1_LEVEL + op), buf, sizeof(buf));
        CHECK(strcmp(buf, "0") == 0, "OP%d LEVEL reads back '%s', not 0\n", op + 1, buf);
    }
    // With every operator silenced there can be no output, whichever algorithm
    // the voice uses -- if the mapping were off by anything, some operator
    // would still be at its ROM level and still sounding.
    float peak = dx7PeakOfCurrentVoice(60, 110, 200);
    CHECK(peak == 0.0f, "all six operator levels at 0 still produced %.0f\n", (double)peak);
    dxSelectRomVoice(0, 0);   // put the voice back
}

// A DX7 preset here IS a factory cartridge voice, so the preset list has to
// name real patches, and loading one has to leave that patch actually loaded.
static void testDx7PresetsAreRomVoices() {
    printf("\n-- DX7: presets are the factory cartridge voices --\n");
    engineSwitchTo(ENGINE_DX7);
    CHECK(dxNumPresetsE() == 128, "expected 128 DX7 presets, got %d\n", (int)dxNumPresetsE());
    for (uint8_t i = 0; i < dxNumPresetsE(); i++) {
        const char* n = dxPresetNameE(i);
        CHECK(n && n[0] != '\0' && n[0] != ' ', "preset %d has an empty name\n", (int)i);
    }
    // ROM1A #1 is BRASS 1 -- the voice a DX7 powers up on, and this engine's
    // init patch.
    dxLoadInitPatchE();
    CHECK(strncmp(dxCurrentVoiceName(), "BRASS", 5) == 0,
          "init patch loaded '%s', expected BRASS 1\n", dxCurrentVoiceName());
    CHECK(dx7PeakOfCurrentVoice(60, 110, 300) > 0.0f, "the init patch is silent\n");
}

// The voice allocator hands out slot indices and reuses the slot already
// holding a pitch; Dexed allocates internally, by pitch.  dxSlotNote[] is the
// bridge, and the failure mode if it is wrong is a note that never stops.
static void testDx7NotesReleaseAndSlotsReuse() {
    printf("\n-- DX7: notes release, and a reused slot doesn't strand one --\n");
    engineSwitchTo(ENGINE_DX7);
    dxLoadInitPatchE();
    int16_t buf[AUDIO_BLOCK * 2];

    dxNoteOnE(0, 60, 110, true);
    for (int b = 0; b < 60; b++) dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    dxNoteOffE(0);
    int active = 0;
    for (int b = 0; b < 1200; b++) active = dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active == 0, "a released DX7 note was still playing after 1.6 s (%d live)\n", active);

    // Same slot, a different pitch, with no note-off in between: the old pitch
    // must be released or Dexed holds it forever.
    dxNoteOnE(0, 60, 110, true);
    for (int b = 0; b < 20; b++) dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    dxNoteOnE(0, 67, 110, true);
    for (int b = 0; b < 20; b++) dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    dxNoteOffE(0);
    for (int b = 0; b < 1200; b++) active = dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active == 0, "reusing a DX7 slot stranded a note (%d still live)\n", active);

    dxAllOffE();
    for (int b = 0; b < 400; b++) active = dxRenderBlockE(buf, 0.0f, 0.0f, 0.0f);
    CHECK(active == 0, "dxAllOffE left %d DX7 notes playing\n", active);
}

// A saved patch has to come back as the same patch.  For this engine that
// means the cartridge voice AND the edits made on top of it -- which only
// works because BANK/VOICE are parameter ids 0 and 1, so the restore loop
// installs the voice before replaying the edits over it.
static void testDx7SnapshotRestoresVoiceAndEdits() {
    printf("\n-- DX7: snapshot/restore keeps both the voice and the edits on it --\n");
    engineSwitchTo(ENGINE_DX7);
    dxSelectRomVoice(2, 17);                       // some voice that isn't the default
    char saved[16];
    snprintf(saved, sizeof(saved), "%s", dxCurrentVoiceName());
    dxSetParameterE(4, 0.25f);                     // OP3 LEVEL, an edit on top

    uint8_t snap[MAX_ENGINE_PARAMS];
    dxSnapshotParamsE(snap);

    dxLoadInitPatchE();                            // wander off
    CHECK(strcmp(dxCurrentVoiceName(), saved) != 0, "the test didn't actually change voice\n");

    dxRestoreParamsE(snap);
    CHECK(strcmp(dxCurrentVoiceName(), saved) == 0,
          "restore loaded '%s', expected '%s'\n", dxCurrentVoiceName(), saved);
    char buf[16];
    dxFormatParameterE(4, buf, sizeof(buf));
    CHECK(strcmp(buf, "24") == 0, "the edit on top of the voice came back as '%s', not 24\n", buf);
}

// BANK's live range beyond the 8 ROM banks is voicebank.cpp's territory
// (real FatFS, not host-testable -- see tests/syntax_check.sh), reached
// through the dxExt{BankCount,BankName,GetVoice} hooks in dx7.h. Those hooks
// themselves ARE host-testable: wire small fakes to them here and drive BANK
// exactly the way turning the front-panel encoder or a MIDI CC would.
static void testDx7ExtensionBanksWireUpSafely() {
    printf("\n-- DX7: filesystem extension banks reach through cleanly, and stay unreachable until wired --\n");
    engineSwitchTo(ENGINE_DX7);
    dxSelectRomVoice(0, 0);                          // known starting point: ROM1A BRASS 1
    char before[16];
    snprintf(before, sizeof(before), "%s", dxCurrentVoiceName());

    // BANK's raw encoding is 0..1 over DX7_ROM_BANKS+MAX_FS_BANKS-1 positions
    // (dx7.cpp's DX_MAX_TOTAL_BANKS) regardless of how many actually resolve
    // right now -- see that constant's own comment for why. Bank index
    // DX7_ROM_BANKS (0-based) is the first position past ROM.
    const float firstExtRaw = (float)DX7_ROM_BANKS / (float)(DX7_ROM_BANKS + MAX_FS_BANKS - 1);

    // 1. Nothing wired up (every test before this one ran that way too): the
    // voice must not change when BANK is driven past the ROM range.
    dxSetParameterE(0, firstExtRaw);   // 0 = DX_BANK, the first panel row
    CHECK(strcmp(dxCurrentVoiceName(), before) == 0,
          "BANK reached an extension bank with nothing wired up (voice became '%s')\n",
          dxCurrentVoiceName());

    // 2. Wire a fake one-bank extension. Hands back a REAL ROM voice's bytes
    // (not synthetic data) so a correct install is verifiable by name, same
    // as every other DX7 test here.
    static uint8_t fakeVoice[DX7_PACKED_VOICE];
    memcpy(fakeVoice, DX7_ROM[3][5], DX7_PACKED_VOICE);
    char expectName[11];
    for (int i = 0; i < 10; i++) {
        uint8_t c = DX7_ROM[3][5][DX7_VOICE_NAME_OFF + i];
        expectName[i] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    expectName[10] = '\0';

    dxExtBankCount = []() -> uint8_t { return 1; };
    dxExtBankName  = [](uint8_t, char* out, size_t n) { snprintf(out, n, "USB BANK 1"); };
    dxExtGetVoice  = [](uint8_t bank, uint8_t, uint8_t* out) -> bool {
        if (bank != 0) return false;
        memcpy(out, fakeVoice, DX7_PACKED_VOICE);
        return true;
    };

    dxSetParameterE(0, firstExtRaw);   // re-apply now that the extension exists
    CHECK(strcmp(dxCurrentVoiceName(), expectName) == 0,
          "extension bank 0 loaded '%s', expected '%s'\n", dxCurrentVoiceName(), expectName);
    char bankLabel[16];
    dxFormatParameterE(0, bankLabel, sizeof(bankLabel));
    CHECK(strcmp(bankLabel, "USB BANK 1") == 0,
          "BANK's own label reads '%s', expected the extension's name\n", bankLabel);

    // 3. A position past what's actually populated (only 1 bank exists here)
    // is a dead zone: selecting it must leave the currently-loaded voice
    // alone, not install silence or garbage.
    const float secondExtRaw = (float)(DX7_ROM_BANKS + 1) / (float)(DX7_ROM_BANKS + MAX_FS_BANKS - 1);
    dxSetParameterE(0, secondExtRaw);
    CHECK(strcmp(dxCurrentVoiceName(), expectName) == 0,
          "an unpopulated extension slot still changed the voice (now '%s')\n", dxCurrentVoiceName());

    // Leave the hooks null again -- every test's starting assumption,
    // including this one's own step 1.
    dxExtBankCount = nullptr;
    dxExtBankName  = nullptr;
    dxExtGetVoice  = nullptr;
    dxSelectRomVoice(0, 0);
}

int main() {
    dspTablesInit();
    paramsInit();
    seqNoteOn = tOn; seqNoteOff = tOff; seqLock = tLock;
    printf("=== Carpenter Synth (multi-engine, %d engines) host tests ===\n", (int)NUM_ENGINES);
    testTables();
    testEffects();
    testSequencer();
    testNoSelfOscillation();
    testQFallsWithCutoff();
    testSawGlitch();
    testFilterEnvelope();
    testHzPerVolt();
    testCutoffRange();
    testPresets();
    testPanel();
    testDualChannel();
    testLadderSelfOscillation();
    testArpSelfOscillation();
    testEngineDispatch();
    testFilterFiniteUnderExtremeParams();
    testVoicesReturnToPoolAfterRelease();
    testVoiceAllocationCeiling();
    testZeroLengthAndEdgeCases();
    testProphet5UnisonOverlappingNotes();
    testObxHoldChordTranspose();
    testStringEnsembleBassCoupler();
    testMasterIsLinearPostSaturationGain();
    testVariableFilterFlavours();
    testDx7EveryRomVoiceSounds();
    testDx7OperatorNumbering();
    testDx7PresetsAreRomVoices();
    testDx7NotesReleaseAndSlotsReuse();
    testDx7SnapshotRestoresVoiceAndEdits();
    testDx7ExtensionBanksWireUpSafely();
    printf("\n%s  (%d failure%s)\n", failures ? "FAILED" : "ALL PASS",
           failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}

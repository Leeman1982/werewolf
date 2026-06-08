#ifndef SYNTH_ENGINE_H
#define SYNTH_ENGINE_H

#include <Arduino.h>
#include <driver/i2s.h>
#include <freertos/semphr.h>
#include <math.h>
#include "synth_patch.h"

// Prophet-8 style synthesizer engine with band-limited wavetable oscillators.
// BLOscillator replaces the old polyBLEP oscillator – interface is identical.
// Supports 8-voice polyphony with full ADSR, filter, and modulation.
// FreeRTOS mutex guards all parameter writes vs the audio render loop.

#define MAX_VOICES   8
#define SAMPLE_RATE  44100
#define BUFFER_SIZE  256
#define TWO_PI       6.28318530718f
#define MIDI_NOTE_COUNT 128

// Audio output – PCM5052 DAC via I2S
#define I2S_NUM      I2S_NUM_0
#define I2S_BCK_PIN  22
#define I2S_WS_PIN   27
#define I2S_DATA_PIN 17

// ── Wavetable dimensions ──────────────────────────────────────────────────────
// 3 waveforms × 8 tiers × 1024 samples = 98 304 bytes of DRAM.
// Doubled from 512 to reduce linear-interpolation error and increase quality.
// SINE uses sinf() directly; PULSE uses polyBLEP.
#define BL_TABLE_SIZE  1024
#define BL_NUM_TABLES  8

// ── Sequencer voice partitioning ──────────────────────────────────────────────
// The 8-voice pool is split evenly across the sequencer's tracks so each track
// owns a private set of voices: tracks can never steal or cut each other off,
// and a note-off on one track never silences a same-pitch note on another.
#define SEQ_TRACKS        4
#define VOICES_PER_TRACK  (MAX_VOICES / SEQ_TRACKS)   // = 2

// ── Branchless clamp helpers (no compare-and-branch on ESP32) ────────────────
static inline float fclamp(float v, float lo, float hi) {
    return fmaxf(lo, fminf(hi, v));
}

// ── State-variable filter stability bounds ───────────────────────────────────
// Chamberlin SVF goes unstable as f → 2 and damping q → 0.  Keep a safety
// margin so stacked voices (chords) can't self-oscillate into a screech.
#define SVF_FMAX 1.85f   // max integrator coefficient (was 1.92, too close to edge)
#define SVF_QMIN 0.06f   // min damping → caps maximum resonance (was 0.04)

// ── Master soft-clipper (always on) ──────────────────────────────────────────
// Cubic waveshaper: transparent below ~0.6, smooth knee, hard-bounded to ±1 at
// |x| ≥ 1.5.  Replaces the old brick-wall fclamp on the output so that loud
// chords compress gracefully instead of producing harsh digital clipping.
static inline float softClip(float x) {
    if (x >  1.5f) return  1.0f;
    if (x < -1.5f) return -1.0f;
    return x - 0.14814814814f * x * x * x;   // x - x^3/6.75
}

// ── Enumerations ──────────────────────────────────────────────────────────────
enum WaveformType {
    WAVE_SAW,
    WAVE_SQUARE,
    WAVE_TRIANGLE,
    WAVE_SINE,
    WAVE_PULSE
};

enum FilterType {
    FILTER_LOWPASS,
    FILTER_HIGHPASS,
    FILTER_BANDPASS,
    FILTER_NOTCH
};

enum EnvelopeState {
    ENV_IDLE,
    ENV_ATTACK,
    ENV_DECAY,
    ENV_SUSTAIN,
    ENV_RELEASE
};

// ── Band-limited wavetable bank ───────────────────────────────────────────────
// Initialised once in SynthEngine::init(); defined in BL_Oscillator_ESP32.cpp.
class BLWavetables {
public:
    static float tables[3][BL_NUM_TABLES][BL_TABLE_SIZE];
    static float topFreqs[BL_NUM_TABLES];   // max alias-free frequency per tier
    static bool  initialized;

    static void init(float sampleRate);
private:
    static void buildTable(int waveIdx, int tableIdx, int numHarmonics);
};

// ── polyBLEP helper (still used for WAVE_PULSE) ───────────────────────────────
inline float polyBlep(float t, float dt) {
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    } else if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

// ── Band-limited oscillator ───────────────────────────────────────────────────
// SAW / SQUARE / TRIANGLE use pre-built wavetables (alias-free, ~40% less CPU).
// SINE  → direct sinf().
// PULSE → polyBLEP (variable pulse width can't use a fixed table).
// _cachedTableIdx avoids re-scanning topFreqs every sample for sustained notes.
struct BLOscillator {
    WaveformType waveform;
    // Q32 fixed-point phase: wraps modulo 2^32 for free (no compare/branch).
    // Top 10 bits = table index (1024-entry table), lower 22 bits = fraction.
    uint32_t     phaseQ;
    uint32_t     phaseIncQ;   // (freq / SR) * 2^32, recomputed in setFrequency.
    float        frequency;
    float        pulseWidth;
    int          _cachedTableIdx;
    bool         _tableCacheDirty;

    void init(WaveformType wave) {
        waveform          = wave;
        phaseQ            = 0;
        phaseIncQ         = 0;
        frequency         = 440.0f;
        pulseWidth        = 0.5f;
        _cachedTableIdx   = 0;
        _tableCacheDirty  = true;
        setFrequency(frequency);
    }

    void setFrequency(float freq) {
        float f = (freq > 0.0f) ? freq : 1.0f;
        if (f != frequency) {
            frequency        = f;
            _tableCacheDirty = true;
        }
        // Q32 step: (freq / SR) * 2^32.  Single mul, no per-sample compare.
        phaseIncQ = (uint32_t)(f * (4294967296.0f / (float)SAMPLE_RATE));
    }

    void setPulseWidth(float pw) {
        pulseWidth = constrain(pw, 0.01f, 0.99f);
    }

    void reset() {
        phaseQ           = 0;
        _tableCacheDirty = true;
    }

    // Defined in BL_Oscillator_ESP32.cpp (IRAM_ATTR for fast I-cache access)
    float process();
};

// ── ADSR envelope (exponential / analog-style) ───────────────────────────────
// Linear segments are the classic "cheap digital" giveaway — they click and
// sound synthetic.  This uses the earlevel one-pole exponential ADSR: each
// stage charges/discharges toward a target through a per-sample coefficient,
// giving the natural curved shape of an analog RC envelope.  Per-sample cost is
// one multiply + one add (no more than the old linear version).
struct Envelope {
    float attack, decay, sustain, release;
    EnvelopeState state;
    float level;
    float attackCoef,  decayCoef,  releaseCoef;
    float attackBase,  decayBase,  releaseBase;

    // Target ratios set the curvature.  ~0.3 gives a punchy convex attack;
    // a tiny value makes decay/release a near-pure exponential tail.
    static constexpr float TR_ATTACK = 0.3f;
    static constexpr float TR_DR     = 0.0001f;

    static float calcCoef(float rateSamples, float targetRatio) {
        if (rateSamples <= 0.0f) return 0.0f;   // instant stage
        return expf(-logf((1.0f + targetRatio) / targetRatio) / rateSamples);
    }

    void init(float a, float d, float s, float r) {
        attack  = a;  decay   = d;
        sustain = fclamp(s, 0.0f, 1.0f);  release = r;
        state   = ENV_IDLE;
        level   = 0.0f;

        attackCoef  = calcCoef(a * SAMPLE_RATE, TR_ATTACK);
        attackBase  = (1.0f + TR_ATTACK) * (1.0f - attackCoef);
        decayCoef   = calcCoef(d * SAMPLE_RATE, TR_DR);
        decayBase   = (sustain - TR_DR) * (1.0f - decayCoef);
        releaseCoef = calcCoef(r * SAMPLE_RATE, TR_DR);
        releaseBase = -TR_DR * (1.0f - releaseCoef);
    }

    void noteOn()  { state = ENV_ATTACK; }
    void noteOff() { if (state != ENV_IDLE) state = ENV_RELEASE; }

    // IRAM_ATTR: hot per-sample function, pinned in instruction SRAM.
    IRAM_ATTR float process() {
        switch (state) {
            case ENV_ATTACK:
                level = attackBase + level * attackCoef;
                if (level >= 1.0f) { level = 1.0f; state = ENV_DECAY; }
                break;
            case ENV_DECAY:
                level = decayBase + level * decayCoef;
                if (level <= sustain) { level = sustain; state = ENV_SUSTAIN; }
                break;
            case ENV_SUSTAIN:
                level = sustain;
                break;
            case ENV_RELEASE:
                level = releaseBase + level * releaseCoef;
                if (level <= 0.0f) { level = 0.0f; state = ENV_IDLE; }
                break;
            case ENV_IDLE:
                level = 0.0f;
                break;
        }
        return level;
    }

    bool isActive() { return state != ENV_IDLE; }
};

// ── State-variable filter (Chamberlin topology) ───────────────────────────────
struct Filter {
    FilterType type;
    float cutoff;       // base cutoff, 0.0–1.0 normalised
    float resonance;    // 0.0–1.0
    float lp, bp, hp;
    float f, q;
    // Cached modulated coefficient (see processWithMod): avoids recomputing
    // powf/sinf every sample while the filter envelope/LFO sweeps smoothly.
    float _lastModC;
    float _fModCache;

    void init(FilterType t, float freq, float res) {
        type      = t;
        cutoff    = constrain(freq, 0.0f, 1.0f);
        resonance = constrain(res,  0.0f, 1.0f);
        lp = bp = hp = 0.0f;
        _lastModC  = -1.0f;   // sentinel forces first processWithMod to compute
        _fModCache = 0.0f;
        updateCoefficients();
    }

    void updateCoefficients() {
        // Logarithmic mapping: 0→20 Hz, 0.5→630 Hz, 1.0→20 kHz
        // Gives perceptually even sweep across the audible spectrum.
        float fc = 20.0f * powf(1000.0f, cutoff);
        f = 2.0f * sinf(PI * fc / (float)SAMPLE_RATE);
        f = constrain(f, 0.001f, SVF_FMAX);  // stay well inside stability bound
        // q: damping – SVF_QMIN = max resonance, 1.0 = no resonance
        q = constrain(1.0f - resonance * 0.94f, SVF_QMIN, 1.0f);
        _lastModC = -1.0f;   // base moved → invalidate the modulated cache
    }

    void setCutoff(float freq) {
        cutoff = constrain(freq, 0.0f, 1.0f);
        updateCoefficients();
    }

    void setResonance(float res) {
        resonance = constrain(res, 0.0f, 1.0f);
        updateCoefficients();
    }

    // Standard process – uses the static f/q coefficients.
    IRAM_ATTR float process(float input) {
        lp += f * bp;
        hp  = input - lp - q * bp;
        bp += f * hp;
        // Branchless saturation – ARM emits min/max ops with no jumps.
        lp = fclamp(lp, -2.0f, 2.0f);
        bp = fclamp(bp, -2.0f, 2.0f);
        hp = fclamp(hp, -2.0f, 2.0f);
        switch (type) {
            case FILTER_LOWPASS:  return lp;
            case FILTER_HIGHPASS: return hp;
            case FILTER_BANDPASS: return bp;
            case FILTER_NOTCH:    return lp + hp;
            default: return input;
        }
    }

    // Modulated process – applies a one-shot cutoff offset without drifting
    // the stored base cutoff.  Called from Voice::process() with the filter-
    // envelope contribution so the base never accumulates.  The expensive
    // powf+sinf coefficient build only runs when cutoffMod actually moves;
    // otherwise we reuse the already-up-to-date `f` baked by updateCoefficients.
    //
    // NOTE: deliberately NOT IRAM_ATTR.  This function carries a literal pool
    // (PI, SR, 20, 1000, etc.) that the Xtensa linker can't reach from IRAM
    // ("dangerous relocation: l32r literal placed after use").  Flash + I-cache
    // is plenty fast for this path because the slow branch (powf/sinf) only
    // fires when cutoffMod is actually moving; the fast branch is one mul +
    // a few adds and stays hot in I-cache.
    float processWithMod(float input, float cutoffMod) {
        float fMod;
        if (fabsf(cutoffMod) < 1e-4f) {
            fMod = f;
        } else {
            float modCutoff = fclamp(cutoff + cutoffMod, 0.0f, 1.0f);
            // Recompute the expensive powf/sinf coefficient only when the
            // modulated cutoff actually moves by a perceptible amount.  During
            // sustain and smooth envelope/LFO sweeps this collapses ~44 100
            // transcendental calls/sec per voice down to a handful — the key
            // fix for audio dropouts when several voices (chords) are active.
            if (fabsf(modCutoff - _lastModC) > 0.0015f) {
                float fc   = 20.0f * powf(1000.0f, modCutoff);
                _fModCache = fclamp(2.0f * sinf(PI * fc / (float)SAMPLE_RATE), 0.001f, SVF_FMAX);
                _lastModC  = modCutoff;
            }
            fMod = _fModCache;
        }
        lp += fMod * bp;
        hp  = input - lp - q * bp;
        bp += fMod * hp;
        lp = fclamp(lp, -2.0f, 2.0f);
        bp = fclamp(bp, -2.0f, 2.0f);
        hp = fclamp(hp, -2.0f, 2.0f);
        switch (type) {
            case FILTER_LOWPASS:  return lp;
            case FILTER_HIGHPASS: return hp;
            case FILTER_BANDPASS: return bp;
            case FILTER_NOTCH:    return lp + hp;
            default: return input;
        }
    }
};

// ── Synth voice (Prophet style – 2 oscillators) ───────────────────────────────
struct Voice {
    int           note;
    int           velocity;
    bool          active;
    unsigned long noteOnTime;

    BLOscillator  osc1;
    BLOscillator  osc2;
    BLOscillator  subOsc;
    Filter        filter;
    Envelope      ampEnv;
    Envelope      filterEnv;

    // Per-voice parameters updated by SynthEngine setters
    float osc1Level;        // 0.0–1.0 mix level for osc1
    float osc2Level;        // 0.0–1.0 mix level for osc2
    float subLevel;         // 0.0–1.0 mix level for sub osc (default 0 = silent)
    float _subTrim;         // cached 1/(1 + subLevel*0.6), refreshed on subLevel change
    int   subOctave;        // 1 or 2 octaves below note
    float osc2Detune;       // frequency offset ratio for osc2 (e.g. 0.006 → freq × 1.006)
    float filterEnvAmount;  // how much the filter envelope opens the cutoff
    float baseFreq;         // fundamental frequency without pitch-bend
    // Stereo panning gains (equal-power). 0.707 each = centred.
    float gainL;
    float gainR;
    // Per-note (P-Lock) filter cutoff offset applied in process().  0 = no lock.
    float cutoffLock;
    // Velocity → filter brightness (expressive "play harder = brighter").
    float velFilter;        // sensitivity, 0 = off
    float _velCutoff;       // cached velFilter * velocity, added to the cutoff mod

    inline void setSubLevelCached(float l) {
        subLevel = fclamp(l, 0.0f, 1.0f);
        _subTrim = 1.0f / (1.0f + subLevel * 0.6f);
    }

    void init() {
        note      = -1;
        velocity  = 0;
        active    = false;
        noteOnTime = 0;

        osc1.init(WAVE_SAW);
        osc2.init(WAVE_SAW);
        subOsc.init(WAVE_SQUARE);
        filter.init(FILTER_LOWPASS, 0.8f, 0.3f);
        ampEnv.init(0.01f, 0.3f, 0.7f, 0.5f);
        filterEnv.init(0.01f, 0.3f, 0.5f, 0.3f);

        osc1Level       = 0.5f;
        osc2Level       = 0.5f;
        setSubLevelCached(0.0f);
        subOctave       = 1;
        osc2Detune      = 0.005f;
        filterEnvAmount = 0.3f;
        baseFreq        = 440.0f;
        gainL           = 0.7071f;
        gainR           = 0.7071f;
        cutoffLock      = 0.0f;
        velFilter       = 0.12f;
        _velCutoff      = 0.0f;
    }

    void noteOn(int n, int vel) {
        note       = n;
        velocity   = vel;
        active     = true;
        noteOnTime = millis();

        // Analog-style pitch drift: a tiny per-note random detune (±~2 cents)
        // baked into baseFreq so no two notes are perfectly identical — the
        // subtle imperfection that separates "warm" from "sterile digital".
        uint32_t r = ((uint32_t)random(0, 65536) << 16) | (uint32_t)random(0, 65536);
        float drift = ((float)(r & 0xFFFF) - 32768.0f) * (0.0012f / 32768.0f);
        baseFreq = (440.0f * powf(2.0f, (n - 69.0f) / 12.0f)) * (1.0f + drift);

        // Free-running / randomised start phase (different per oscillator) rather
        // than a hard reset to 0.  Kills the identical-attack "machine-gun" click
        // and decorrelates osc1/osc2 so the detune beats from the very first
        // sample.  The exponential attack envelope keeps note onsets click-free.
        osc1.phaseQ   = r;
        osc2.phaseQ   = r * 2654435761u + 1u;
        subOsc.phaseQ = r * 40503u + 12345u;
        osc1.setFrequency(baseFreq);
        osc2.setFrequency(baseFreq * (1.0f + osc2Detune));
        // Sub-osc: 1 or 2 octaves below
        int subOct = (subOctave < 1) ? 1 : (subOctave > 2 ? 2 : subOctave);
        subOsc.setFrequency(baseFreq * (subOct == 2 ? 0.25f : 0.5f));

        // Cache velocity → cutoff contribution (brighten-only).
        _velCutoff = velFilter * (vel * (1.0f / 127.0f));

        ampEnv.noteOn();
        filterEnv.noteOn();
    }

    void noteOff() {
        ampEnv.noteOff();
        filterEnv.noteOff();
    }

    // filterLFOMod: normalized cutoff addition from LFO (-1..+1 * depth)
    // ampLFOMod:    0-centred amplitude scale offset from LFO
    IRAM_ATTR float process(float filterLFOMod = 0.0f, float ampLFOMod = 0.0f) {
        if (!active) return 0.0f;

        float oscMix = osc1.process() * osc1Level + osc2.process() * osc2Level;
        // Sub-osc is fully skipped when its level is below epsilon — zero CPU
        // cost when the patch doesn't use it.  Cached _subTrim avoids a div
        // per sample (was: 1.0f / (1.0f + subLevel * 0.6f)).
        if (subLevel > 1e-3f) {
            oscMix += subOsc.process() * subLevel;
            oscMix *= _subTrim;
        }
        float envMod   = filterEnv.process() * filterEnvAmount
                       + filterLFOMod + cutoffLock + _velCutoff;
        float filtered = filter.processWithMod(oscMix, envMod);

        float ampEnvOut = ampEnv.process() * (velocity * (1.0f / 127.0f));
        // Amp LFO multiplier centred at 1.0 (ampLFOMod is -depth..+depth)
        ampEnvOut *= fclamp(1.0f + ampLFOMod, 0.0f, 2.0f);

        float output = filtered * ampEnvOut;

        if (!ampEnv.isActive()) active = false;

        return output;
    }
};

// ── Analog saturation (tanh soft-clipper) ─────────────────────────────────────
// Drive and normalisation factor pre-computed so per-sample cost is one tanhf.
struct AnalogSaturation {
    float _amount;
    float _driveFactor;
    float _normFactor;

    void init() {
        _amount      = 0.0f;
        _driveFactor = 1.0f;
        _normFactor  = 1.0f / tanhf(1.0f);
    }

    void setDrive(float d) {
        _driveFactor = 1.0f + constrain(d, 0.0f, 1.0f) * 4.0f;
        _normFactor  = 1.0f / tanhf(_driveFactor);
    }

    void setAmount(float a) {
        _amount = constrain(a, 0.0f, 1.0f);
    }

    inline float apply(float in) const {
        if (_amount < 0.001f) return in;
        return in + (tanhf(_driveFactor * in) * _normFactor - in) * _amount;
    }
};

// ── BPM-synced stereo-spread delay (mono in, stereo out) ─────────────────────
// Tape-style: read tap at `delaySamples`, feed back into the write head.
// L tap is straight; R tap is offset by 7 ms for natural stereo spread.
// `setTimeFromBPM(bpm, divIdx)` snaps the tap to musical divisions
// (0:1/4, 1:1/8d, 2:1/8, 3:1/16).
#define DELAY_BUF_SIZE 8192   // 32 KB at float; 186 ms @ 44.1 kHz mono ring
struct DelayFX {
    float    buf[DELAY_BUF_SIZE];
    uint32_t wPos;
    int      delaySamples;    // current tap distance (samples behind write)
    float    feedback;        // 0..0.85
    float    mix;             // 0..1 (0 = bypass)
    int      divIdx;          // 0..3 — see comment above
    float    _bpm;

    void init() {
        memset(buf, 0, sizeof(buf));
        wPos          = 0;
        delaySamples  = 5292;   // 1/8 at 120 BPM
        feedback      = 0.35f;
        mix           = 0.0f;
        divIdx        = 2;
        _bpm          = 120.0f;
    }

    void setFeedback(float fb) { feedback = fclamp(fb, 0.0f, 0.85f); }
    void setMix(float m)       { mix      = fclamp(m,  0.0f, 1.0f); }

    void setTimeFromBPM(float bpm, int div) {
        _bpm   = bpm;
        divIdx = (div < 0) ? 0 : (div > 3 ? 3 : div);
        // beats per division: 1, 0.75 (dotted-8th), 0.5, 0.25
        static const float kBeats[4] = { 1.0f, 0.75f, 0.5f, 0.25f };
        float seconds = (60.0f / bpm) * kBeats[divIdx];
        int s = (int)(seconds * (float)SAMPLE_RATE);
        if (s < 8) s = 8;
        if (s > DELAY_BUF_SIZE - 16) s = DELAY_BUF_SIZE - 16;
        delaySamples = s;
    }

    IRAM_ATTR void process(float in, float& outL, float& outR) {
        if (mix < 0.001f) { outL = 0.0f; outR = 0.0f; wPos = (wPos + 1) & (DELAY_BUF_SIZE - 1); buf[wPos] = in; return; }
        // L tap (straight) — 1024 mask only valid if DELAY_BUF_SIZE is pow2 = 8192
        uint32_t mask = DELAY_BUF_SIZE - 1;
        uint32_t rPosL = (wPos - (uint32_t)delaySamples) & mask;
        // R tap offset by 7 ms for stereo spread (~309 samples @ 44.1 kHz)
        int rOffR = delaySamples + 309;
        if (rOffR >= DELAY_BUF_SIZE) rOffR = delaySamples - 309;
        uint32_t rPosR = (wPos - (uint32_t)rOffR) & mask;

        float wetL = buf[rPosL];
        float wetR = buf[rPosR];

        // Feed back into write head — single tap (L) for cleanest decay.
        buf[wPos] = in + wetL * feedback;
        wPos = (wPos + 1) & mask;

        outL = wetL * mix;
        outR = wetR * mix;
    }
};

// ── Master look-ahead lite limiter (selectable) ──────────────────────────────
// Peak-tracks across a small circular buffer and applies a one-pole release
// gain reduction.  Soft-knee above -3 dBFS.  Designed to sit AFTER the
// AnalogSaturation stage (which provides tonal soft-clip).
#define LIMITER_WINDOW 64
struct MasterLimiter {
    float peakBuf[LIMITER_WINDOW];
    int   wIdx;
    float gain;       // current attenuation (1.0 = no reduction)
    float releaseCoef;

    void init() {
        memset(peakBuf, 0, sizeof(peakBuf));
        wIdx        = 0;
        gain        = 1.0f;
        // Release ~80 ms one-pole: coef = exp(-1 / (release_s * SR))
        releaseCoef = expf(-1.0f / (0.080f * (float)SAMPLE_RATE));
    }

    IRAM_ATTR void process(float& L, float& R) {
        float absL = fabsf(L), absR = fabsf(R);
        float peak = (absL > absR) ? absL : absR;
        peakBuf[wIdx] = peak;
        wIdx = (wIdx + 1) & (LIMITER_WINDOW - 1);

        // Window peak (cheap: only need max of recent window)
        float windowPeak = peak;
        for (int k = 0; k < LIMITER_WINDOW; k += 8) {
            float a = peakBuf[k];
            if (a > windowPeak) windowPeak = a;
        }

        const float ceiling = 0.98f;
        float target = (windowPeak > ceiling) ? (ceiling / windowPeak) : 1.0f;
        // One-pole towards target.  Attack instant (target<gain), release slow.
        if (target < gain) gain = target;
        else               gain = target + (gain - target) * releaseCoef;
        L *= gain; R *= gain;
    }
};

// ── Main synthesizer engine ───────────────────────────────────────────────────
class SynthEngine {
private:
    Voice   voices[MAX_VOICES];
    int16_t audioBuffer[BUFFER_SIZE * 2];

    float masterVolume;
    float osc1Level;
    float osc2Level;
    float osc2Detune;
    float osc2Semitones;
    float filterEnvAmount;
    float pitchBendRatio;
    // Sub-osc defaults pushed to every voice on patch load / note-on
    WaveformType subWave;
    float        subLevel;
    int          subOctave;

    // Thread-safety: guards all parameter writes vs Core-0 audio render loop
    SemaphoreHandle_t _mutex;

    AnalogSaturation  _sat;
    DelayFX           _delay;
    MasterLimiter     _limiter;
    bool              _limiterOn;
    // Master "warmth": gentle one-pole high-shelf cut that rounds off the top
    // end so the digital edge/fizz doesn't make it sound cheap.  Per-channel
    // one-pole state.  ~6 kHz corner, highs kept at ~70 % (≈ -3 dB).
    float _warmL, _warmR;
    static constexpr float WARM_A    = 0.575f;  // 1 - exp(-2π·6000/44100)
    static constexpr float WARM_KEEP = 0.70f;   // fraction of high band retained

    // LFO modulation amounts (set from UI task, consumed in processAudio)
    float _lfoFilterMod;  // added to normalized cutoff in voice render
    float _lfoAmpMod;     // centred offset added to amplitude scale
    // Pitch LFO is applied directly to voice frequencies via setLFOPitch()

    int findVoice(int note) {
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].active && voices[i].note == note) return i;
        for (int i = 0; i < MAX_VOICES; i++)
            if (!voices[i].active) return i;

        // Steal oldest
        int oldest = 0;
        unsigned long oldestTime = voices[0].noteOnTime;
        for (int i = 1; i < MAX_VOICES; i++) {
            if (voices[i].noteOnTime < oldestTime) {
                oldest = i;
                oldestTime = voices[i].noteOnTime;
            }
        }
        return oldest;
    }

public:
    SynthEngine() {
        masterVolume    = 0.5f;
        osc1Level       = 0.5f;
        osc2Level       = 0.5f;
        osc2Detune      = 0.005f;
        osc2Semitones   = 0.0f;
        filterEnvAmount = 0.3f;
        pitchBendRatio  = 1.0f;
        subWave         = WAVE_SQUARE;
        subLevel        = 0.0f;
        subOctave       = 1;
        _mutex         = NULL;
        _lfoFilterMod  = 0.0f;
        _lfoAmpMod     = 0.0f;
        _limiterOn     = true;   // transparent peak control on by default
        _warmL         = 0.0f;
        _warmR         = 0.0f;
    }

    void init() {
        _mutex = xSemaphoreCreateMutex();

        _sat.init();
        _delay.init();
        _limiter.init();

        // Build wavetables first (≈30–50 ms, one-time)
        BLWavetables::init((float)SAMPLE_RATE);

        for (int i = 0; i < MAX_VOICES; i++) voices[i].init();

        // I2S for PCM5052 DAC
        i2s_config_t i2s_config = {
            .mode                = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
            .sample_rate         = SAMPLE_RATE,
            .bits_per_sample     = I2S_BITS_PER_SAMPLE_16BIT,
            .channel_format      = I2S_CHANNEL_FMT_RIGHT_LEFT,
            .communication_format= I2S_COMM_FORMAT_STAND_I2S,
            .intr_alloc_flags    = ESP_INTR_FLAG_LEVEL1,
            .dma_buf_count       = 8,
            .dma_buf_len         = BUFFER_SIZE,
            .use_apll            = true,
            .tx_desc_auto_clear  = true,
            .fixed_mclk          = 0
        };
        i2s_pin_config_t pin_config = {
            .bck_io_num   = I2S_BCK_PIN,
            .ws_io_num    = I2S_WS_PIN,
            .data_out_num = I2S_DATA_PIN,
            .data_in_num  = I2S_PIN_NO_CHANGE
        };
        i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
        i2s_set_pin(I2S_NUM, &pin_config);
        i2s_set_clk(I2S_NUM, SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_STEREO);
    }

    // ── Note events ───────────────────────────────────────────────────────────
    void noteOn(int note, int velocity) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        int vi = findVoice(note);
        Voice& v = voices[vi];
        v.osc2Detune      = osc2Detune;
        v.osc1Level       = osc1Level;
        v.osc2Level       = osc2Level;
        v.setSubLevelCached(subLevel);
        v.subOctave       = subOctave;
        v.subOsc.waveform = subWave;
        v.filterEnvAmount = filterEnvAmount;
        v.cutoffLock      = 0.0f;
        v.gainL           = 0.7071f;
        v.gainR           = 0.7071f;
        v.noteOn(note, velocity);
        if (pitchBendRatio != 1.0f) {
            v.osc1.setFrequency(v.baseFreq * pitchBendRatio);
            v.osc2.setFrequency(v.baseFreq * (1.0f + osc2Detune) * pitchBendRatio);
        }
        xSemaphoreGive(_mutex);
    }

    void noteOff(int note) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].active && voices[i].note == note)
                voices[i].noteOff();
        xSemaphoreGive(_mutex);
    }

    void allNotesOff() {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].active) voices[i].noteOff();
        xSemaphoreGive(_mutex);
    }

    // ── Oscillator ────────────────────────────────────────────────────────────
    void setOsc1Waveform(WaveformType wave) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].osc1.waveform = wave;
        xSemaphoreGive(_mutex);
    }

    void setOsc2Waveform(WaveformType wave) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].osc2.waveform = wave;
        xSemaphoreGive(_mutex);
    }

    void setOsc1Level(float level) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        osc1Level = constrain(level, 0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].osc1Level = osc1Level;
        xSemaphoreGive(_mutex);
    }

    void setOsc2Level(float level) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        osc2Level = constrain(level, 0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].osc2Level = osc2Level;
        xSemaphoreGive(_mutex);
    }

    void setOsc2Detune(float detuneRatio) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        osc2Detune = detuneRatio;
        for (int i = 0; i < MAX_VOICES; i++) {
            voices[i].osc2Detune = detuneRatio;
            if (voices[i].active)
                voices[i].osc2.setFrequency(
                    voices[i].baseFreq * (1.0f + detuneRatio) * pitchBendRatio);
        }
        xSemaphoreGive(_mutex);
    }

    float getOsc2Detune() const { return osc2Detune; }

    // ── Sub-oscillator ────────────────────────────────────────────────────────
    void setSubWaveform(WaveformType wave) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        subWave = wave;
        for (int i = 0; i < MAX_VOICES; i++) voices[i].subOsc.waveform = wave;
        xSemaphoreGive(_mutex);
    }

    void setSubLevel(float level) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        subLevel = constrain(level, 0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].setSubLevelCached(subLevel);
        xSemaphoreGive(_mutex);
    }

    void setSubOctave(int oct) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        subOctave = (oct < 1) ? 1 : (oct > 2 ? 2 : oct);
        for (int i = 0; i < MAX_VOICES; i++) {
            voices[i].subOctave = subOctave;
            if (voices[i].active) {
                voices[i].subOsc.setFrequency(
                    voices[i].baseFreq * (subOctave == 2 ? 0.25f : 0.5f));
            }
        }
        xSemaphoreGive(_mutex);
    }

    // ── Note-on with stereo pan (-1 = full L, 0 = centre, +1 = full R) ────────
    void noteOnPan(int note, int velocity, float pan) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        int vi = findVoice(note);
        Voice& v = voices[vi];
        v.osc2Detune      = osc2Detune;
        v.osc1Level       = osc1Level;
        v.osc2Level       = osc2Level;
        v.setSubLevelCached(subLevel);
        v.subOctave       = subOctave;
        v.subOsc.waveform = subWave;
        v.filterEnvAmount = filterEnvAmount;
        v.cutoffLock      = 0.0f;
        float p = constrain(pan, -1.0f, 1.0f);
        v.gainL = sqrtf(0.5f * (1.0f - p));
        v.gainR = sqrtf(0.5f * (1.0f + p));
        v.noteOn(note, velocity);
        if (pitchBendRatio != 1.0f) {
            v.osc1.setFrequency(v.baseFreq * pitchBendRatio);
            v.osc2.setFrequency(v.baseFreq * (1.0f + osc2Detune) * pitchBendRatio);
        }
        xSemaphoreGive(_mutex);
    }

    // ── Filter ────────────────────────────────────────────────────────────────
    void setFilterCutoff(float cutoff) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].filter.setCutoff(cutoff);
        xSemaphoreGive(_mutex);
    }

    void setFilterResonance(float resonance) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].filter.setResonance(resonance);
        xSemaphoreGive(_mutex);
    }

    void setFilterType(FilterType type) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].filter.type = type;
        xSemaphoreGive(_mutex);
    }

    void setFilterEnvAmount(float amount) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        filterEnvAmount = constrain(amount, 0.0f, 1.0f);
        for (int i = 0; i < MAX_VOICES; i++) voices[i].filterEnvAmount = filterEnvAmount;
        xSemaphoreGive(_mutex);
    }

    // ── Envelopes ─────────────────────────────────────────────────────────────
    void setAmpEnvelope(float attack, float decay, float sustain, float release) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++)
            voices[i].ampEnv.init(attack, decay, sustain, release);
        xSemaphoreGive(_mutex);
    }

    void setFilterEnvelope(float attack, float decay, float sustain, float release) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        for (int i = 0; i < MAX_VOICES; i++)
            voices[i].filterEnv.init(attack, decay, sustain, release);
        xSemaphoreGive(_mutex);
    }

    // ── Master volume ─────────────────────────────────────────────────────────
    void setMasterVolume(float vol) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        masterVolume = constrain(vol, 0.0f, 1.0f);
        xSemaphoreGive(_mutex);
    }

    // ── Pitch bend ────────────────────────────────────────────────────────────
    void setPitchBend(float semitones) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        pitchBendRatio = powf(2.0f, semitones / 12.0f);
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].active) {
                voices[i].osc1.setFrequency(
                    voices[i].baseFreq * pitchBendRatio);
                voices[i].osc2.setFrequency(
                    voices[i].baseFreq * (1.0f + voices[i].osc2Detune) * pitchBendRatio);
            }
        }
        xSemaphoreGive(_mutex);
    }

    // ── FX: Analog Saturation ─────────────────────────────────────────────────
    void setSaturationDrive(float d) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _sat.setDrive(d);
        xSemaphoreGive(_mutex);
    }

    void setSaturationAmount(float a) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _sat.setAmount(a);
        xSemaphoreGive(_mutex);
    }

    // ── FX: BPM-synced delay ──────────────────────────────────────────────────
    void setDelayMix(float m) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _delay.setMix(m);
        xSemaphoreGive(_mutex);
    }
    void setDelayFeedback(float fb) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _delay.setFeedback(fb);
        xSemaphoreGive(_mutex);
    }
    void setDelayDivision(float bpm, int divIdx) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _delay.setTimeFromBPM(bpm, divIdx);
        xSemaphoreGive(_mutex);
    }
    int   getDelayDivision() const { return _delay.divIdx; }
    float getDelayMix()      const { return _delay.mix; }
    float getDelayFeedback() const { return _delay.feedback; }

    // ── FX: Master limiter (selectable; tanh soft-clip stays on always) ───────
    void setLimiterEnabled(bool on) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _limiterOn = on;
        xSemaphoreGive(_mutex);
    }
    bool getLimiterEnabled() const { return _limiterOn; }

    // ── Note-on with stereo pan AND per-step cutoff lock (P-Lock support) ─────
    // cutoffLock is added to the filter cutoff inside Voice::process()
    // alongside the LFO + envelope contributions.  0.0 = no lock (default).
    void noteOnPanCutoff(int note, int velocity, float pan, float cutoffLock) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        int vi = findVoice(note);
        Voice& v = voices[vi];
        v.osc2Detune      = osc2Detune;
        v.osc1Level       = osc1Level;
        v.osc2Level       = osc2Level;
        v.setSubLevelCached(subLevel);
        v.subOctave       = subOctave;
        v.subOsc.waveform = subWave;
        v.filterEnvAmount = filterEnvAmount;
        float p = constrain(pan, -1.0f, 1.0f);
        v.gainL      = sqrtf(0.5f * (1.0f - p));
        v.gainR      = sqrtf(0.5f * (1.0f + p));
        v.cutoffLock = constrain(cutoffLock, -1.0f, 1.0f);
        v.noteOn(note, velocity);
        if (pitchBendRatio != 1.0f) {
            v.osc1.setFrequency(v.baseFreq * pitchBendRatio);
            v.osc2.setFrequency(v.baseFreq * (1.0f + osc2Detune) * pitchBendRatio);
        }
        xSemaphoreGive(_mutex);
    }

    // ── Per-track note-on (sequencer) ─────────────────────────────────────────
    // Each track owns a private voice window [track*VOICES_PER_TRACK, +N) so it
    // never steals from or cuts off another track.  The track's full SynthPatch
    // is applied to ITS voice only — no global state thrash, so a sustaining
    // note on another track keeps its own timbre/envelope.  pan and cutoffMod
    // (P-Locks) are applied per note.
    void noteOnTrack(int track, int note, int velocity,
                     float pan, float cutoffMod, const SynthPatch& p) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        int t   = (track < 0) ? 0 : (track >= SEQ_TRACKS ? SEQ_TRACKS - 1 : track);
        int lo  = t * VOICES_PER_TRACK;
        int hi  = lo + VOICES_PER_TRACK;
        // Pick a voice within this track's window: same note → free → oldest.
        int vi = -1;
        for (int i = lo; i < hi; i++)
            if (voices[i].active && voices[i].note == note) { vi = i; break; }
        if (vi < 0)
            for (int i = lo; i < hi; i++)
                if (!voices[i].active) { vi = i; break; }
        if (vi < 0) {
            vi = lo;
            unsigned long oldest = voices[lo].noteOnTime;
            for (int i = lo + 1; i < hi; i++)
                if (voices[i].noteOnTime < oldest) { oldest = voices[i].noteOnTime; vi = i; }
        }
        Voice& v = voices[vi];
        // Apply the per-track patch to this voice only.
        v.osc1.waveform   = (WaveformType)p.osc1Wave;
        v.osc2.waveform   = (WaveformType)p.osc2Wave;
        v.subOsc.waveform = (WaveformType)p.subWave;
        v.osc1Level       = constrain(p.osc1Level, 0.0f, 1.0f);
        v.osc2Level       = constrain(p.osc2Level, 0.0f, 1.0f);
        v.osc2Detune      = p.osc2Detune;
        v.setSubLevelCached(constrain(p.subLevel, 0.0f, 1.0f));
        v.subOctave       = (p.subOctave < 1) ? 1 : (p.subOctave > 2 ? 2 : p.subOctave);
        v.filterEnvAmount = constrain(p.filterEnvAmount, 0.0f, 1.0f);
        v.filter.type     = (FilterType)p.filterType;
        v.filter.setCutoff(p.filterCutoff);
        v.filter.setResonance(p.filterResonance);
        v.ampEnv.init(p.ampAttack, p.ampDecay, p.ampSustain, p.ampRelease);
        v.filterEnv.init(p.filterAttack, p.filterDecay, p.filterSustain, p.filterRelease);
        float pp = constrain(pan, -1.0f, 1.0f);
        v.gainL      = sqrtf(0.5f * (1.0f - pp));
        v.gainR      = sqrtf(0.5f * (1.0f + pp));
        v.cutoffLock = constrain(cutoffMod, -1.0f, 1.0f);
        v.noteOn(note, velocity);
        if (pitchBendRatio != 1.0f) {
            v.osc1.setFrequency(v.baseFreq * pitchBendRatio);
            v.osc2.setFrequency(v.baseFreq * (1.0f + v.osc2Detune) * pitchBendRatio);
        }
        xSemaphoreGive(_mutex);
    }

    // Note-off restricted to a track's voice window so it can't silence a
    // same-pitch note sounding on a different track.
    void noteOffTrack(int track, int note) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        int t  = (track < 0) ? 0 : (track >= SEQ_TRACKS ? SEQ_TRACKS - 1 : track);
        int lo = t * VOICES_PER_TRACK;
        int hi = lo + VOICES_PER_TRACK;
        for (int i = lo; i < hi; i++)
            if (voices[i].active && voices[i].note == note) voices[i].noteOff();
        xSemaphoreGive(_mutex);
    }

    // ── Audio generation ──────────────────────────────────────────────────────
    // Mutex held only during render; released before i2s_write so DMA blocking
    // doesn't stall Core-1 parameter setters.
    void processAudio() {
        // Lock-free render: snapshot scalar parameters under the mutex (a few
        // 32-bit reads), then release IMMEDIATELY so Core-1 parameter setters
        // are never stalled for the full 256-sample buffer (~5.8 ms).
        // Voice array reads are individually 32-bit-atomic on ESP32; the worst
        // case is a single inaudible sample with a mid-flight field swap.
        xSemaphoreTake(_mutex, portMAX_DELAY);
        const float lfoF       = _lfoFilterMod;
        const float lfoA       = _lfoAmpMod;
        const float masterVol  = masterVolume;
        const bool  limiterOn  = _limiterOn;
        xSemaphoreGive(_mutex);
        for (int i = 0; i < BUFFER_SIZE; i++) {
            float mixL = 0.0f, mixR = 0.0f;
            for (int v = 0; v < MAX_VOICES; v++) {
                if (voices[v].active || voices[v].ampEnv.isActive()) {
                    float s = voices[v].process(lfoF, lfoA);
                    mixL += s * voices[v].gainL;
                    mixR += s * voices[v].gainR;
                }
            }

            mixL = _sat.apply(mixL * masterVol * 0.3f);
            mixR = _sat.apply(mixR * masterVol * 0.3f);

            // Mono sum feeds the delay send (one tap, stereo-spread return).
            const float monoMix = (mixL + mixR) * 0.5f;

            // Optional BPM-synced delay sends a mono signal through the ring
            // and adds its stereo wet to the bus.  Bypass when mix < epsilon.
            float dL = 0.0f, dR = 0.0f;
            _delay.process(monoMix, dL, dR);

            // Dry pan retained; delay adds its stereo wet on top.
            float outL = mixL + dL;
            float outR = mixR + dR;

            // Master warmth: roll a little off the very top so the tone reads
            // as "analog/expensive" rather than digitally brittle.
            _warmL += WARM_A * (outL - _warmL);
            _warmR += WARM_A * (outR - _warmR);
            outL = _warmL + (outL - _warmL) * WARM_KEEP;
            outR = _warmR + (outR - _warmR) * WARM_KEEP;

            // Optional master limiter (selectable) tames sustained peaks.
            if (limiterOn) _limiter.process(outL, outR);

            // Always-on cubic soft-clip is the final safety net: it replaces the
            // old brick-wall clamp so transients/chords saturate smoothly instead
            // of producing harsh, glitchy digital clipping.
            audioBuffer[i * 2]     = (int16_t)(softClip(outL) * 32767.0f);
            audioBuffer[i * 2 + 1] = (int16_t)(softClip(outR) * 32767.0f);
        }

        size_t bytes_written;
        i2s_write(I2S_NUM, audioBuffer, sizeof(audioBuffer), &bytes_written, portMAX_DELAY);
    }

    // ── LFO modulation ────────────────────────────────────────────────────────
    // Filter LFO: normalized cutoff addition, e.g. ±0.15 = ±~1.5 log-octaves
    void setLFOFilterMod(float mod) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _lfoFilterMod = mod;
        xSemaphoreGive(_mutex);
    }

    // Amp LFO: centred offset (0.0 = no effect, +0.4 = +40% amplitude)
    void setLFOAmpMod(float mod) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        _lfoAmpMod = mod;
        xSemaphoreGive(_mutex);
    }

    // Pitch LFO: update all active voice frequencies by semitone offset.
    // Call from UI task at LFO rate; mutex ensures thread safety.
    void setLFOPitch(float semitones) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        float ratio = powf(2.0f, semitones / 12.0f) * pitchBendRatio;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (voices[i].active) {
                voices[i].osc1.setFrequency(voices[i].baseFreq * ratio);
                voices[i].osc2.setFrequency(
                    voices[i].baseFreq * (1.0f + voices[i].osc2Detune) * ratio);
            }
        }
        xSemaphoreGive(_mutex);
    }

    // ── Load a full SynthPatch atomically ─────────────────────────────────────
    void applyPatch(const SynthPatch& p) {
        xSemaphoreTake(_mutex, portMAX_DELAY);
        osc1Level       = p.osc1Level;
        osc2Level       = p.osc2Level;
        osc2Detune      = p.osc2Detune;
        filterEnvAmount = p.filterEnvAmount;
        masterVolume    = p.masterVolume;
        subWave         = (WaveformType)p.subWave;
        subLevel        = constrain(p.subLevel, 0.0f, 1.0f);
        subOctave       = (p.subOctave < 1) ? 1 : (p.subOctave > 2 ? 2 : p.subOctave);
        for (int i = 0; i < MAX_VOICES; i++) {
            voices[i].osc1.waveform  = (WaveformType)p.osc1Wave;
            voices[i].osc2.waveform  = (WaveformType)p.osc2Wave;
            voices[i].subOsc.waveform= subWave;
            voices[i].osc1Level      = p.osc1Level;
            voices[i].osc2Level      = p.osc2Level;
            voices[i].setSubLevelCached(subLevel);
            voices[i].subOctave      = subOctave;
            voices[i].osc2Detune     = p.osc2Detune;
            voices[i].filterEnvAmount= p.filterEnvAmount;
            voices[i].filter.type    = (FilterType)p.filterType;
            voices[i].filter.setCutoff(p.filterCutoff);
            voices[i].filter.setResonance(p.filterResonance);
            voices[i].ampEnv.init(p.ampAttack, p.ampDecay,
                                   p.ampSustain, p.ampRelease);
            voices[i].filterEnv.init(p.filterAttack, p.filterDecay,
                                      p.filterSustain, p.filterRelease);
        }
        xSemaphoreGive(_mutex);
    }

    int getActiveVoiceCount() {
        int count = 0;
        for (int i = 0; i < MAX_VOICES; i++)
            if (voices[i].active) count++;
        return count;
    }
};

#endif

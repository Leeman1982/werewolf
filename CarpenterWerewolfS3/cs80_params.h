#pragma once
#include <Arduino.h>
#include "config.h"
#include "param_types.h"

// ============================================================================
//  cs80_params.h — the CS-80 panel, as data.
//
//  Channels I and II are laid out as two identical contiguous blocks, so the
//  engine can address either with one offset and the UI gets two identical
//  pages for free.  Everything else follows the real panel's grouping.
//
//  As in the NYR build, every control is one normalised 0..1 float, which is
//  what lets the UI editor, MIDI CC, presets and per-step parameter locks all
//  share a single code path.
// ============================================================================

#define CS_CH_PARAMS 26        // parameters per channel — keep in step with the enum

enum ParamId : uint8_t {
    // ── Channel I ───────────────────────────────────────────────────────────
    C1_FEET = 0, C1_TUNE, C1_WAVE, C1_PW, C1_PWM_RATE, C1_PWM_DEPTH, C1_NOISE,
    C1_HPF_CUT, C1_HPF_RES, C1_LPF_CUT, C1_LPF_RES, C1_SINE_LVL,
    C1_VCA_A, C1_VCA_D, C1_VCA_S, C1_VCA_R,
    C1_VCF_IL, C1_VCF_AL, C1_VCF_A, C1_VCF_D, C1_VCF_R,
    C1_VEL_LVL, C1_VEL_BRI, C1_AT_LVL, C1_AT_BRI, C1_LEVEL,

    // ── Channel II (must mirror Channel I exactly, in the same order) ──────
    C2_FEET, C2_TUNE, C2_WAVE, C2_PW, C2_PWM_RATE, C2_PWM_DEPTH, C2_NOISE,
    C2_HPF_CUT, C2_HPF_RES, C2_LPF_CUT, C2_LPF_RES, C2_SINE_LVL,
    C2_VCA_A, C2_VCA_D, C2_VCA_S, C2_VCA_R,
    C2_VCF_IL, C2_VCF_AL, C2_VCF_A, C2_VCF_D, C2_VCF_R,
    C2_VEL_LVL, C2_VEL_BRI, C2_AT_LVL, C2_AT_BRI, C2_LEVEL,

    // ── Global voicing ─────────────────────────────────────────────────────
    P_MIX, P_BRILLIANCE, P_RESONANCE, P_FINAL_LPF, P_DRIFT,
    P_MASTER, P_BEND_RANGE, P_PORTA_TIME, P_PORTA_MODE, P_INIT_BEND,

    // ── Sub-oscillator (the global LFO) ────────────────────────────────────
    P_LFO_WAVE, P_LFO_RATE, P_LFO_PITCH, P_LFO_VCF, P_LFO_VCA,
    P_AT_LFO_DEPTH, P_AT_LFO_SPEED,

    // ── Ring modulator ─────────────────────────────────────────────────────
    P_RING_SPEED, P_RING_DEPTH, P_RING_EG_A, P_RING_EG_D, P_RING_EG_DEPTH,

    // ── Output section ─────────────────────────────────────────────────────
    P_TREM_RATE, P_TREM_DEPTH, P_TREM_MIX,
    P_DLY_TIME, P_DLY_FB, P_DLY_MIX, P_REV_SIZE, P_REV_MIX,

    // ── Arpeggiator ────────────────────────────────────────────────────────
    P_ARP_ENABLE, P_ARP_PATTERN, P_ARP_RATE, P_ARP_GATE,
    P_ARP_OCTAVES, P_ARP_DIR, P_ARP_LATCH, P_ARP_SYNC,

    NUM_PARAMETERS
};

// ─── Enumerated value names ─────────────────────────────────────────────────
extern const char* const CS_FEET_NAMES[4];      // 16' 8' 4' 2'
extern const char* const CS_WAVE_NAMES[3];      // SAW PULSE SINE
extern const char* const CS_LFO_WAVE_NAMES[5];  // SINE SAW+ SAW- SQR S&H
extern const char* const CS_PORTA_NAMES[3];     // OFF PORTA GLISS
extern const char* const CS_ARP_DIR_NAMES[6];
extern const char* const CS_ONOFF[2];

#define CSLFO_SINE 0
#define CSLFO_SAWUP 1
#define CSLFO_SAWDN 2
#define CSLFO_SQR  3
#define CSLFO_SH   4

#define PORTA_OFF   0
#define PORTA_GLIDE 1
#define PORTA_GLISS 2

// ─── Descriptor ─────────────────────────────────────────────────────────────
// ParamKind / ParamInfo / PresetDelta / Preset now live in param_types.h,
// shared with every other engine.
extern const ParamInfo PARAM_INFO[NUM_PARAMETERS];
extern float allParameterValues[NUM_PARAMETERS];

// ─── Engine-ready decoded values ────────────────────────────────────────────
struct Cs80ChanParams {
    float    feet;            // octave multiplier
    float    tune;            // detune ratio
    uint8_t  wave;
    float    pw;              // 0.5 .. 0.9 before PWM
    uint32_t pwmInc;          // this channel's own PWM LFO
    float    pwmDepth;
    float    noise;
    float    hpIdx, hpRes;    // log-frequency table index + 0..1 resonance
    float    lpIdx, lpRes;
    float    sineLvl;         // pure sine mixed AFTER the filters
    float    ampAtk, ampDec, ampSus, ampRel;
    float    vcfIL, vcfAL;    // bipolar, in filter-table index units
    float    vcfAtkStep, vcfDec, vcfRel;
    float    velLvl, velBri;
    float    atLvl, atBri;
    float    level;
};

struct Cs80EngineParams {
    Cs80ChanParams ch[CS_CHANNELS];

    float    mix;             // 0 = channel I only, 1 = channel II only
    float    brilliance;      // index offset applied to both filters
    float    resonance;       // added to both resonances
    float    finalLpIdx;      // unifying low-pass after the mix
    float    drift;           // 0..1 scaling of the per-voice offsets
    float    master;
    float    bendRange;       // semitones
    float    portaTime;       // seconds
    uint8_t  portaMode;
    float    initBend;        // velocity-sensitive dip into the note, semitones

    uint8_t  lfoWave;
    uint32_t lfoInc;
    float    lfoPitchHz;      // Hz/V architecture: pitch mod is a Hz offset
    float    lfoVcf, lfoVca;
    float    atLfoDepth, atLfoSpeed;

    float    ringSpeedHz, ringDepth;
    float    ringEgAtk, ringEgDec, ringEgDepth;

    float    tremRate, tremDepth, tremMix;
    float    dlyTimeMs, dlyFb, dlyMix;
    float    revSize, revMix;
};
extern Cs80EngineParams cs;

// ─── API ────────────────────────────────────────────────────────────────────
void  paramsInit();
void  setParameter(uint8_t id, float value);
float getParameter(uint8_t id);
void  formatParameter(uint8_t id, char* buf, size_t n);
bool  paramIsLockable(uint8_t id);

// ─── Presets ────────────────────────────────────────────────────────────────
extern const uint8_t INIT_PATCH[NUM_PARAMETERS];
extern const Preset  PRESETS[];
extern const int     NUM_PRESETS;

void loadPreset(int index);
void loadInitPatch();
void snapshotParams(uint8_t* out);
void restoreParams(const uint8_t* in);

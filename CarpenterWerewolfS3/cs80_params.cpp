#include "cs80_params.h"
#include "dsp_tables.h"
#include <math.h>
#include <stdio.h>

float allParameterValues[NUM_PARAMETERS];
Cs80EngineParams cs;

const char* const CS_FEET_NAMES[4]     = {"16'", "8'", "4'", "2'"};
const char* const CS_WAVE_NAMES[3]     = {"SAW", "PULSE", "SINE"};
const char* const CS_LFO_WAVE_NAMES[5] = {"SINE", "SAW+", "SAW-", "SQR", "S&H"};
const char* const CS_PORTA_NAMES[3]    = {"OFF", "PORTA", "GLISS"};
const char* const CS_ARP_DIR_NAMES[6]  = {"UP", "DOWN", "UPDN", "DNUP", "RAND", "PAT"};
const char* const CS_ONOFF[2]          = {"OFF", "ON"};

// ─── Panel table ────────────────────────────────────────────────────────────
// { name, kind, options, numOpts, dispMin, dispMax, cc }
// CC numbers run in disjoint blocks and skip every reserved controller
// (0/32 bank, 6/38 data entry, 64 sustain, 98-101 RPN/NRPN, 120-127 mode).
#define CHAN_ROWS(cc0, cc1, cc2, cc3, cc4, cc5, cc6, cc7, cc8, cc9, cc10, cc11, cc12, \
                  cc13, cc14, cc15, cc16, cc17, cc18, cc19, cc20, cc21, cc22, cc23,   \
                  cc24, cc25)                                                          \
    {"Feet",      PK_ENUM,   CS_FEET_NAMES, 4,   0,   0, cc0},                         \
    {"Tune",      PK_CENTS,  nullptr,       0,-100, 100, cc1},                         \
    {"Wave",      PK_ENUM,   CS_WAVE_NAMES, 3,   0,   0, cc2},                         \
    {"PulseWid",  PK_CONT,   nullptr,       0,  50,  90, cc3},                         \
    {"PWM Rate",  PK_HZ,     nullptr,       0,   0,   0, cc4},                         \
    {"PWM Dpth",  PK_CONT,   nullptr,       0,   0, 100, cc5},                         \
    {"Noise",     PK_CONT,   nullptr,       0,   0, 100, cc6},                         \
    {"HPF Cut",   PK_HZ,     nullptr,       0,   0,   0, cc7},                         \
    {"HPF Res",   PK_CONT,   nullptr,       0,   0, 100, cc8},                         \
    {"LPF Cut",   PK_HZ,     nullptr,       0,   0,   0, cc9},                         \
    {"LPF Res",   PK_CONT,   nullptr,       0,   0, 100, cc10},                        \
    {"Sine Lvl",  PK_CONT,   nullptr,       0,   0, 100, cc11},                        \
    {"VCA Atk",   PK_MS,     nullptr,       0,   0,   0, cc12},                        \
    {"VCA Dec",   PK_MS,     nullptr,       0,   0,   0, cc13},                        \
    {"VCA Sus",   PK_CONT,   nullptr,       0,   0, 100, cc14},                        \
    {"VCA Rel",   PK_MS,     nullptr,       0,   0,   0, cc15},                        \
    {"VCF IL",    PK_BIPOLAR,nullptr,       0,-100, 100, cc16},                        \
    {"VCF AL",    PK_BIPOLAR,nullptr,       0,-100, 100, cc17},                        \
    {"VCF Atk",   PK_MS,     nullptr,       0,   0,   0, cc18},                        \
    {"VCF Dec",   PK_MS,     nullptr,       0,   0,   0, cc19},                        \
    {"VCF Rel",   PK_MS,     nullptr,       0,   0,   0, cc20},                        \
    {"Vel>Lvl",   PK_CONT,   nullptr,       0,   0, 100, cc21},                        \
    {"Vel>Bri",   PK_CONT,   nullptr,       0,   0, 100, cc22},                        \
    {"AT>Lvl",    PK_CONT,   nullptr,       0,   0, 100, cc23},                        \
    {"AT>Bri",    PK_CONT,   nullptr,       0,   0, 100, cc24},                        \
    {"Level",     PK_CONT,   nullptr,       0,   0, 100, cc25}

const ParamInfo PARAM_INFO[NUM_PARAMETERS] = {
    CHAN_ROWS(14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
              27, 28, 29, 30, 31, 33, 34, 35, 36, 37, 39, 40, 41),
    CHAN_ROWS(42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54,
              55, 56, 57, 58, 59, 60, 61, 62, 63, 65, 66, 67, 68),

    {"Mix I-II",  PK_BIPOLAR, nullptr,          0, -100, 100, 69},
    {"Brillianc", PK_BIPOLAR, nullptr,          0, -100, 100, 70},
    {"Resonance", PK_CONT,    nullptr,          0,    0, 100, 71},
    {"Final LPF", PK_HZ,      nullptr,          0,    0,   0, 72},
    {"Drift",     PK_CONT,    nullptr,          0,    0, 100, 73},
    {"Master",    PK_CONT,    nullptr,          0,    0, 100, 74},
    {"Bend Rng",  PK_INT,     nullptr,          0,    0,  12, 75},
    {"Porta Tim", PK_MS,      nullptr,          0,    0,   0, 76},
    {"Porta Mod", PK_ENUM,    CS_PORTA_NAMES,   3,    0,   0, 77},
    {"Init Bend", PK_CONT,    nullptr,          0,    0, 100, 78},

    {"LFO Wave",  PK_ENUM,    CS_LFO_WAVE_NAMES,5,    0,   0, 79},
    {"LFO Rate",  PK_HZ,      nullptr,          0,    0,   0, 80},
    {"LFO>Pitch", PK_CONT,    nullptr,          0,    0, 100, 81},
    {"LFO>VCF",   PK_CONT,    nullptr,          0,    0, 100, 82},
    {"LFO>VCA",   PK_CONT,    nullptr,          0,    0, 100, 83},
    {"AT>LFO Dp", PK_CONT,    nullptr,          0,    0, 100, 84},
    {"AT>LFO Sp", PK_CONT,    nullptr,          0,    0, 100, 85},

    {"Ring Spd",  PK_HZ,      nullptr,          0,    0,   0, 86},
    {"Ring Dpth", PK_CONT,    nullptr,          0,    0, 100, 87},
    {"RingEG At", PK_MS,      nullptr,          0,    0,   0, 88},
    {"RingEG Dc", PK_MS,      nullptr,          0,    0,   0, 89},
    {"RingEG Dp", PK_CONT,    nullptr,          0,    0, 100, 90},

    {"Trem Rate", PK_HZ,      nullptr,          0,    0,   0, 91},
    {"Trem Dpth", PK_CONT,    nullptr,          0,    0, 100, 92},
    {"Trem Mix",  PK_CONT,    nullptr,          0,    0, 100, 93},
    {"Dly Time",  PK_MS,      nullptr,          0,    0,   0, 94},
    {"Dly Fdbk",  PK_CONT,    nullptr,          0,    0,  95, 95},
    {"Dly Mix",   PK_CONT,    nullptr,          0,    0, 100, 96},
    {"Rev Size",  PK_CONT,    nullptr,          0,    0, 100, 97},
    {"Rev Mix",   PK_CONT,    nullptr,          0,    0, 100, 102},

    {"Arp On",    PK_TOGGLE,  CS_ONOFF,         2,    0,   0, 103},
    {"Arp Patt",  PK_INT,     nullptr,          0,    1,  50, 104},
    {"Arp Rate",  PK_INT,     nullptr,          0,   40, 300, 105},
    {"Arp Gate",  PK_CONT,    nullptr,          0,   10, 100, 106},
    {"Arp Oct",   PK_INT,     nullptr,          0,    1,   4, 107},
    {"Arp Dir",   PK_ENUM,    CS_ARP_DIR_NAMES, 6,    0,   0, 108},
    {"Arp Latch", PK_TOGGLE,  CS_ONOFF,         2,    0,   0, 109},
    {"Arp Sync",  PK_TOGGLE,  CS_ONOFF,         2,    0,   0, 110},
};

// ─── Scaling helpers (core0 only) ───────────────────────────────────────────
static inline int enumOf(float n, int count) {
    int i = (int)(n * (float)count);
    return (i < 0) ? 0 : (i >= count ? count - 1 : i);
}
// Spec: VCA attack 1 ms .. 1 s, decay/release 10 ms .. 10 s.
static inline float atkSec(float n)  { return 0.001f * powf(1000.0f, n); }
static inline float decSec(float n)  { return 0.010f * powf(1000.0f, n); }
static inline float lfoHz(float n)   { return 0.7f * powf(85.7f, n); }        // 0.7 .. 60 Hz
static inline float pwmHz(float n)   { return 0.05f * powf(400.0f, n); }      // 0.05 .. 20 Hz
static inline float ringHz(float n)  { return 0.1f * powf(30000.0f, n); }     // 0.1 Hz .. 3 kHz
static inline float tremHz(float n)  { return 0.1f * powf(100.0f, n); }       // 0.1 .. 10 Hz
static inline float portaSec(float n){ return (n < 0.02f) ? 0.0f : 0.01f * powf(300.0f, n); }
static inline float delayMs(float n) { return 10.0f + n * 330.0f; }

// Exponential segment coefficient reaching its target in `t` seconds, at the
// rate the caller runs (sample rate, or control rate for the filter envelope).
static inline float segCoef(float t, float rate) {
    if (t < 0.0002f) return 1.0f;
    float c = 1.0f - expf(-6.9f / (t * rate));
    return (c > 1.0f) ? 1.0f : c;
}
#define CTRL_RATE (SAMPLE_RATE_F / (float)CONTROL_RATE_DIV)

// Cutoff in log-frequency table index, floored at the CS-80's ~23 Hz minimum.
static float cutIdxFromNorm(float n) {
    float minIdx = log2f(CS_MIN_CUTOFF_HZ / FILT_HZ_MIN) * FILT_IDX_PER_OCT;
    if (minIdx < 0.0f) minIdx = 0.0f;
    return minIdx + n * ((float)(FILT_TAB_SIZE - 1) - minIdx);
}

float getParameter(uint8_t id) {
    return (id < NUM_PARAMETERS) ? allParameterValues[id] : 0.0f;
}

// Decode one channel parameter.  `k` is the offset within the channel block,
// which is identical for both channels — that symmetry is the whole reason the
// enum is laid out in two contiguous runs.
static void setChanParam(Cs80ChanParams& c, uint8_t k, float v) {
    static const float FEET[4] = {0.5f, 1.0f, 2.0f, 4.0f};
    switch (k) {
    case 0:  c.feet = FEET[enumOf(v, 4)]; break;
    case 1:  c.tune = powf(2.0f, (v - 0.5f) * 200.0f / 1200.0f); break;   // +/-100 cents
    case 2:  c.wave = (uint8_t)enumOf(v, 3); break;
    case 3:  c.pw = 0.5f + v * 0.4f; break;                               // 50..90 %
    case 4:  c.pwmInc = (uint32_t)(pwmHz(v) * PHASE_PER_HZ); break;
    case 5:  c.pwmDepth = v * 0.4f; break;
    case 6:  c.noise = v * 0.7f; break;
    case 7:  c.hpIdx = cutIdxFromNorm(v); break;
    case 8:  c.hpRes = v; break;
    case 9:  c.lpIdx = cutIdxFromNorm(v); break;
    case 10: c.lpRes = v; break;
    case 11: c.sineLvl = v; break;
    case 12: c.ampAtk = 1.0f / (atkSec(v) * SAMPLE_RATE_F); break;
    case 13: c.ampDec = segCoef(decSec(v), SAMPLE_RATE_F); break;
    case 14: c.ampSus = v; break;
    case 15: c.ampRel = segCoef(decSec(v), SAMPLE_RATE_F); break;
    // IL and AL are bipolar and reach +/-4 octaves of cutoff offset.
    case 16: c.vcfIL = (v - 0.5f) * 8.0f * FILT_IDX_PER_OCT; break;
    case 17: c.vcfAL = (v - 0.5f) * 8.0f * FILT_IDX_PER_OCT; break;
    case 18: c.vcfAtkStep = 1.0f / (atkSec(v) * CTRL_RATE); break;
    case 19: c.vcfDec = segCoef(decSec(v), CTRL_RATE); break;
    case 20: c.vcfRel = segCoef(decSec(v), CTRL_RATE); break;
    case 21: c.velLvl = v; break;
    case 22: c.velBri = v * 4.0f * FILT_IDX_PER_OCT; break;
    case 23: c.atLvl = v; break;
    case 24: c.atBri = v * 4.0f * FILT_IDX_PER_OCT; break;
    case 25: c.level = v; break;
    default: break;
    }
}

void setParameter(uint8_t id, float v) {
    if (id >= NUM_PARAMETERS) return;
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;
    allParameterValues[id] = v;

    if (id < CS_CH_PARAMS * CS_CHANNELS) {
        setChanParam(cs.ch[id / CS_CH_PARAMS], (uint8_t)(id % CS_CH_PARAMS), v);
        return;
    }

    switch (id) {
    case P_MIX:        cs.mix = v; break;
    case P_BRILLIANCE: cs.brilliance = (v - 0.5f) * 6.0f * FILT_IDX_PER_OCT; break;
    case P_RESONANCE:  cs.resonance = v; break;
    case P_FINAL_LPF:  cs.finalLpIdx = cutIdxFromNorm(0.25f + v * 0.75f); break;
    case P_DRIFT:      cs.drift = v; break;
    case P_MASTER:     cs.master = v; break;
    case P_BEND_RANGE: cs.bendRange = (float)((int)(v * 12.99f)); break;
    case P_PORTA_TIME: cs.portaTime = portaSec(v); break;
    case P_PORTA_MODE: cs.portaMode = (uint8_t)enumOf(v, 3); break;
    case P_INIT_BEND:  cs.initBend = v * 12.0f; break;

    case P_LFO_WAVE:   cs.lfoWave = (uint8_t)enumOf(v, 5); break;
    case P_LFO_RATE:   cs.lfoInc = (uint32_t)(lfoHz(v) * PHASE_PER_HZ); break;
    // Hz/V architecture: a fixed modulation voltage is a fixed FREQUENCY
    // deviation, not a fixed interval.  Vibrato is therefore much deeper in
    // musical terms low down the keyboard than high up, which is one of the
    // things that makes a CS-80 sound like a CS-80 rather than like a V/oct
    // synth.  Kept faithful rather than "corrected".
    case P_LFO_PITCH:  cs.lfoPitchHz = v * v * 25.0f; break;
    case P_LFO_VCF:    cs.lfoVcf = v * 3.0f * FILT_IDX_PER_OCT; break;
    case P_LFO_VCA:    cs.lfoVca = v; break;
    case P_AT_LFO_DEPTH: cs.atLfoDepth = v; break;
    case P_AT_LFO_SPEED: cs.atLfoSpeed = v; break;

    case P_RING_SPEED: cs.ringSpeedHz = ringHz(v); break;
    case P_RING_DEPTH: cs.ringDepth = v; break;
    case P_RING_EG_A:  cs.ringEgAtk = 1.0f / (atkSec(v) * CTRL_RATE); break;
    case P_RING_EG_D:  cs.ringEgDec = segCoef(decSec(v), CTRL_RATE); break;
    case P_RING_EG_DEPTH: cs.ringEgDepth = v; break;

    case P_TREM_RATE:  cs.tremRate = tremHz(v); break;
    case P_TREM_DEPTH: cs.tremDepth = v; break;
    case P_TREM_MIX:   cs.tremMix = v; break;
    case P_DLY_TIME:   cs.dlyTimeMs = delayMs(v); break;
    case P_DLY_FB:     cs.dlyFb = v * 0.95f; break;
    case P_DLY_MIX:    cs.dlyMix = v; break;
    case P_REV_SIZE:   cs.revSize = 0.55f + v * 0.42f; break;
    case P_REV_MIX:    cs.revMix = v; break;
    default: break;                       // arp block is read straight from the array
    }
}

// ─── Display ────────────────────────────────────────────────────────────────
static void formatTime(float sec, char* buf, size_t n) {
    if (sec <= 0.0f)     snprintf(buf, n, "OFF");
    else if (sec < 1.0f) snprintf(buf, n, "%dms", (int)(sec * 1000.0f + 0.5f));
    else                 snprintf(buf, n, "%.2fs", (double)sec);
}

void formatParameter(uint8_t id, char* buf, size_t n) {
    if (id >= NUM_PARAMETERS) { snprintf(buf, n, "-"); return; }
    const ParamInfo& pi = PARAM_INFO[id];
    float v = allParameterValues[id];
    uint8_t k = (id < CS_CH_PARAMS * CS_CHANNELS) ? (uint8_t)(id % CS_CH_PARAMS) : 0xFF;

    switch (pi.kind) {
    case PK_ENUM:    snprintf(buf, n, "%s", pi.opts[enumOf(v, pi.numOpts)]); break;
    case PK_TOGGLE:  snprintf(buf, n, "%s", (v > 0.5f) ? "ON" : "OFF"); break;
    case PK_BIPOLAR: snprintf(buf, n, "%+d", (int)((v - 0.5f) * 2.0f * (float)pi.dispMax)); break;
    case PK_CENTS:   snprintf(buf, n, "%+d", (int)((v - 0.5f) * 200.0f)); break;
    case PK_INT:     snprintf(buf, n, "%d", pi.dispMin +
                              (int)(v * (float)(pi.dispMax - pi.dispMin) + 0.5f)); break;
    case PK_HZ: {
        float hz;
        if (k == 7 || k == 9)        hz = filterHzFromIdx(cutIdxFromNorm(v));
        else if (k == 4)             hz = pwmHz(v);
        else if (id == P_FINAL_LPF)  hz = filterHzFromIdx(cutIdxFromNorm(0.25f + v * 0.75f));
        else if (id == P_LFO_RATE)   hz = lfoHz(v);
        else if (id == P_RING_SPEED) hz = ringHz(v);
        else                         hz = tremHz(v);
        if (hz >= 1000.0f) snprintf(buf, n, "%.1fk", (double)(hz / 1000.0f));
        else if (hz >= 10.0f) snprintf(buf, n, "%dHz", (int)hz);
        else snprintf(buf, n, "%.2fHz", (double)hz);
        break;
    }
    case PK_MS:
        if (id == P_DLY_TIME)        snprintf(buf, n, "%dms", (int)delayMs(v));
        else if (id == P_PORTA_TIME) formatTime(portaSec(v), buf, n);
        else if (k == 12 || k == 18 || id == P_RING_EG_A) formatTime(atkSec(v), buf, n);
        else                         formatTime(decSec(v), buf, n);
        break;
    default:
        snprintf(buf, n, "%d", pi.dispMin + (int)(v * (float)(pi.dispMax - pi.dispMin) + 0.5f));
        break;
    }
}

bool paramIsLockable(uint8_t id) {
    if (id >= P_ARP_ENABLE) return false;
    if (id == P_MASTER || id == P_BEND_RANGE) return false;
    return true;
}

// ============================================================================
//  Presets
// ============================================================================
#define ENV_(i, n) ((uint8_t)((((i) * 2 + 1) * 255) / (2 * (n))))
#define PCT(x)     ((uint8_t)(((x) * 255) / 100))
#define BI(x)      ((uint8_t)(128 + ((x) * 127) / 100))   // -100..+100 -> 0..255

// The init patch is a plain 8' saw on both channels, channel II detuned a
// couple of cents — the CS-80 at its most neutral, and the right starting
// point for building anything else.
const uint8_t INIT_PATCH[NUM_PARAMETERS] = {
    // ── Channel I ──
    ENV_(1,4), 128, ENV_(0,3), 128, PCT(30), 0, 0,
    0, 0, PCT(72), PCT(20), 0,
    PCT(6), PCT(45), PCT(80), PCT(30),
    BI(0), BI(35), PCT(20), PCT(45), PCT(30),
    PCT(40), PCT(45), PCT(30), PCT(55), PCT(85),
    // ── Channel II ──
    ENV_(1,4), 133, ENV_(0,3), 128, PCT(33), 0, 0,
    0, 0, PCT(66), PCT(22), 0,
    PCT(10), PCT(50), PCT(78), PCT(34),
    BI(0), BI(30), PCT(24), PCT(50), PCT(34),
    PCT(40), PCT(45), PCT(30), PCT(55), PCT(85),
    // ── Global voicing ──
    128, BI(0), PCT(15), PCT(80), PCT(35), PCT(78), ENV_(2,13), 0, ENV_(0,3), 0,
    // ── Sub-oscillator ──
    ENV_(0,5), PCT(35), 0, 0, 0, PCT(40), PCT(30),
    // ── Ring modulator ──
    PCT(45), 0, PCT(20), PCT(45), 0,
    // ── Output ──
    PCT(35), PCT(30), 0, PCT(40), PCT(35), 0, PCT(55), 0,
    // ── Arp ──
    0, 0, PCT(30), PCT(70), 0, ENV_(0,6), 0, 255,
};

#define PSET(sym) static const PresetDelta sym[] =

PSET(pd_brass)     {{C1_WAVE,ENV_(0,3)},{C1_LPF_CUT,PCT(40)},{C1_LPF_RES,PCT(30)},
                    {C1_VCF_IL,BI(-25)},{C1_VCF_AL,BI(55)},{C1_VCF_A,PCT(22)},
                    {C1_VCF_D,PCT(42)},{C1_VCA_A,PCT(18)},{C1_VCA_S,PCT(75)},
                    {C2_WAVE,ENV_(0,3)},{C2_TUNE,136},{C2_LPF_CUT,PCT(36)},
                    {C2_VCF_AL,BI(48)},{C2_VCF_A,PCT(26)},{C2_VCA_A,PCT(22)},
                    {C1_VEL_BRI,PCT(70)},{C2_VEL_BRI,PCT(70)},{P_RESONANCE,PCT(25)}};
PSET(pd_strings)   {{C1_LPF_CUT,PCT(55)},{C1_VCA_A,PCT(42)},{C1_VCA_R,PCT(48)},
                    {C1_VCA_S,PCT(90)},{C1_VCF_AL,BI(20)},{C1_VCF_A,PCT(45)},
                    {C2_TUNE,141},{C2_LPF_CUT,PCT(52)},{C2_VCA_A,PCT(46)},
                    {C2_VCA_R,PCT(50)},{C2_VCA_S,PCT(90)},
                    {C1_HPF_CUT,PCT(18)},{C2_HPF_CUT,PCT(18)},
                    {P_TREM_MIX,PCT(45)},{P_LFO_PITCH,PCT(12)},{P_REV_MIX,PCT(30)}};
PSET(pd_choir)     {{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(55)},{C1_PWM_DEPTH,PCT(60)},
                    {C1_PWM_RATE,PCT(18)},{C1_LPF_CUT,PCT(48)},{C1_SINE_LVL,PCT(35)},
                    {C1_VCA_A,PCT(50)},{C1_VCA_R,PCT(55)},{C1_VCA_S,PCT(92)},
                    {C2_WAVE,ENV_(1,3)},{C2_PW,PCT(62)},{C2_PWM_DEPTH,PCT(65)},
                    {C2_PWM_RATE,PCT(22)},{C2_TUNE,139},{C2_LPF_CUT,PCT(44)},
                    {C2_VCA_A,PCT(54)},{C2_VCA_R,PCT(58)},{C2_VCA_S,PCT(92)},
                    {P_TREM_MIX,PCT(55)},{P_REV_MIX,PCT(42)},{P_REV_SIZE,PCT(85)}};
PSET(pd_bass)      {{C1_FEET,ENV_(0,4)},{C2_FEET,ENV_(0,4)},{C1_LPF_CUT,PCT(28)},
                    {C1_LPF_RES,PCT(40)},{C1_VCF_AL,BI(45)},{C1_VCF_D,PCT(28)},
                    {C1_VCA_D,PCT(35)},{C1_VCA_S,PCT(60)},{C1_VCA_R,PCT(22)},
                    {C2_TUNE,131},{C2_LPF_CUT,PCT(24)},{C2_VCA_S,PCT(60)},
                    {C1_SINE_LVL,PCT(30)},{C2_SINE_LVL,PCT(25)},
                    {P_PORTA_TIME,PCT(20)},{P_PORTA_MODE,ENV_(1,3)}};
PSET(pd_lead)      {{C1_FEET,ENV_(2,4)},{C1_LPF_CUT,PCT(58)},{C1_LPF_RES,PCT(45)},
                    {C1_VCF_AL,BI(40)},{C1_VCF_D,PCT(38)},{C1_VCA_A,PCT(10)},
                    {C2_FEET,ENV_(1,4)},{C2_TUNE,143},{C2_LPF_CUT,PCT(52)},
                    {P_PORTA_TIME,PCT(28)},{P_PORTA_MODE,ENV_(1,3)},
                    {P_LFO_PITCH,PCT(18)},{P_LFO_RATE,PCT(45)},{P_INIT_BEND,PCT(30)},
                    {P_DLY_MIX,PCT(25)}};
PSET(pd_bells)     {{C1_WAVE,ENV_(2,3)},{C2_WAVE,ENV_(2,3)},{C2_FEET,ENV_(2,4)},
                    {C1_VCA_A,0},{C1_VCA_D,PCT(55)},{C1_VCA_S,0},{C1_VCA_R,PCT(50)},
                    {C2_VCA_A,0},{C2_VCA_D,PCT(50)},{C2_VCA_S,0},{C2_VCA_R,PCT(45)},
                    {C1_LPF_CUT,PCT(85)},{C2_LPF_CUT,PCT(85)},
                    {P_RING_DEPTH,PCT(65)},{P_RING_SPEED,PCT(62)},
                    {P_RING_EG_DEPTH,PCT(45)},{P_RING_EG_D,PCT(50)},
                    {P_REV_MIX,PCT(40)}};
PSET(pd_funkclav)  {{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(20)},{C1_LPF_CUT,PCT(45)},
                    {C1_LPF_RES,PCT(55)},{C1_HPF_CUT,PCT(28)},
                    {C1_VCF_IL,BI(-40)},{C1_VCF_AL,BI(60)},{C1_VCF_A,0},
                    {C1_VCF_D,PCT(22)},{C1_VCA_A,0},{C1_VCA_D,PCT(28)},
                    {C1_VCA_S,PCT(15)},{C1_VCA_R,PCT(18)},
                    {C2_LEVEL,0},{C1_VEL_BRI,PCT(85)},{C1_VEL_LVL,PCT(60)}};
PSET(pd_softpad)   {{C1_LPF_CUT,PCT(38)},{C1_VCA_A,PCT(58)},{C1_VCA_R,PCT(62)},
                    {C1_VCA_S,PCT(95)},{C1_SINE_LVL,PCT(40)},{C1_VCF_AL,BI(18)},
                    {C1_VCF_A,PCT(55)},{C2_TUNE,138},{C2_LPF_CUT,PCT(34)},
                    {C2_VCA_A,PCT(62)},{C2_VCA_R,PCT(66)},{C2_VCA_S,PCT(95)},
                    {C2_SINE_LVL,PCT(35)},{P_REV_MIX,PCT(50)},{P_REV_SIZE,PCT(90)},
                    {P_TREM_MIX,PCT(35)}};
PSET(pd_toots)     {{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(30)},{C1_LPF_CUT,PCT(50)},
                    {C1_LPF_RES,PCT(35)},{C1_VCF_AL,BI(35)},{C1_VCF_D,PCT(35)},
                    {C1_VCA_A,PCT(14)},{C1_VCA_S,PCT(78)},{C2_LEVEL,PCT(40)},
                    {C2_WAVE,ENV_(2,3)},{C2_FEET,ENV_(0,4)},
                    {P_LFO_PITCH,PCT(22)},{P_LFO_RATE,PCT(50)},{P_AT_LFO_DEPTH,PCT(70)},
                    {C1_AT_BRI,PCT(65)},{C1_AT_LVL,PCT(40)}};
PSET(pd_hollow)    {{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(85)},{C1_HPF_CUT,PCT(35)},
                    {C1_HPF_RES,PCT(45)},{C1_LPF_CUT,PCT(60)},{C1_LPF_RES,PCT(40)},
                    {C2_WAVE,ENV_(1,3)},{C2_PW,PCT(15)},{C2_TUNE,134},
                    {C2_HPF_CUT,PCT(30)},{C1_VCA_A,PCT(30)},{C2_VCA_A,PCT(34)},
                    {P_BRILLIANCE,BI(12)}};
PSET(pd_noisewind) {{C1_NOISE,PCT(95)},{C1_LEVEL,PCT(40)},{C1_LPF_CUT,PCT(42)},
                    {C1_LPF_RES,PCT(62)},{C1_HPF_CUT,PCT(25)},
                    {C1_VCA_A,PCT(55)},{C1_VCA_S,PCT(95)},{C1_VCA_R,PCT(58)},
                    {C2_LEVEL,0},{P_LFO_VCF,PCT(55)},{P_LFO_RATE,PCT(10)},
                    {P_REV_MIX,PCT(45)}};
PSET(pd_ringlead)  {{C1_FEET,ENV_(2,4)},{C1_LPF_CUT,PCT(62)},{C2_FEET,ENV_(2,4)},
                    {C2_TUNE,150},{P_RING_DEPTH,PCT(80)},{P_RING_SPEED,PCT(38)},
                    {P_RING_EG_DEPTH,PCT(70)},{P_RING_EG_A,PCT(35)},
                    {P_RING_EG_D,PCT(55)},{P_DLY_MIX,PCT(30)}};
PSET(pd_orch)      {{C1_LPF_CUT,PCT(50)},{C1_VCA_A,PCT(35)},{C1_VCA_S,PCT(88)},
                    {C1_VCA_R,PCT(45)},{C1_VCF_AL,BI(28)},{C1_VCF_A,PCT(38)},
                    {C2_WAVE,ENV_(1,3)},{C2_PW,PCT(60)},{C2_PWM_DEPTH,PCT(50)},
                    {C2_TUNE,140},{C2_VCA_A,PCT(40)},{C2_VCA_S,PCT(88)},
                    {C1_HPF_CUT,PCT(12)},{P_TREM_MIX,PCT(50)},{P_REV_MIX,PCT(38)},
                    {P_MIX,120}};
PSET(pd_percorgan) {{C1_WAVE,ENV_(2,3)},{C2_WAVE,ENV_(2,3)},{C2_FEET,ENV_(3,4)},
                    {C1_VCA_A,0},{C1_VCA_S,255},{C1_VCA_R,PCT(10)},
                    {C2_VCA_A,0},{C2_VCA_D,PCT(30)},{C2_VCA_S,0},{C2_VCA_R,PCT(12)},
                    {C1_LPF_CUT,PCT(78)},{C2_LPF_CUT,PCT(88)},
                    {P_TREM_MIX,PCT(60)},{P_TREM_RATE,PCT(60)}};
PSET(pd_sweeppad)  {{C1_LPF_CUT,PCT(20)},{C1_LPF_RES,PCT(58)},
                    {C1_VCF_IL,BI(-60)},{C1_VCF_AL,BI(70)},{C1_VCF_A,PCT(70)},
                    {C1_VCF_D,PCT(65)},{C1_VCF_R,PCT(60)},
                    {C1_VCA_A,PCT(45)},{C1_VCA_S,PCT(92)},{C1_VCA_R,PCT(58)},
                    {C2_TUNE,137},{C2_LPF_CUT,PCT(18)},{C2_VCF_AL,BI(65)},
                    {C2_VCF_A,PCT(75)},{C2_VCA_A,PCT(48)},{C2_VCA_S,PCT(92)},
                    {P_REV_MIX,PCT(48)}};
PSET(pd_warmbass)  {{C1_FEET,ENV_(0,4)},{C2_FEET,ENV_(0,4)},{C1_WAVE,ENV_(1,3)},
                    {C1_PW,PCT(35)},{C1_LPF_CUT,PCT(30)},{C1_LPF_RES,PCT(25)},
                    {C1_VCF_AL,BI(35)},{C1_VCF_D,PCT(30)},{C1_VCA_D,PCT(35)},
                    {C1_VCA_S,PCT(65)},{C2_TUNE,132},{C2_LPF_CUT,PCT(26)}};
PSET(pd_deepbass)  {{C1_FEET,ENV_(0,4)},{C2_FEET,ENV_(0,4)},{C1_WAVE,ENV_(2,3)},
                    {C1_SINE_LVL,PCT(70)},{C1_LPF_CUT,PCT(20)},
                    {C1_VCA_D,PCT(30)},{C1_VCA_S,PCT(55)},{C2_LEVEL,0}};
PSET(pd_glassbells){{C1_WAVE,ENV_(2,3)},{C2_WAVE,ENV_(2,3)},{C2_FEET,ENV_(3,4)},
                    {C1_VCA_A,0},{C1_VCA_D,PCT(60)},{C1_VCA_S,0},{C1_VCA_R,PCT(55)},
                    {C2_VCA_A,0},{C2_VCA_D,PCT(55)},{C2_VCA_S,0},
                    {C1_LPF_CUT,PCT(90)},{C2_LPF_CUT,PCT(90)},
                    {P_RING_DEPTH,PCT(40)},{P_RING_SPEED,PCT(70)},{P_REV_MIX,PCT(45)}};
PSET(pd_vibratolead){{C1_FEET,ENV_(2,4)},{C1_LPF_CUT,PCT(60)},{C1_LPF_RES,PCT(40)},
                    {P_LFO_PITCH,PCT(30)},{P_LFO_RATE,PCT(55)},{P_INIT_BEND,PCT(20)}};
PSET(pd_spacepad)  {{C1_LPF_CUT,PCT(35)},{C1_VCA_A,PCT(70)},{C1_VCA_R,PCT(75)},
                    {C1_VCA_S,PCT(95)},{C2_TUNE,142},{C2_LPF_CUT,PCT(32)},
                    {C2_VCA_A,PCT(72)},{C2_VCA_R,PCT(78)},{C2_VCA_S,PCT(95)},
                    {P_DLY_MIX,PCT(35)},{P_DLY_TIME,PCT(50)},
                    {P_REV_MIX,PCT(65)},{P_REV_SIZE,PCT(100)}};
PSET(pd_glidelead) {{C1_FEET,ENV_(2,4)},{C1_LPF_CUT,PCT(65)},{C1_LPF_RES,PCT(45)},
                    {P_PORTA_TIME,PCT(35)},{P_PORTA_MODE,ENV_(1,3)}};
PSET(pd_pwmstrings){{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(50)},{C1_PWM_DEPTH,PCT(55)},
                    {C1_PWM_RATE,PCT(15)},{C1_VCA_A,PCT(45)},{C1_VCA_S,PCT(88)},
                    {C2_WAVE,ENV_(1,3)},{C2_PW,PCT(56)},{C2_PWM_DEPTH,PCT(58)},
                    {C2_TUNE,137},{C2_VCA_A,PCT(48)},{C2_VCA_S,PCT(88)},
                    {P_TREM_MIX,PCT(40)}};
PSET(pd_metalbells){{C1_WAVE,ENV_(1,3)},{C2_WAVE,ENV_(1,3)},{C2_FEET,ENV_(2,4)},
                    {C1_VCA_A,0},{C1_VCA_D,PCT(45)},{C1_VCA_S,0},
                    {P_RING_DEPTH,PCT(85)},{P_RING_SPEED,PCT(45)},
                    {P_RING_EG_DEPTH,PCT(70)},{P_RING_EG_D,PCT(60)},{P_REV_MIX,PCT(35)}};
PSET(pd_subdrone)  {{C1_FEET,ENV_(0,4)},{C2_FEET,ENV_(0,4)},{C1_WAVE,ENV_(2,3)},
                    {C1_SINE_LVL,PCT(60)},{C1_VCA_A,PCT(90)},{C1_VCA_S,PCT(100)},
                    {C2_TUNE,130},{C2_VCA_A,PCT(90)},{C2_VCA_S,PCT(100)},
                    {P_DRIFT,PCT(30)},{P_REV_MIX,PCT(55)},{P_REV_SIZE,PCT(115)}};
PSET(pd_flute)     {{C1_WAVE,ENV_(2,3)},{C1_SINE_LVL,PCT(85)},{C1_LPF_CUT,PCT(55)},
                    {C1_VCA_A,PCT(15)},{C1_VCA_S,PCT(85)},{C2_LEVEL,PCT(20)},
                    {P_LFO_PITCH,PCT(8)},{P_LFO_RATE,PCT(40)}};
PSET(pd_choirswell){{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(58)},{C1_PWM_DEPTH,PCT(50)},
                    {C1_VCA_A,PCT(75)},{C1_VCA_R,PCT(70)},{C1_VCA_S,PCT(95)},
                    {C2_WAVE,ENV_(1,3)},{C2_PW,PCT(64)},{C2_TUNE,140},
                    {C2_VCA_A,PCT(78)},{C2_VCA_S,PCT(95)},
                    {P_REV_MIX,PCT(55)},{P_REV_SIZE,PCT(110)}};
PSET(pd_brassstab) {{C1_WAVE,ENV_(0,3)},{C1_LPF_CUT,PCT(55)},{C1_LPF_RES,PCT(35)},
                    {C1_VCF_AL,BI(60)},{C1_VCF_A,0},{C1_VCF_D,PCT(20)},
                    {C1_VCA_A,0},{C1_VCA_D,PCT(18)},{C1_VCA_S,PCT(30)},
                    {C2_WAVE,ENV_(0,3)},{C2_TUNE,135},{C2_VCA_D,PCT(20)},{C2_VCA_S,PCT(30)}};
PSET(pd_arppluck)  {{C1_WAVE,ENV_(1,3)},{C1_PW,PCT(30)},{C1_VCA_A,0},
                    {C1_VCA_D,PCT(22)},{C1_VCA_S,0},{C2_LEVEL,PCT(30)},
                    {P_ARP_ENABLE,ENV_(1,2)},{P_ARP_RATE,PCT(60)},{P_ARP_GATE,PCT(65)}};
PSET(pd_arpseq)    {{C1_WAVE,ENV_(0,3)},{C1_LPF_CUT,PCT(60)},{C1_VCA_D,PCT(20)},
                    {C1_VCA_S,PCT(15)},{C2_LEVEL,0},
                    {P_ARP_ENABLE,ENV_(1,2)},{P_ARP_RATE,PCT(70)},{P_ARP_GATE,PCT(75)},
                    {P_ARP_OCTAVES,159},{P_ARP_DIR,ENV_(2,6)}};
PSET(pd_halloween) {{C1_WAVE,ENV_(0,3)},{C1_LPF_CUT,PCT(28)},{C1_LPF_RES,PCT(55)},
                    {C1_VCF_IL,BI(-45)},{C1_VCF_AL,BI(65)},{C1_VCF_A,PCT(60)},
                    {C1_VCF_D,PCT(55)},{C1_VCA_A,PCT(55)},{C1_VCA_S,PCT(90)},
                    {C2_TUNE,146},{C2_LPF_CUT,PCT(24)},{C2_VCF_AL,BI(60)},
                    {C2_VCA_A,PCT(58)},{C2_VCA_S,PCT(90)},
                    {P_DRIFT,PCT(45)},{P_TREM_MIX,PCT(35)},
                    {P_REV_MIX,PCT(60)},{P_REV_SIZE,PCT(120)}};

#define PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

const Preset PRESETS[] = {
    PRESET("CS Brass",     pd_brass),
    PRESET("CS Strings",   pd_strings),
    PRESET("Choir PWM",    pd_choir),
    PRESET("Fat Bass",     pd_bass),
    PRESET("Solo Lead",    pd_lead),
    PRESET("Ring Bells",   pd_bells),
    PRESET("Funk Clav",    pd_funkclav),
    PRESET("Soft Pad",     pd_softpad),
    PRESET("Toots Lead",   pd_toots),
    PRESET("Hollow PWM",   pd_hollow),
    PRESET("Wind Noise",   pd_noisewind),
    PRESET("Ring Lead",    pd_ringlead),
    PRESET("Orchestral",   pd_orch),
    PRESET("Perc Organ",   pd_percorgan),
    PRESET("Sweep Pad",    pd_sweeppad),
    PRESET("Warm Bass",    pd_warmbass),
    PRESET("Deep Bass",    pd_deepbass),
    PRESET("Glass Bells",  pd_glassbells),
    PRESET("Vibrato Lead", pd_vibratolead),
    PRESET("Space Pad",    pd_spacepad),
    PRESET("Glide Lead",   pd_glidelead),
    PRESET("PWM Strings",  pd_pwmstrings),
    PRESET("Metal Bells",  pd_metalbells),
    PRESET("Sub Drone",    pd_subdrone),
    PRESET("Flute",        pd_flute),
    PRESET("Choir Swell",  pd_choirswell),
    PRESET("Brass Stab",   pd_brassstab),
    PRESET("Arp Pluck",    pd_arppluck),
    PRESET("Arp Sequence", pd_arpseq),
    PRESET("Halloween Pad",pd_halloween),
};
const int NUM_PRESETS = (int)(sizeof(PRESETS) / sizeof(PRESETS[0]));

void loadInitPatch() {
    for (uint8_t i = 0; i < NUM_PARAMETERS; i++)
        setParameter(i, (float)INIT_PATCH[i] * (1.0f / 255.0f));
}

void loadPreset(int index) {
    if (index < 0 || index >= NUM_PRESETS) return;
    loadInitPatch();
    const Preset& p = PRESETS[index];
    for (uint8_t i = 0; i < p.n; i++)
        setParameter(p.deltas[i].param, (float)p.deltas[i].value * (1.0f / 255.0f));
}

void paramsInit() { loadInitPatch(); }

void snapshotParams(uint8_t* out) {
    for (uint8_t i = 0; i < NUM_PARAMETERS; i++)
        out[i] = (uint8_t)(allParameterValues[i] * 255.0f + 0.5f);
}
void restoreParams(const uint8_t* in) {
    for (uint8_t i = 0; i < NUM_PARAMETERS; i++)
        setParameter(i, (float)in[i] * (1.0f / 255.0f));
}

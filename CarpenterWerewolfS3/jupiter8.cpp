#include "jupiter8.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  jupiter8.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum Jp8ParamId : uint8_t {
    JP8_OSC1_RANGE = 0, JP8_OSC1_TRI, JP8_OSC1_SAW, JP8_OSC1_PULSE, JP8_OSC1_PW,
    JP8_OSC2_RANGE, JP8_OSC2_LOWFREQ, JP8_OSC2_TRI, JP8_OSC2_SAW, JP8_OSC2_PULSE, JP8_OSC2_PW, JP8_OSC2_FINE, JP8_OSC2_SYNC,
    JP8_NOISE, JP8_CROSSMOD,
    JP8_FILTER_CUTOFF, JP8_FILTER_TYPE, JP8_FILTER_RES, JP8_FILTER_SLOPE, JP8_FILTER_ENV_AMT, JP8_FILTER_KBD,
    JP8_FENV_A, JP8_FENV_D, JP8_FENV_S, JP8_FENV_R,
    JP8_AENV_A, JP8_AENV_D, JP8_AENV_S, JP8_AENV_R,
    JP8_LFO_RATE, JP8_LFO_TO_OSC, JP8_LFO_TO_FILTER, JP8_LFO_TO_PWM,
    JP8_GLIDE_ON, JP8_GLIDE_TIME, JP8_MASTER, JP8_BEND_RANGE,

    JP8_TREM_RATE, JP8_TREM_DEPTH, JP8_TREM_MIX,
    JP8_DLY_TIME, JP8_DLY_FB, JP8_DLY_MIX, JP8_REV_SIZE, JP8_REV_MIX,

    JP8_ARP_ENABLE, JP8_ARP_PATTERN, JP8_ARP_RATE, JP8_ARP_GATE,
    JP8_ARP_OCTAVES, JP8_ARP_DIR, JP8_ARP_LATCH, JP8_ARP_SYNC,

    JP8_NUM_PARAMS
};

static const char* const JP8_RANGE_NAMES[4] = { "16'", "8'", "4'", "2'" };
static const char* const JP8_ONOFF[2]       = { "OFF", "ON" };
static const char* const JP8_SLOPE_NAMES[2] = { "12dB", "24dB" };
static const char* const JP8_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define JP8ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [JP8_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo JP8_PARAM_INFO[] = {
    JP8ROW("O1 RANGE",PK_ENUM,   JP8_RANGE_NAMES, 4, 0, 3,  20),
    JP8ROW("O1 TRI",  PK_CONT,   nullptr, 0,   0, 127,  21),
    JP8ROW("O1 SAW",  PK_CONT,   nullptr, 0,   0, 127,  22),
    JP8ROW("O1 PULSE",PK_CONT,   nullptr, 0,   0, 127,  23),
    JP8ROW("O1 PW",   PK_CONT,   nullptr, 0,   0, 127,  24),
    JP8ROW("O2 RANGE",PK_ENUM,   JP8_RANGE_NAMES, 4, 0, 3,  25),
    JP8ROW("O2 LOWFRQ",PK_TOGGLE,JP8_ONOFF, 2,  0, 1,      26),
    JP8ROW("O2 TRI",  PK_CONT,   nullptr, 0,   0, 127,  27),
    JP8ROW("O2 SAW",  PK_CONT,   nullptr, 0,   0, 127,  28),
    JP8ROW("O2 PULSE",PK_CONT,   nullptr, 0,   0, 127,  29),
    JP8ROW("O2 PW",   PK_CONT,   nullptr, 0,   0, 127,  30),
    JP8ROW("O2 FINE", PK_CENTS,  nullptr, 0,-700, 700,  31),
    JP8ROW("O2 SYNC", PK_TOGGLE, JP8_ONOFF, 2,  0, 1,   33),
    JP8ROW("NOISE",   PK_CONT,   nullptr, 0,   0, 127,  34),
    JP8ROW("CROSS MOD",PK_CONT,  nullptr, 0,   0, 127,  35),
    JP8ROW("CUTOFF",  PK_HZ,     nullptr, 0,  20, 18000, 36),
    JP8ROW("FILT TYPE",PK_ENUM,  VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 37),
    JP8ROW("RESONANCE",PK_CONT,  nullptr, 0,   0, 127,  39),
    JP8ROW("SLOPE",   PK_TOGGLE, JP8_SLOPE_NAMES, 2, 0, 1, 40),
    JP8ROW("VCF ENV AMT",PK_CONT,nullptr, 0,   0, 127,  41),
    JP8ROW("KBD AMT", PK_CONT,   nullptr, 0,   0, 127,  42),
    JP8ROW("VCF ATK", PK_MS,     nullptr, 0,   1, 8000,  43),
    JP8ROW("VCF DEC", PK_MS,     nullptr, 0,   3, 12000, 44),
    JP8ROW("VCF SUS", PK_CONT,   nullptr, 0,   0, 127,   45),
    JP8ROW("VCF REL", PK_MS,     nullptr, 0,   3, 12000, 46),
    JP8ROW("VCA ATK", PK_MS,     nullptr, 0,   1, 8000,  47),
    JP8ROW("VCA DEC", PK_MS,     nullptr, 0,   3, 12000, 48),
    JP8ROW("VCA SUS", PK_CONT,   nullptr, 0,   0, 127,   49),
    JP8ROW("VCA REL", PK_MS,     nullptr, 0,   3, 12000, 50),
    JP8ROW("LFO RATE",PK_HZ,     nullptr, 0,   0, 20,    51),
    JP8ROW("LFO>OSC", PK_CONT,   nullptr, 0,   0, 127,   52),
    JP8ROW("LFO>VCF", PK_CONT,   nullptr, 0,   0, 127,   53),
    JP8ROW("LFO>PWM", PK_CONT,   nullptr, 0,   0, 127,   54),
    JP8ROW("GLIDE",   PK_TOGGLE, JP8_ONOFF, 2,  0, 1,    55),
    JP8ROW("GLIDE TIME",PK_MS,   nullptr, 0,   0, 5000,  56),
    JP8ROW("MASTER",  PK_CONT,   nullptr, 0,   0, 127,   57),
    JP8ROW("BEND RNG",PK_INT,    nullptr, 0,   1, 24,    58),
    JP8ROW("TREM RATE",PK_HZ,    nullptr, 0,   0, 20,    59),
    JP8ROW("TREM DEPTH",PK_CONT, nullptr, 0,   0, 127,   60),
    JP8ROW("TREM MIX",PK_CONT,   nullptr, 0,   0, 127,   61),
    JP8ROW("DLY TIME",PK_MS,     nullptr, 0,  20, 1200,  62),
    JP8ROW("DLY FDBK",PK_CONT,   nullptr, 0,   0, 127,   63),
    JP8ROW("DLY MIX", PK_CONT,   nullptr, 0,   0, 127,   65),
    JP8ROW("REV SIZE",PK_CONT,   nullptr, 0,   0, 127,   66),
    JP8ROW("REV MIX", PK_CONT,   nullptr, 0,   0, 127,   67),
    JP8ROW("ARP ON",  PK_TOGGLE, JP8_ONOFF, 2,  0, 1,    68),
    JP8ROW("ARP PATT",PK_INT,    nullptr, 0,   0, 7,     69),
    JP8ROW("ARP RATE",PK_CONT,   nullptr, 0,   0, 127,   70),
    JP8ROW("ARP GATE",PK_CONT,   nullptr, 0,   0, 127,   71),
    JP8ROW("ARP OCT", PK_INT,    nullptr, 0,   1, 4,     72),
    JP8ROW("ARP DIR", PK_ENUM,   JP8_ARP_DIR_NAMES, 6, 0, 5, 73),
    JP8ROW("ARP LATCH",PK_TOGGLE,JP8_ONOFF, 2,  0, 1,    74),
    JP8ROW("ARP SYNC",PK_TOGGLE, JP8_ONOFF, 2,  0, 1,    75),
};
static_assert(sizeof(JP8_PARAM_INFO) / sizeof(JP8_PARAM_INFO[0]) == JP8_NUM_PARAMS,
              "JP8_PARAM_INFO must have exactly JP8_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float jp8Raw[JP8_NUM_PARAMS];

struct Jp8Decoded {
    float range1Mul; float tri1, saw1, pulse1, pwA;
    float range2Mul; bool lowFreq2; float tri2, saw2, pulse2, pwB, fineB; bool sync2;
    float noiseLvl, crossModAmt;
    float cutoffHz; uint8_t filterType; float resonance; bool slope24; float envAmt, kbdAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    float lfoRateHz, lfoToOsc, lfoToFilter, lfoToPwm;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static Jp8Decoded jp8;

static inline float rangeMulFromEnum(float v) {
    static const float TAB[4] = { 0.5f, 1.0f, 2.0f, 4.0f };
    int i = (int)(v * 3.999f); if (i < 0) i = 0; else if (i > 3) i = 3;
    return TAB[i];
}
static const float JP8_LOWFREQ_HZ[4] = { 0.2f, 1.0f, 4.0f, 16.0f };

static void jp8Decode() {
    jp8.range1Mul = rangeMulFromEnum(jp8Raw[JP8_OSC1_RANGE]);
    jp8.tri1 = jp8Raw[JP8_OSC1_TRI]; jp8.saw1 = jp8Raw[JP8_OSC1_SAW]; jp8.pulse1 = jp8Raw[JP8_OSC1_PULSE];
    jp8.pwA = 0.05f + jp8Raw[JP8_OSC1_PW] * 0.45f;

    jp8.range2Mul = rangeMulFromEnum(jp8Raw[JP8_OSC2_RANGE]);
    jp8.lowFreq2 = jp8Raw[JP8_OSC2_LOWFREQ] > 0.5f;
    jp8.tri2 = jp8Raw[JP8_OSC2_TRI]; jp8.saw2 = jp8Raw[JP8_OSC2_SAW]; jp8.pulse2 = jp8Raw[JP8_OSC2_PULSE];
    jp8.pwB = 0.05f + jp8Raw[JP8_OSC2_PW] * 0.45f;
    jp8.fineB = fastPow2(((jp8Raw[JP8_OSC2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    jp8.sync2 = jp8Raw[JP8_OSC2_SYNC] > 0.5f;

    jp8.noiseLvl = jp8Raw[JP8_NOISE];
    jp8.crossModAmt = jp8Raw[JP8_CROSSMOD];

    jp8.cutoffHz = 20.0f * fastPow2(jp8Raw[JP8_FILTER_CUTOFF] * 9.8f);
    jp8.filterType = (uint8_t)(jp8Raw[JP8_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    jp8.resonance = jp8Raw[JP8_FILTER_RES];
    jp8.slope24 = jp8Raw[JP8_FILTER_SLOPE] > 0.5f;
    jp8.envAmt = jp8Raw[JP8_FILTER_ENV_AMT];
    jp8.kbdAmt = jp8Raw[JP8_FILTER_KBD];

    jp8.fA = 1.0f / (1.0f + jp8Raw[JP8_FENV_A] * 800.0f);
    jp8.fD = 1.0f / (1.0f + jp8Raw[JP8_FENV_D] * 1600.0f);
    jp8.fS = jp8Raw[JP8_FENV_S];
    jp8.fR = 1.0f / (1.0f + jp8Raw[JP8_FENV_R] * 1600.0f);
    jp8.aA = 1.0f / (1.0f + jp8Raw[JP8_AENV_A] * 800.0f);
    jp8.aD = 1.0f / (1.0f + jp8Raw[JP8_AENV_D] * 1600.0f);
    jp8.aS = jp8Raw[JP8_AENV_S];
    jp8.aR = 1.0f / (1.0f + jp8Raw[JP8_AENV_R] * 1600.0f);

    jp8.lfoRateHz = 0.1f + jp8Raw[JP8_LFO_RATE] * 19.9f;
    jp8.lfoToOsc = jp8Raw[JP8_LFO_TO_OSC];
    jp8.lfoToFilter = jp8Raw[JP8_LFO_TO_FILTER];
    jp8.lfoToPwm = jp8Raw[JP8_LFO_TO_PWM];

    jp8.glideOn = jp8Raw[JP8_GLIDE_ON] > 0.5f;
    jp8.glideTimeS = 0.005f + jp8Raw[JP8_GLIDE_TIME] * 5.0f;
    jp8.master = jp8Raw[JP8_MASTER];
    jp8.bendRangeSemis = 1.0f + jp8Raw[JP8_BEND_RANGE] * 23.0f;

    jp8.tremRate = jp8Raw[JP8_TREM_RATE] * 20.0f;
    jp8.tremDepth = jp8Raw[JP8_TREM_DEPTH];
    jp8.tremMix = jp8Raw[JP8_TREM_MIX];
    jp8.dlyTimeMs = 20.0f + jp8Raw[JP8_DLY_TIME] * 1180.0f;
    jp8.dlyFb = jp8Raw[JP8_DLY_FB];
    jp8.dlyMix = jp8Raw[JP8_DLY_MIX];
    jp8.revSize = jp8Raw[JP8_REV_SIZE];
    jp8.revMix = jp8Raw[JP8_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) for why every PK_ENUM/PK_TOGGLE delta below
// that isn't option 0 goes through it.
static const uint8_t JP8_INIT[] = {
    EI(1,4), 0,110,0,64,          // osc1: 8', tri off, saw, pulse off, pw
    EI(1,4), 0, 0,90,0,64,64,0,   // osc2: 8', not lowfreq, tri off, saw, pulse off, pw, fine(in tune), sync off
    0,0,                          // noise/crossmod
    100,0,20,0,60,0,              // filter cutoff/type/res/slope(12dB)/envamt/kbd
    20,60,90,40,                  // filter env ADSR
    5,70,110,50,                  // amp env ADSR
    0,0,0,0,                      // LFO rate/to-osc/to-filter/to-pwm
    0,64,                         // glide/glidetime
    110,20,                       // master/bend
    0,0,0, 0,0,0,0,0,             // output
    0,0,64,80,0,0,0,0,            // arp
};
static_assert(sizeof(JP8_INIT) / sizeof(JP8_INIT[0]) == JP8_NUM_PARAMS,
              "JP8_INIT must have exactly JP8_NUM_PARAMS values");

#define JP8_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta JP8_P_BASS[]      = { {JP8_OSC1_RANGE,0},{JP8_OSC2_RANGE,0},{JP8_OSC1_SAW,110},{JP8_OSC2_PULSE,90},{JP8_OSC2_FINE,60},{JP8_FILTER_CUTOFF,25},{JP8_FILTER_RES,55},{JP8_FILTER_ENV_AMT,90},{JP8_FENV_D,30} };
static const PresetDelta JP8_P_SUBBASS[]   = { {JP8_OSC1_RANGE,0},{JP8_OSC1_TRI,110},{JP8_OSC2_PULSE,0},{JP8_FILTER_CUTOFF,20},{JP8_FILTER_RES,20},{JP8_AENV_D,40},{JP8_AENV_S,90} };
static const PresetDelta JP8_P_XMODBASS[]  = { {JP8_OSC1_RANGE,0},{JP8_OSC1_SAW,110},{JP8_OSC2_RANGE,EI(2,4)},{JP8_OSC2_SAW,60},{JP8_CROSSMOD,45},{JP8_FILTER_CUTOFF,30},{JP8_FILTER_RES,60},{JP8_FILTER_ENV_AMT,60},{JP8_FENV_D,30} };
static const PresetDelta JP8_P_SYNCBASS[]  = { {JP8_OSC1_RANGE,0},{JP8_OSC1_SAW,110},{JP8_OSC2_RANGE,EI(2,4)},{JP8_OSC2_SAW,90},{JP8_OSC2_SYNC,EI(1,2)},{JP8_FILTER_CUTOFF,45},{JP8_FILTER_RES,50},{JP8_FENV_D,25} };
static const PresetDelta JP8_P_RESOBASS[]  = { {JP8_OSC1_RANGE,0},{JP8_OSC1_SAW,110},{JP8_FILTER_CUTOFF,26},{JP8_FILTER_RES,120},{JP8_FILTER_ENV_AMT,80},{JP8_FENV_D,28} };

// ── Leads ──
static const PresetDelta JP8_P_LEAD[]      = { {JP8_OSC1_SAW,110},{JP8_OSC2_SAW,90},{JP8_OSC2_FINE,72},{JP8_FILTER_CUTOFF,80},{JP8_FILTER_RES,55},{JP8_FILTER_ENV_AMT,30},{JP8_FENV_A,0} };
static const PresetDelta JP8_P_XMODLEAD[]  = { {JP8_OSC1_SAW,100},{JP8_OSC2_TRI,90},{JP8_CROSSMOD,60},{JP8_FILTER_CUTOFF,75},{JP8_FILTER_RES,60},{JP8_FENV_A,0} };
static const PresetDelta JP8_P_SYNCLEAD[]  = { {JP8_OSC1_SAW,110},{JP8_OSC2_SAW,90},{JP8_OSC2_RANGE,EI(2,4)},{JP8_OSC2_SYNC,EI(1,2)},{JP8_FILTER_CUTOFF,85},{JP8_FILTER_RES,65},{JP8_LFO_TO_OSC,25},{JP8_LFO_RATE,55} };
static const PresetDelta JP8_P_PWMLEAD[]   = { {JP8_OSC1_PULSE,110},{JP8_OSC1_PW,50},{JP8_LFO_TO_PWM,45},{JP8_LFO_RATE,20},{JP8_FILTER_CUTOFF,70},{JP8_FILTER_RES,50} };
static const PresetDelta JP8_P_GLIDELEAD[] = { {JP8_OSC1_SAW,110},{JP8_OSC2_SAW,90},{JP8_OSC2_FINE,74},{JP8_FILTER_CUTOFF,78},{JP8_FILTER_RES,55},{JP8_GLIDE_ON,EI(1,2)},{JP8_GLIDE_TIME,18} };
static const PresetDelta JP8_P_24DBLEAD[]  = { {JP8_OSC1_SAW,110},{JP8_FILTER_CUTOFF,70},{JP8_FILTER_RES,80},{JP8_FILTER_SLOPE,EI(1,2)},{JP8_FILTER_ENV_AMT,50},{JP8_FENV_D,35} };

// ── Pads ──
static const PresetDelta JP8_P_LUSHPAD[]   = { {JP8_OSC1_SAW,90},{JP8_OSC2_SAW,80},{JP8_OSC2_FINE,74},{JP8_FILTER_CUTOFF,55},{JP8_FILTER_RES,20},{JP8_FENV_A,70},{JP8_AENV_A,80},{JP8_AENV_D,50},{JP8_AENV_S,100},{JP8_TREM_MIX,35} };
static const PresetDelta JP8_P_STRINGPAD[] = { {JP8_OSC1_SAW,90},{JP8_OSC1_PULSE,40},{JP8_OSC2_SAW,80},{JP8_OSC2_FINE,72},{JP8_FILTER_CUTOFF,60},{JP8_AENV_A,60},{JP8_AENV_D,40},{JP8_AENV_S,100},{JP8_TREM_MIX,45} };
static const PresetDelta JP8_P_BRIGHTPAD[] = { {JP8_OSC1_SAW,90},{JP8_OSC2_PULSE,80},{JP8_OSC2_PW,55},{JP8_OSC2_FINE,73},{JP8_FILTER_CUTOFF,80},{JP8_AENV_A,70},{JP8_AENV_S,100} };
static const PresetDelta JP8_P_XMODPAD[]   = { {JP8_OSC1_SAW,80},{JP8_OSC2_TRI,75},{JP8_CROSSMOD,25},{JP8_FILTER_CUTOFF,65},{JP8_AENV_A,90},{JP8_AENV_S,100},{JP8_REV_MIX,45} };
static const PresetDelta JP8_P_SWEEPPAD[]  = { {JP8_OSC1_SAW,90},{JP8_OSC2_SAW,80},{JP8_OSC2_FINE,73},{JP8_FILTER_CUTOFF,20},{JP8_FILTER_RES,55},{JP8_FILTER_ENV_AMT,100},{JP8_FENV_A,100},{JP8_FENV_D,110},{JP8_AENV_A,80},{JP8_AENV_S,100} };

// ── Brass ──
static const PresetDelta JP8_P_BRASS[]     = { {JP8_OSC1_SAW,110},{JP8_OSC1_PULSE,40},{JP8_OSC2_SAW,100},{JP8_OSC2_FINE,72},{JP8_FILTER_CUTOFF,55},{JP8_FILTER_ENV_AMT,85},{JP8_FENV_A,15},{JP8_FENV_D,50},{JP8_AENV_A,10} };
static const PresetDelta JP8_P_BRASSSTAB[] = { {JP8_OSC1_SAW,110},{JP8_OSC2_SAW,100},{JP8_OSC2_FINE,72},{JP8_FILTER_CUTOFF,60},{JP8_FILTER_ENV_AMT,90},{JP8_FENV_A,0},{JP8_FENV_D,20},{JP8_AENV_D,25} };
static const PresetDelta JP8_P_SYNCBRASS[] = { {JP8_OSC1_SAW,100},{JP8_OSC2_SAW,90},{JP8_OSC2_RANGE,EI(2,4)},{JP8_OSC2_SYNC,EI(1,2)},{JP8_FILTER_CUTOFF,60},{JP8_FILTER_ENV_AMT,70},{JP8_FENV_A,10},{JP8_AENV_A,8} };

// ── Bells/plucks/metallic ──
static const PresetDelta JP8_P_METALBELL[] = { {JP8_OSC1_SAW,90},{JP8_OSC2_TRI,90},{JP8_CROSSMOD,90},{JP8_FILTER_CUTOFF,80},{JP8_FILTER_RES,40},{JP8_FENV_D,45},{JP8_AENV_D,50},{JP8_AENV_S,0} };
static const PresetDelta JP8_P_EPIANO[]    = { {JP8_OSC1_PULSE,90},{JP8_OSC1_PW,35},{JP8_OSC2_TRI,60},{JP8_CROSSMOD,15},{JP8_FILTER_CUTOFF,65},{JP8_FENV_D,35},{JP8_AENV_D,45},{JP8_AENV_S,30},{JP8_AENV_R,40} };
static const PresetDelta JP8_P_PLUCK[]     = { {JP8_OSC1_SAW,100},{JP8_FILTER_CUTOFF,75},{JP8_FILTER_RES,60},{JP8_FILTER_ENV_AMT,60},{JP8_FENV_D,25},{JP8_AENV_D,30},{JP8_AENV_S,0} };

// ── SFX ──
static const PresetDelta JP8_P_NOISESWEEP[]= { {JP8_OSC1_SAW,0},{JP8_NOISE,110},{JP8_FILTER_CUTOFF,25},{JP8_FILTER_RES,90},{JP8_FILTER_ENV_AMT,120},{JP8_FENV_A,90},{JP8_FENV_D,100},{JP8_AENV_A,10},{JP8_AENV_S,110} };
static const PresetDelta JP8_P_XMODFX[]    = { {JP8_OSC1_SAW,90},{JP8_OSC2_SAW,90},{JP8_CROSSMOD,127},{JP8_FILTER_CUTOFF,70},{JP8_FILTER_RES,70},{JP8_AENV_A,20},{JP8_AENV_S,100} };
static const PresetDelta JP8_P_LOWFREQFX[] = { {JP8_OSC1_SAW,100},{JP8_OSC2_LOWFREQ,EI(1,2)},{JP8_OSC2_RANGE,EI(3,4)},{JP8_CROSSMOD,70},{JP8_FILTER_CUTOFF,60},{JP8_FILTER_RES,50},{JP8_AENV_A,10},{JP8_AENV_S,110} };

// ── Bells/plucks/metallic (more) ──
static const PresetDelta JP8_P_GLASSPLUCK[]= { {JP8_OSC1_SAW,90},{JP8_OSC2_SAW,85},{JP8_OSC2_FINE,90},{JP8_FILTER_CUTOFF,95},{JP8_FILTER_RES,50},{JP8_FENV_D,25},{JP8_AENV_D,30},{JP8_AENV_S,0} };
static const PresetDelta JP8_P_WARMBRASS[] = { {JP8_OSC1_TRI,60},{JP8_OSC1_SAW,80},{JP8_OSC2_SAW,80},{JP8_OSC2_FINE,71},{JP8_FILTER_CUTOFF,48},{JP8_FILTER_ENV_AMT,55},{JP8_FENV_A,25},{JP8_AENV_A,20} };

// ── Arp ──
static const PresetDelta JP8_P_ARPCLASSIC[]= { {JP8_OSC1_PULSE,90},{JP8_OSC1_PW,40},{JP8_FILTER_CUTOFF,60},{JP8_FILTER_RES,50},{JP8_FILTER_ENV_AMT,50},{JP8_FENV_D,20},{JP8_AENV_D,18},{JP8_AENV_S,30},{JP8_ARP_ENABLE,EI(1,2)},{JP8_ARP_RATE,80},{JP8_ARP_GATE,70} };
static const PresetDelta JP8_P_ARPOCTAVES[]= { {JP8_OSC1_SAW,100},{JP8_FILTER_CUTOFF,70},{JP8_FILTER_RES,40},{JP8_ARP_ENABLE,EI(1,2)},{JP8_ARP_RATE,90},{JP8_ARP_GATE,80},{JP8_ARP_OCTAVES,80},{JP8_ARP_DIR,EI(2,6)} };
static const PresetDelta JP8_P_ARPPULSE[]  = { {JP8_OSC1_PULSE,100},{JP8_OSC1_PW,25},{JP8_OSC2_PULSE,80},{JP8_OSC2_FINE,68},{JP8_FILTER_CUTOFF,65},{JP8_ARP_ENABLE,EI(1,2)},{JP8_ARP_RATE,90},{JP8_ARP_GATE,75} };

static const Preset JP8_PRESETS[] = {
    JP8_PRESET("JP8 BASS",     JP8_P_BASS),
    JP8_PRESET("SUB BASS",     JP8_P_SUBBASS),
    JP8_PRESET("XMOD BASS",    JP8_P_XMODBASS),
    JP8_PRESET("SYNC BASS",    JP8_P_SYNCBASS),
    JP8_PRESET("RESO BASS",    JP8_P_RESOBASS),
    JP8_PRESET("JP8 LEAD",     JP8_P_LEAD),
    JP8_PRESET("XMOD LEAD",    JP8_P_XMODLEAD),
    JP8_PRESET("SYNC LEAD",    JP8_P_SYNCLEAD),
    JP8_PRESET("PWM LEAD",     JP8_P_PWMLEAD),
    JP8_PRESET("GLIDE LEAD",   JP8_P_GLIDELEAD),
    JP8_PRESET("24DB LEAD",    JP8_P_24DBLEAD),
    JP8_PRESET("LUSH PAD",     JP8_P_LUSHPAD),
    JP8_PRESET("STRING PAD",   JP8_P_STRINGPAD),
    JP8_PRESET("BRIGHT PAD",   JP8_P_BRIGHTPAD),
    JP8_PRESET("XMOD PAD",     JP8_P_XMODPAD),
    JP8_PRESET("SWEEP PAD",    JP8_P_SWEEPPAD),
    JP8_PRESET("JP8 BRASS",    JP8_P_BRASS),
    JP8_PRESET("BRASS STAB",   JP8_P_BRASSSTAB),
    JP8_PRESET("SYNC BRASS",   JP8_P_SYNCBRASS),
    JP8_PRESET("METAL BELL",   JP8_P_METALBELL),
    JP8_PRESET("E.PIANO",      JP8_P_EPIANO),
    JP8_PRESET("PLUCK",        JP8_P_PLUCK),
    JP8_PRESET("GLASS PLUCK",  JP8_P_GLASSPLUCK),
    JP8_PRESET("WARM BRASS",   JP8_P_WARMBRASS),
    JP8_PRESET("NOISE SWEEP",  JP8_P_NOISESWEEP),
    JP8_PRESET("XMOD FX",      JP8_P_XMODFX),
    JP8_PRESET("LOWFREQ FX",   JP8_P_LOWFREQFX),
    JP8_PRESET("ARP CLASSIC",  JP8_P_ARPCLASSIC),
    JP8_PRESET("ARP OCTAVES",  JP8_P_ARPOCTAVES),
    JP8_PRESET("ARP PULSE",    JP8_P_ARPPULSE),
};
#define JP8_NUM_PRESETS (sizeof(JP8_PRESETS) / sizeof(JP8_PRESETS[0]))

static void jp8ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < JP8_NUM_PARAMS; i++) jp8Raw[i] = (float)JP8_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) jp8Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    jp8Decode();
}

static const EnginePageDef JP8_PAGES[] = {
    { "OSC 1/2",    JP8_OSC1_RANGE,   9 },
    { "MOD",        JP8_OSC2_SYNC,    4 },
    { "FILTER",     JP8_FILTER_CUTOFF,6 },
    { "ENVELOPES",  JP8_FENV_A,       8 },
    { "VOICING",    JP8_LFO_RATE,     6 },
    { "OUTPUT",     JP8_TREM_RATE,    8 },
    { "ARP",        JP8_ARP_ENABLE,   8 },
};
#define JP8_NUM_PAGES (sizeof(JP8_PAGES) / sizeof(JP8_PAGES[0]))

uint8_t jp8MasterParamIdE() { return JP8_MASTER; }
uint8_t jp8FilterCutoffParamIdE()    { return JP8_FILTER_CUTOFF; }
uint8_t jp8FilterResonanceParamIdE() { return JP8_FILTER_RES; }
uint8_t jp8NumPagesE() { return (uint8_t)JP8_NUM_PAGES; }
const EnginePageDef* jp8PageE(uint8_t idx) { return (idx < JP8_NUM_PAGES) ? &JP8_PAGES[idx] : &JP8_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static Jp8Voice jp8v[JP8_VOICES];
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void jp8EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void jp8EngineActivate() {
    for (int i = 0; i < JP8_VOICES; i++) jp8v[i] = Jp8Voice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void jp8ParamsInitE() { jp8ApplyDeltas(nullptr, 0); }
void jp8SetParameterE(uint8_t id, float v) { if (id < JP8_NUM_PARAMS) { jp8Raw[id] = v; jp8Decode(); } }
float jp8GetParameterE(uint8_t id) { return (id < JP8_NUM_PARAMS) ? jp8Raw[id] : 0.0f; }
void jp8FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= JP8_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = JP8_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(jp8Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(jp8Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool jp8ParamIsLockableE(uint8_t id) { return id < JP8_NUM_PARAMS; }
uint8_t jp8NumParamsE() { return JP8_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo JP8_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* jp8ParamInfoE(uint8_t id) {
    return (id < JP8_NUM_PARAMS) ? &JP8_PARAM_INFO[id] : &JP8_INVALID_PARAM;
}
uint8_t jp8NumPresetsE() { return (uint8_t)JP8_NUM_PRESETS; }
const char* jp8PresetNameE(uint8_t idx) { return JP8_PRESETS[idx].name; }
void jp8LoadPresetE(uint8_t idx) { jp8ApplyDeltas(JP8_PRESETS[idx].deltas, JP8_PRESETS[idx].n); }
void jp8LoadInitPatchE() { jp8ApplyDeltas(nullptr, 0); }
void jp8SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < JP8_NUM_PARAMS; i++) out[i] = (uint8_t)(jp8Raw[i] * 255.0f + 0.5f);
}
void jp8RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < JP8_NUM_PARAMS; i++) jp8Raw[i] = (float)in[i] * (1.0f / 255.0f);
    jp8Decode();
}

uint8_t jp8NumVoicesE() { return JP8_VOICES; }
void    jp8VoiceInitE() { jp8EngineActivate(); }

void jp8NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= JP8_VOICES) return;
    Jp8Voice& v = jp8v[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (jp8.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (jp8.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(jp8.fA, jp8.fD, jp8.fS, jp8.fR);
        v.ampEnv.trigger(jp8.aA, jp8.aD, jp8.aS, jp8.aR);
    }
}
void jp8NoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= JP8_VOICES) return;
    Jp8Voice& v = jp8v[voiceIdx];
    v.gate = false;
    v.filtEnv.release();
    v.ampEnv.release();
}
void jp8AllOffE() { for (int i = 0; i < JP8_VOICES; i++) jp8NoteOffE(i); }
void jp8SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < JP8_VOICES) jp8v[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int jp8RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * jp8.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = jp8.tremMix > 0.004f;
    const bool fxDelay  = jp8.dlyMix  > 0.004f;
    const bool fxReverb = jp8.revMix  > 0.004f;
    g_fxChorus.setRate(jp8.tremRate);
    g_fxReverb.setRoom(jp8.revSize, 0.4f);
    g_fxDelay.setTimeMs(jp8.dlyTimeMs, false);

    static uint32_t lfoPhase = 0;
    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        lfoPhase += (uint32_t)(jp8.lfoRateHz * PHASE_PER_HZ);
        const float lfo = sineFromPhase(lfoPhase);
        const float lfoOscSemis = lfo * jp8.lfoToOsc * modWheel * 2.0f;
        const float lfoFiltSemis = lfo * jp8.lfoToFilter * modWheel * 24.0f;
        const float lfoPwmAmt = lfo * jp8.lfoToPwm * modWheel * 0.4f;

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < JP8_VOICES; i++) {
            Jp8Voice& v = jp8v[i];
            const bool alive = v.ampEnv.stage != JP8_E_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                float base = v.currentFreq * bendMul;
                float freq1 = base * jp8.range1Mul * fastPow2(lfoOscSemis / 12.0f);
                float freq2;
                if (jp8.lowFreq2) {
                    int oi = (int)(jp8Raw[JP8_OSC2_RANGE] * 3.999f); if (oi < 0) oi = 0; else if (oi > 3) oi = 3;
                    freq2 = JP8_LOWFREQ_HZ[oi];
                } else {
                    freq2 = base * jp8.range2Mul * jp8.fineB * fastPow2(lfoOscSemis / 12.0f);
                }
                v.osc1.setFreq(freq1);
                v.osc2.setFreq(freq2);

                const float envF = v.filtEnv.process();
                float kbdSemis = jp8.kbdAmt * (float)(v.note - 60);
                float cutoff = safeClamp(
                    jp8.cutoffHz * fastPow2((kbdSemis + jp8.envAmt * envF * 48.0f + lfoFiltSemis) / 12.0f),
                    20.0f, 18000.0f);
                if (jp8.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, jp8.resonance);
                else                             v.varFilt.setCutoff(cutoff, jp8.resonance, jp8.filterType);
            }

            bool wrapped2 = false;
            float tB = v.osc2.advance(&wrapped2);
            float o2raw = v.osc2.tri(tB) * jp8.tri2 + v.osc2.saw(tB) * jp8.saw2
                        + v.osc2.pulse(tB, jp8.pwB) * jp8.pulse2;

            if (jp8.crossModAmt > 0.001f) {
                float fmHz = v.osc1.baseHz * fastPow2(o2raw * jp8.crossModAmt * (2.0f / 12.0f));
                v.osc1.setFmHz(fmHz);
            }
            if (jp8.sync2 && wrapped2) v.osc1.resetPhase();
            float tA = v.osc1.advance();

            float pwA = jp8.pwA + lfoPwmAmt; if (pwA < 0.05f) pwA = 0.05f; else if (pwA > 0.95f) pwA = 0.95f;
            float o1raw = v.osc1.tri(tA) * jp8.tri1 + v.osc1.saw(tA) * jp8.saw1
                        + v.osc1.pulse(tA, pwA) * jp8.pulse1;
            float noiseOut = v.noise.white() * jp8.noiseLvl;

            float voiceIn = (o1raw + o2raw + noiseOut) * 0.4f;
            float filtered = (jp8.filterType == VF_STOCK)
                ? v.filter.process(voiceIn, jp8.slope24)
                : v.varFilt.process(voiceIn, jp8.filterType, jp8.resonance);

            float amp = v.ampEnv.process();
            float velScale = 0.5f + 0.5f * v.velNorm;

            mix += filtered * amp * velScale;
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER used to be multiplied straight into the saturator below, so
        // turning the volume up also drove more distortion. JP8_MIX_DRIVE
        // is a fixed pre-saturator drive instead, matching every other
        // engine's own fixed-drive fix for the same bug. MASTER itself is
        // now applied as pure linear gain after the saturator and the whole
        // effect chain.
        const float JP8_MIX_DRIVE = 1.4f;
        float out = fastTanh(mix * JP8_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, jp8.tremDepth, jp8.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, jp8.dlyFb, jp8.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * jp8.revMix; r += rr * jp8.revMix;
        }
        l *= jp8.master; r *= jp8.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

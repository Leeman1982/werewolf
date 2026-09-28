#include "moogmodular.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  moogmodular.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum Mm2ParamId : uint8_t {
    MM2_OSC1_WAVE = 0, MM2_OSC1_OCT, MM2_OSC1_LEVEL,
    MM2_OSC2_WAVE, MM2_OSC2_OCT, MM2_OSC2_FINE, MM2_OSC2_LEVEL, MM2_OSC2_SYNC,
    MM2_OSC3_WAVE, MM2_OSC3_OCT, MM2_OSC3_FINE, MM2_OSC3_LEVEL, MM2_OSC3_TO_OSC1_FM,
    MM2_RING_ON, MM2_RING_MIX,
    MM2_NOISE,
    MM2_SH_RATE, MM2_SH_TO_PITCH, MM2_SH_TO_FILTER,
    MM2_FILTER_CUTOFF, MM2_FILTER_TYPE, MM2_FILTER_RES, MM2_FILTER_ENV_AMT, MM2_FILTER_KBD,
    MM2_FENV_A, MM2_FENV_D, MM2_FENV_S, MM2_FENV_R, MM2_FENV_TO_PITCH,
    MM2_AENV_A, MM2_AENV_D, MM2_AENV_S, MM2_AENV_R,
    MM2_GLIDE_ON, MM2_GLIDE_TIME, MM2_MASTER, MM2_BEND_RANGE,

    MM2_TREM_RATE, MM2_TREM_DEPTH, MM2_TREM_MIX,
    MM2_DLY_TIME, MM2_DLY_FB, MM2_DLY_MIX, MM2_REV_SIZE, MM2_REV_MIX,

    MM2_ARP_ENABLE, MM2_ARP_PATTERN, MM2_ARP_RATE, MM2_ARP_GATE,
    MM2_ARP_OCTAVES, MM2_ARP_DIR, MM2_ARP_LATCH, MM2_ARP_SYNC,

    MM2_NUM_PARAMS
};

static const char* const MM2_WAVE_NAMES[4] = { "TRI", "SAW", "PULSE", "SINE" };
static const char* const MM2_OCT_NAMES[5]  = { "32'", "16'", "8'", "4'", "2'" };
static const char* const MM2_ONOFF[2]      = { "OFF", "ON" };
static const char* const MM2_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define MM2ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [MM2_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo MM2_PARAM_INFO[] = {
    MM2ROW("O1 WAVE", PK_ENUM,   MM2_WAVE_NAMES, 4, 0, 3,  20),
    MM2ROW("O1 OCT",  PK_ENUM,   MM2_OCT_NAMES,  5, 0, 4,  21),
    MM2ROW("O1 LEVEL",PK_CONT,   nullptr, 0,   0, 127,  22),
    MM2ROW("O2 WAVE", PK_ENUM,   MM2_WAVE_NAMES, 4, 0, 3,  23),
    MM2ROW("O2 OCT",  PK_ENUM,   MM2_OCT_NAMES,  5, 0, 4,  24),
    MM2ROW("O2 FINE", PK_CENTS,  nullptr, 0,-700, 700,  25),
    MM2ROW("O2 LEVEL",PK_CONT,   nullptr, 0,   0, 127,  26),
    MM2ROW("O2 SYNC", PK_TOGGLE, MM2_ONOFF, 2, 0, 1,    27),
    MM2ROW("O3 WAVE", PK_ENUM,   MM2_WAVE_NAMES, 4, 0, 3,  28),
    MM2ROW("O3 OCT",  PK_ENUM,   MM2_OCT_NAMES,  5, 0, 4,  29),
    MM2ROW("O3 FINE", PK_CENTS,  nullptr, 0,-700, 700,  30),
    MM2ROW("O3 LEVEL",PK_CONT,   nullptr, 0,   0, 127,  31),
    MM2ROW("O3>O1 FM",PK_CONT,   nullptr, 0,   0, 127,  33),
    MM2ROW("RING ON", PK_TOGGLE, MM2_ONOFF, 2, 0, 1,    34),
    MM2ROW("RING MIX",PK_CONT,   nullptr, 0,   0, 127,  35),
    MM2ROW("NOISE",   PK_CONT,   nullptr, 0,   0, 127,  36),
    MM2ROW("S&H RATE",PK_HZ,     nullptr, 0,   0, 40,   37),
    MM2ROW("S&H>PITCH",PK_CONT,  nullptr, 0,   0, 127,  39),
    MM2ROW("S&H>VCF", PK_CONT,   nullptr, 0,   0, 127,  40),
    MM2ROW("CUTOFF",  PK_HZ,     nullptr, 0,  20, 18000, 41),
    MM2ROW("FILT TYPE",PK_ENUM,  VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 42),
    MM2ROW("RESONANCE",PK_CONT,  nullptr, 0,   0, 127,  43),
    MM2ROW("VCF ENV AMT",PK_CONT,nullptr, 0,   0, 127,  44),
    MM2ROW("KBD AMT", PK_CONT,   nullptr, 0,   0, 127,  45),
    MM2ROW("VCF ATK", PK_MS,     nullptr, 0,   1, 8000,  46),
    MM2ROW("VCF DEC", PK_MS,     nullptr, 0,   3, 12000, 47),
    MM2ROW("VCF SUS", PK_CONT,   nullptr, 0,   0, 127,   48),
    MM2ROW("VCF REL", PK_MS,     nullptr, 0,   3, 12000, 49),
    MM2ROW("VCF>PITCH",PK_CONT,  nullptr, 0,   0, 127,   50),
    MM2ROW("VCA ATK", PK_MS,     nullptr, 0,   1, 8000,  51),
    MM2ROW("VCA DEC", PK_MS,     nullptr, 0,   3, 12000, 52),
    MM2ROW("VCA SUS", PK_CONT,   nullptr, 0,   0, 127,   53),
    MM2ROW("VCA REL", PK_MS,     nullptr, 0,   3, 12000, 54),
    MM2ROW("GLIDE",   PK_TOGGLE, MM2_ONOFF, 2, 0, 1,     55),
    MM2ROW("GLIDE TIME",PK_MS,   nullptr, 0,   0, 5000,  56),
    MM2ROW("MASTER",  PK_CONT,   nullptr, 0,   0, 127,   57),
    MM2ROW("BEND RNG",PK_INT,    nullptr, 0,   1, 24,    58),
    MM2ROW("TREM RATE",PK_HZ,    nullptr, 0,   0, 20,    59),
    MM2ROW("TREM DEPTH",PK_CONT, nullptr, 0,   0, 127,   60),
    MM2ROW("TREM MIX",PK_CONT,   nullptr, 0,   0, 127,   61),
    MM2ROW("DLY TIME",PK_MS,     nullptr, 0,  20, 1200,  62),
    MM2ROW("DLY FDBK",PK_CONT,   nullptr, 0,   0, 127,   63),
    MM2ROW("DLY MIX", PK_CONT,   nullptr, 0,   0, 127,   65),
    MM2ROW("REV SIZE",PK_CONT,   nullptr, 0,   0, 127,   66),
    MM2ROW("REV MIX", PK_CONT,   nullptr, 0,   0, 127,   67),
    MM2ROW("ARP ON",  PK_TOGGLE, MM2_ONOFF, 2, 0, 1,     68),
    MM2ROW("ARP PATT",PK_INT,    nullptr, 0,   0, 7,     69),
    MM2ROW("ARP RATE",PK_CONT,   nullptr, 0,   0, 127,   70),
    MM2ROW("ARP GATE",PK_CONT,   nullptr, 0,   0, 127,   71),
    MM2ROW("ARP OCT", PK_INT,    nullptr, 0,   1, 4,     72),
    MM2ROW("ARP DIR", PK_ENUM,   MM2_ARP_DIR_NAMES, 6, 0, 5, 73),
    MM2ROW("ARP LATCH",PK_TOGGLE,MM2_ONOFF, 2, 0, 1,     74),
    MM2ROW("ARP SYNC",PK_TOGGLE, MM2_ONOFF, 2, 0, 1,     75),
};
static_assert(sizeof(MM2_PARAM_INFO) / sizeof(MM2_PARAM_INFO[0]) == MM2_NUM_PARAMS,
              "MM2_PARAM_INFO must have exactly MM2_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float mm2Raw[MM2_NUM_PARAMS];

struct Mm2Decoded {
    uint8_t o1Wave; float o1OctMul, o1Level;
    uint8_t o2Wave; float o2OctMul, o2FineRatio, o2Level; bool o2Sync;
    uint8_t o3Wave; float o3OctMul, o3FineRatio, o3Level, o3ToO1Fm;
    bool ringOn; float ringMix;
    float noiseLvl;
    float shRateHz, shToPitch, shToFilter;
    float cutoffHz; uint8_t filterType; float resonance, envAmt, kbdAmt;
    float fA, fD, fS, fR, fToPitch, aA, aD, aS, aR;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static Mm2Decoded mm2;

static inline float mm2OctMul(float v) {
    static const float TAB[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int i = (int)(v * 4.999f); if (i < 0) i = 0; else if (i > 4) i = 4;
    return TAB[i];
}

static void mm2Decode() {
    mm2.o1Wave = (uint8_t)(mm2Raw[MM2_OSC1_WAVE] * 3.999f);
    mm2.o1OctMul = mm2OctMul(mm2Raw[MM2_OSC1_OCT]);
    mm2.o1Level = mm2Raw[MM2_OSC1_LEVEL];

    mm2.o2Wave = (uint8_t)(mm2Raw[MM2_OSC2_WAVE] * 3.999f);
    mm2.o2OctMul = mm2OctMul(mm2Raw[MM2_OSC2_OCT]);
    mm2.o2FineRatio = fastPow2(((mm2Raw[MM2_OSC2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    mm2.o2Level = mm2Raw[MM2_OSC2_LEVEL];
    mm2.o2Sync = mm2Raw[MM2_OSC2_SYNC] > 0.5f;

    mm2.o3Wave = (uint8_t)(mm2Raw[MM2_OSC3_WAVE] * 3.999f);
    mm2.o3OctMul = mm2OctMul(mm2Raw[MM2_OSC3_OCT]);
    mm2.o3FineRatio = fastPow2(((mm2Raw[MM2_OSC3_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    mm2.o3Level = mm2Raw[MM2_OSC3_LEVEL];
    mm2.o3ToO1Fm = mm2Raw[MM2_OSC3_TO_OSC1_FM];

    mm2.ringOn = mm2Raw[MM2_RING_ON] > 0.5f;
    mm2.ringMix = mm2Raw[MM2_RING_MIX];

    mm2.noiseLvl = mm2Raw[MM2_NOISE];

    mm2.shRateHz = 0.2f + mm2Raw[MM2_SH_RATE] * 39.8f;
    mm2.shToPitch = mm2Raw[MM2_SH_TO_PITCH];
    mm2.shToFilter = mm2Raw[MM2_SH_TO_FILTER];

    mm2.cutoffHz = 20.0f * fastPow2(mm2Raw[MM2_FILTER_CUTOFF] * 9.8f);
    mm2.filterType = (uint8_t)(mm2Raw[MM2_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    mm2.resonance = mm2Raw[MM2_FILTER_RES];
    mm2.envAmt = mm2Raw[MM2_FILTER_ENV_AMT];
    mm2.kbdAmt = mm2Raw[MM2_FILTER_KBD];

    mm2.fA = 1.0f / (1.0f + mm2Raw[MM2_FENV_A] * 800.0f);
    mm2.fD = 1.0f / (1.0f + mm2Raw[MM2_FENV_D] * 1600.0f);
    mm2.fS = mm2Raw[MM2_FENV_S];
    mm2.fR = 1.0f / (1.0f + mm2Raw[MM2_FENV_R] * 1600.0f);
    mm2.fToPitch = mm2Raw[MM2_FENV_TO_PITCH];
    mm2.aA = 1.0f / (1.0f + mm2Raw[MM2_AENV_A] * 800.0f);
    mm2.aD = 1.0f / (1.0f + mm2Raw[MM2_AENV_D] * 1600.0f);
    mm2.aS = mm2Raw[MM2_AENV_S];
    mm2.aR = 1.0f / (1.0f + mm2Raw[MM2_AENV_R] * 1600.0f);

    mm2.glideOn = mm2Raw[MM2_GLIDE_ON] > 0.5f;
    mm2.glideTimeS = 0.005f + mm2Raw[MM2_GLIDE_TIME] * 5.0f;
    mm2.master = mm2Raw[MM2_MASTER];
    mm2.bendRangeSemis = 1.0f + mm2Raw[MM2_BEND_RANGE] * 23.0f;

    mm2.tremRate = mm2Raw[MM2_TREM_RATE] * 20.0f;
    mm2.tremDepth = mm2Raw[MM2_TREM_DEPTH];
    mm2.tremMix = mm2Raw[MM2_TREM_MIX];
    mm2.dlyTimeMs = 20.0f + mm2Raw[MM2_DLY_TIME] * 1180.0f;
    mm2.dlyFb = mm2Raw[MM2_DLY_FB];
    mm2.dlyMix = mm2Raw[MM2_DLY_MIX];
    mm2.revSize = mm2Raw[MM2_REV_SIZE];
    mm2.revMix = mm2Raw[MM2_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) for why every PK_ENUM/PK_TOGGLE delta below
// that isn't option 0 goes through it.
static const uint8_t MM2_INIT[] = {
    EI(1,4),EI(2,5),110,           // osc1 saw/8'/level
    EI(1,4),EI(2,5),64,90,0,       // osc2 saw/8'/fine(in tune)/level/sync off
    EI(0,4),EI(1,5),64,0,0,        // osc3 tri/16'/fine(in tune)/level 0/fm 0
    0,0,                           // ring on/mix
    0,                             // noise
    30,0,0,                        // S&H rate/to-pitch/to-filter
    90,0,20,60,0,                  // filter cutoff/type/res/envamt/kbd
    20,60,90,40,0,                 // filter env ADSR + to-pitch
    5,70,110,50,                   // amp env ADSR
    0,64,110,20,                   // glide/glidetime/master/bend
    0,0,0, 0,0,0,0,0,              // output
    0,0,64,80,0,0,0,0,             // arp
};
static_assert(sizeof(MM2_INIT) / sizeof(MM2_INIT[0]) == MM2_NUM_PARAMS,
              "MM2_INIT must have exactly MM2_NUM_PARAMS values");

#define MM2_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta MM2_P_BASS[]     = { {MM2_OSC1_OCT,0},{MM2_OSC2_WAVE,EI(2,4)},{MM2_OSC2_OCT,0},{MM2_OSC2_LEVEL,100},{MM2_FILTER_CUTOFF,25},{MM2_FILTER_RES,55},{MM2_FILTER_ENV_AMT,90},{MM2_FENV_D,30} };
static const PresetDelta MM2_P_SUBBASS[]  = { {MM2_OSC1_OCT,0},{MM2_OSC2_LEVEL,0},{MM2_OSC3_LEVEL,90},{MM2_OSC3_OCT,0},{MM2_FILTER_CUTOFF,18},{MM2_FILTER_RES,20},{MM2_AENV_D,40},{MM2_AENV_S,90} };
static const PresetDelta MM2_P_RINGBASS[] = { {MM2_OSC1_OCT,0},{MM2_OSC2_OCT,0},{MM2_OSC2_LEVEL,90},{MM2_RING_ON,EI(1,2)},{MM2_RING_MIX,35},{MM2_FILTER_CUTOFF,28},{MM2_FILTER_RES,50},{MM2_FENV_D,28} };
static const PresetDelta MM2_P_FMBASS[]   = { {MM2_OSC1_OCT,0},{MM2_OSC2_LEVEL,0},{MM2_OSC3_LEVEL,0},{MM2_OSC3_TO_OSC1_FM,35},{MM2_FILTER_CUTOFF,32},{MM2_FILTER_RES,45},{MM2_FENV_D,25} };
static const PresetDelta MM2_P_RESOBASS[] = { {MM2_OSC1_OCT,0},{MM2_OSC2_LEVEL,0},{MM2_FILTER_CUTOFF,26},{MM2_FILTER_RES,120},{MM2_FILTER_ENV_AMT,80},{MM2_FENV_D,28} };

// ── Leads ──
static const PresetDelta MM2_P_LEAD[]     = { {MM2_OSC2_FINE,76},{MM2_FILTER_CUTOFF,85},{MM2_FILTER_RES,55} };
static const PresetDelta MM2_P_SYNCLEAD[] = { {MM2_OSC2_OCT,EI(3,5)},{MM2_OSC2_SYNC,EI(1,2)},{MM2_FILTER_CUTOFF,80},{MM2_FILTER_RES,60} };
static const PresetDelta MM2_P_FMLEAD[]   = { {MM2_OSC2_LEVEL,0},{MM2_OSC3_LEVEL,0},{MM2_OSC3_TO_OSC1_FM,30},{MM2_FILTER_CUTOFF,80},{MM2_FILTER_RES,50} };
static const PresetDelta MM2_P_RINGLEAD[] = { {MM2_OSC2_FINE,80},{MM2_RING_ON,EI(1,2)},{MM2_RING_MIX,30},{MM2_FILTER_CUTOFF,78},{MM2_FILTER_RES,55} };
static const PresetDelta MM2_P_GLIDELEAD[]= { {MM2_OSC2_FINE,74},{MM2_FILTER_CUTOFF,80},{MM2_FILTER_RES,55},{MM2_GLIDE_ON,EI(1,2)},{MM2_GLIDE_TIME,18} };
static const PresetDelta MM2_P_SCREAMLEAD[]={ {MM2_OSC2_LEVEL,0},{MM2_FILTER_CUTOFF,90},{MM2_FILTER_RES,120},{MM2_FILTER_ENV_AMT,60},{MM2_FENV_D,40} };

// ── Patch-cord FX (the modular-specific category) ──
static const PresetDelta MM2_P_ENVTOPITCH[]= { {MM2_OSC2_LEVEL,0},{MM2_FILTER_ENV_AMT,20},{MM2_FENV_TO_PITCH,70},{MM2_FENV_A,0},{MM2_FENV_D,40},{MM2_FILTER_CUTOFF,70} };
static const PresetDelta MM2_P_SHVOICE[]  = { {MM2_OSC1_LEVEL,90},{MM2_SH_RATE,50},{MM2_SH_TO_PITCH,60},{MM2_SH_TO_FILTER,30},{MM2_FILTER_CUTOFF,65},{MM2_FILTER_RES,50} };
static const PresetDelta MM2_P_RINGFX[]   = { {MM2_OSC1_LEVEL,90},{MM2_OSC2_LEVEL,90},{MM2_OSC2_FINE,95},{MM2_RING_ON,EI(1,2)},{MM2_RING_MIX,100},{MM2_FILTER_CUTOFF,70} };
static const PresetDelta MM2_P_FMTEXTURE[]= { {MM2_OSC2_LEVEL,0},{MM2_OSC3_LEVEL,50},{MM2_OSC3_WAVE,EI(3,4)},{MM2_OSC3_TO_OSC1_FM,70},{MM2_FILTER_CUTOFF,75},{MM2_FILTER_RES,40} };
static const PresetDelta MM2_P_NOISESH[]  = { {MM2_OSC1_LEVEL,0},{MM2_NOISE,80},{MM2_SH_RATE,60},{MM2_SH_TO_FILTER,70},{MM2_FILTER_CUTOFF,55},{MM2_FILTER_RES,70},{MM2_AENV_S,110} };

// ── Pads ──
static const PresetDelta MM2_P_WARMPAD[]  = { {MM2_OSC2_FINE,70},{MM2_FILTER_CUTOFF,45},{MM2_FILTER_RES,20},{MM2_FENV_A,80},{MM2_AENV_A,75},{MM2_AENV_D,50},{MM2_AENV_S,100} };
static const PresetDelta MM2_P_MODULARPAD[]={ {MM2_OSC2_FINE,72},{MM2_OSC3_LEVEL,40},{MM2_OSC3_OCT,EI(2,5)},{MM2_FILTER_CUTOFF,55},{MM2_AENV_A,90},{MM2_AENV_S,100},{MM2_REV_MIX,40} };
static const PresetDelta MM2_P_SPACEPAD[] = { {MM2_OSC2_FINE,75},{MM2_OSC3_LEVEL,35},{MM2_OSC3_OCT,EI(3,5)},{MM2_FILTER_CUTOFF,50},{MM2_AENV_A,120},{MM2_AENV_S,110},{MM2_DLY_MIX,30},{MM2_DLY_TIME,60},{MM2_REV_MIX,70},{MM2_REV_SIZE,127} };
static const PresetDelta MM2_P_SWEEPPAD[] = { {MM2_OSC2_FINE,73},{MM2_FILTER_CUTOFF,20},{MM2_FILTER_RES,55},{MM2_FILTER_ENV_AMT,110},{MM2_FENV_A,100},{MM2_FENV_D,110},{MM2_AENV_A,90},{MM2_AENV_S,100} };

// ── Brass ──
static const PresetDelta MM2_P_BRASS[]    = { {MM2_OSC1_WAVE,EI(2,4)},{MM2_OSC2_WAVE,EI(2,4)},{MM2_OSC2_FINE,72},{MM2_FILTER_ENV_AMT,90},{MM2_FENV_A,15},{MM2_FENV_D,60},{MM2_AENV_A,10} };
static const PresetDelta MM2_P_BRASSSTAB[]= { {MM2_OSC1_WAVE,EI(2,4)},{MM2_OSC2_WAVE,EI(2,4)},{MM2_OSC2_FINE,72},{MM2_FILTER_ENV_AMT,90},{MM2_FENV_A,0},{MM2_FENV_D,20},{MM2_AENV_D,25} };

// ── Bells/plucks/SFX ──
static const PresetDelta MM2_P_BELLPLUCK[]= { {MM2_OSC1_WAVE,EI(3,4)},{MM2_OSC2_WAVE,EI(3,4)},{MM2_OSC2_FINE,95},{MM2_RING_ON,EI(1,2)},{MM2_RING_MIX,45},{MM2_FILTER_CUTOFF,90},{MM2_FENV_D,45},{MM2_AENV_D,50},{MM2_AENV_S,0} };
static const PresetDelta MM2_P_PING[]     = { {MM2_OSC1_WAVE,EI(3,4)},{MM2_OSC2_LEVEL,0},{MM2_FILTER_CUTOFF,95},{MM2_FILTER_RES,80},{MM2_FENV_D,12},{MM2_AENV_D,14},{MM2_AENV_S,0} };
static const PresetDelta MM2_P_MARIMBA[]  = { {MM2_OSC1_WAVE,EI(2,4)},{MM2_OSC1_OCT,EI(3,5)},{MM2_OSC2_LEVEL,0},{MM2_FILTER_CUTOFF,55},{MM2_FILTER_ENV_AMT,50},{MM2_FENV_D,18},{MM2_AENV_D,16},{MM2_AENV_S,0} };
static const PresetDelta MM2_P_NOISESWEEP[]={ {MM2_OSC1_LEVEL,0},{MM2_NOISE,110},{MM2_FILTER_CUTOFF,25},{MM2_FILTER_RES,100},{MM2_FILTER_ENV_AMT,120},{MM2_FENV_A,90},{MM2_FENV_D,100},{MM2_AENV_A,10},{MM2_AENV_S,110} };
static const PresetDelta MM2_P_WINDFX[]   = { {MM2_OSC1_LEVEL,0},{MM2_NOISE,90},{MM2_FILTER_CUTOFF,35},{MM2_FILTER_RES,20},{MM2_AENV_A,100},{MM2_AENV_S,110},{MM2_REV_MIX,40} };
static const PresetDelta MM2_P_DRONE[]    = { {MM2_OSC2_FINE,66},{MM2_OSC3_LEVEL,50},{MM2_OSC3_OCT,0},{MM2_NOISE,15},{MM2_FILTER_CUTOFF,40},{MM2_FILTER_RES,100},{MM2_FENV_A,127},{MM2_AENV_A,127},{MM2_AENV_S,127},{MM2_REV_MIX,60},{MM2_REV_SIZE,127} };

// ── Arp ──
static const PresetDelta MM2_P_ARPCLASSIC[]={ {MM2_OSC1_WAVE,EI(2,4)},{MM2_FILTER_CUTOFF,65},{MM2_FILTER_RES,50},{MM2_FILTER_ENV_AMT,50},{MM2_FENV_D,20},{MM2_AENV_D,18},{MM2_AENV_S,30},{MM2_ARP_ENABLE,EI(1,2)},{MM2_ARP_RATE,80},{MM2_ARP_GATE,70} };
static const PresetDelta MM2_P_ARPOCTAVES[]={ {MM2_OSC1_WAVE,EI(1,4)},{MM2_FILTER_CUTOFF,70},{MM2_FILTER_RES,40},{MM2_ARP_ENABLE,EI(1,2)},{MM2_ARP_RATE,90},{MM2_ARP_GATE,80},{MM2_ARP_OCTAVES,80},{MM2_ARP_DIR,EI(2,6)} };
static const PresetDelta MM2_P_ARPSEQ[]   = { {MM2_OSC1_WAVE,EI(2,4)},{MM2_OSC2_LEVEL,0},{MM2_FILTER_CUTOFF,60},{MM2_FILTER_ENV_AMT,90},{MM2_FENV_D,20},{MM2_AENV_D,25},{MM2_AENV_S,10},{MM2_ARP_ENABLE,EI(1,2)},{MM2_ARP_RATE,100},{MM2_ARP_GATE,60}};

static const Preset MM2_PRESETS[] = {
    MM2_PRESET("MODULAR BASS", MM2_P_BASS),
    MM2_PRESET("SUB BASS",     MM2_P_SUBBASS),
    MM2_PRESET("RING BASS",    MM2_P_RINGBASS),
    MM2_PRESET("FM BASS",      MM2_P_FMBASS),
    MM2_PRESET("RESO BASS",    MM2_P_RESOBASS),
    MM2_PRESET("MODULAR LEAD", MM2_P_LEAD),
    MM2_PRESET("SYNC LEAD",    MM2_P_SYNCLEAD),
    MM2_PRESET("FM LEAD",      MM2_P_FMLEAD),
    MM2_PRESET("RING LEAD",    MM2_P_RINGLEAD),
    MM2_PRESET("GLIDE LEAD",   MM2_P_GLIDELEAD),
    MM2_PRESET("SCREAM LEAD",  MM2_P_SCREAMLEAD),
    MM2_PRESET("ENV>PITCH FX", MM2_P_ENVTOPITCH),
    MM2_PRESET("S&H VOICE",    MM2_P_SHVOICE),
    MM2_PRESET("RING FX",      MM2_P_RINGFX),
    MM2_PRESET("FM TEXTURE",   MM2_P_FMTEXTURE),
    MM2_PRESET("NOISE S&H",    MM2_P_NOISESH),
    MM2_PRESET("WARM PAD",     MM2_P_WARMPAD),
    MM2_PRESET("MODULAR PAD",  MM2_P_MODULARPAD),
    MM2_PRESET("SPACE PAD",    MM2_P_SPACEPAD),
    MM2_PRESET("SWEEP PAD",    MM2_P_SWEEPPAD),
    MM2_PRESET("MODULAR BRASS",MM2_P_BRASS),
    MM2_PRESET("BRASS STAB",   MM2_P_BRASSSTAB),
    MM2_PRESET("BELL PLUCK",   MM2_P_BELLPLUCK),
    MM2_PRESET("PING",         MM2_P_PING),
    MM2_PRESET("MARIMBA",      MM2_P_MARIMBA),
    MM2_PRESET("NOISE SWEEP",  MM2_P_NOISESWEEP),
    MM2_PRESET("WIND FX",      MM2_P_WINDFX),
    MM2_PRESET("DRONE",        MM2_P_DRONE),
    MM2_PRESET("ARP CLASSIC",  MM2_P_ARPCLASSIC),
    MM2_PRESET("ARP OCTAVES",  MM2_P_ARPOCTAVES),
    MM2_PRESET("ARP SEQUENCE", MM2_P_ARPSEQ),
};
#define MM2_NUM_PRESETS (sizeof(MM2_PRESETS) / sizeof(MM2_PRESETS[0]))

static void mm2ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < MM2_NUM_PARAMS; i++) mm2Raw[i] = (float)MM2_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) mm2Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    mm2Decode();
}

static const EnginePageDef MM2_PAGES[] = {
    { "OSC 1/2/3",  MM2_OSC1_WAVE,      13 },
    { "RING/S&H",   MM2_RING_ON,         5 },
    { "FILTER",     MM2_FILTER_CUTOFF,   5 },
    { "ENVELOPES",  MM2_FENV_A,          9 },
    { "VOICING",    MM2_GLIDE_ON,        4 },
    { "OUTPUT",     MM2_TREM_RATE,       8 },
    { "ARP",        MM2_ARP_ENABLE,      8 },
};
#define MM2_NUM_PAGES (sizeof(MM2_PAGES) / sizeof(MM2_PAGES[0]))

uint8_t mm2MasterParamIdE() { return MM2_MASTER; }
uint8_t mm2FilterCutoffParamIdE()    { return MM2_FILTER_CUTOFF; }
uint8_t mm2FilterResonanceParamIdE() { return MM2_FILTER_RES; }
uint8_t mm2NumPagesE() { return (uint8_t)MM2_NUM_PAGES; }
const EnginePageDef* mm2PageE(uint8_t idx) { return (idx < MM2_NUM_PAGES) ? &MM2_PAGES[idx] : &MM2_PAGES[0]; }

// ─── Voice ──────────────────────────────────────────────────────────────
static Mm2Voice mm2v;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void mm2EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void mm2EngineActivate() {
    mm2v = Mm2Voice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void mm2ParamsInitE() { mm2ApplyDeltas(nullptr, 0); }
void mm2SetParameterE(uint8_t id, float v) { if (id < MM2_NUM_PARAMS) { mm2Raw[id] = v; mm2Decode(); } }
float mm2GetParameterE(uint8_t id) { return (id < MM2_NUM_PARAMS) ? mm2Raw[id] : 0.0f; }
void mm2FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= MM2_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = MM2_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(mm2Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(mm2Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool mm2ParamIsLockableE(uint8_t id) { return id < MM2_NUM_PARAMS; }
uint8_t mm2NumParamsE() { return MM2_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo MM2_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* mm2ParamInfoE(uint8_t id) {
    return (id < MM2_NUM_PARAMS) ? &MM2_PARAM_INFO[id] : &MM2_INVALID_PARAM;
}
uint8_t mm2NumPresetsE() { return (uint8_t)MM2_NUM_PRESETS; }
const char* mm2PresetNameE(uint8_t idx) { return MM2_PRESETS[idx].name; }
void mm2LoadPresetE(uint8_t idx) { mm2ApplyDeltas(MM2_PRESETS[idx].deltas, MM2_PRESETS[idx].n); }
void mm2LoadInitPatchE() { mm2ApplyDeltas(nullptr, 0); }
void mm2SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < MM2_NUM_PARAMS; i++) out[i] = (uint8_t)(mm2Raw[i] * 255.0f + 0.5f);
}
void mm2RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < MM2_NUM_PARAMS; i++) mm2Raw[i] = (float)in[i] * (1.0f / 255.0f);
    mm2Decode();
}

uint8_t mm2NumVoicesE() { return MM2_VOICES; }
void    mm2VoiceInitE() { mm2EngineActivate(); }

void mm2NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    (void)voiceIdx;
    float freq = noteToFreq(note);
    mm2v.note = note; mm2v.velocity = vel; mm2v.velNorm = (float)vel * (1.0f / 127.0f);
    mm2v.gate = true;
    mm2v.targetFreq = freq;
    if (mm2.glideOn && mm2v.currentFreq > 20.0f) {
        mm2v.gliding = true;
        mm2v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (mm2.glideTimeS * SAMPLE_RATE_F);
        if (mm2v.glideCoef > 1.0f) mm2v.glideCoef = 1.0f;
    } else {
        mm2v.currentFreq = freq;
        mm2v.gliding = false;
    }
    if (trigEnv) {
        mm2v.filtEnv.trigger(mm2.fA, mm2.fD, mm2.fS, mm2.fR);
        mm2v.ampEnv.trigger(mm2.aA, mm2.aD, mm2.aS, mm2.aR);
    }
}
void mm2NoteOffE(uint8_t voiceIdx) {
    (void)voiceIdx;
    mm2v.gate = false;
    mm2v.filtEnv.release();
    mm2v.ampEnv.release();
}
void mm2AllOffE() { mm2NoteOffE(0); }
void mm2SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    (void)voiceIdx; mm2v.aftertouch = (float)pressure * (1.0f / 127.0f);
}

int mm2RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)modWheel; (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * mm2.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = mm2.tremMix > 0.004f;
    const bool fxDelay  = mm2.dlyMix  > 0.004f;
    const bool fxReverb = mm2.revMix  > 0.004f;
    g_fxChorus.setRate(mm2.tremRate);
    g_fxReverb.setRoom(mm2.revSize, 0.4f);
    g_fxDelay.setTimeMs(mm2.dlyTimeMs, false);

    int active = 0;
    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        const bool alive = mm2v.ampEnv.stage != MM2_E_IDLE;

        float out = 0.0f;
        if (alive) {
            uint32_t prevSh = mm2v.shPhase;
            mm2v.shPhase += (uint32_t)(mm2.shRateHz * PHASE_PER_HZ);
            if (mm2v.shPhase < prevSh) mm2v.shValue = mm2v.noise.white();

            const float envF = mm2v.filtEnv.process();

            if (ctrl) {
                if (mm2v.gliding) {
                    float d = mm2v.targetFreq - mm2v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { mm2v.currentFreq = mm2v.targetFreq; mm2v.gliding = false; }
                    else mm2v.currentFreq += d * mm2v.glideCoef;
                }
                float shPitchSemis = mm2.shToPitch * mm2v.shValue * 24.0f;
                // The filter envelope's second destination: patched to
                // pitch as well as the filter (see file banner point 3).
                float envPitchSemis = mm2.fToPitch * envF * 24.0f;
                float base = mm2v.currentFreq * bendMul
                    * fastPow2((shPitchSemis + envPitchSemis) / 12.0f);

                mm2v.osc2.setFreq(base * mm2.o2OctMul * mm2.o2FineRatio);
                mm2v.osc3.setFreq(base * mm2.o3OctMul * mm2.o3FineRatio);
                // OSC1's own base frequency before any audio-rate FM from
                // OSC3 -- setFmHz() below retunes it per-sample without
                // touching this control-rate PolyBLEP sizing.
                mm2v.osc1.setFreq(base * mm2.o1OctMul);

                float kbdSemis = mm2.kbdAmt * (float)(mm2v.note - 60);
                float cutoff = safeClamp(
                    mm2.cutoffHz * fastPow2((kbdSemis + mm2.envAmt * envF * 48.0f) / 12.0f),
                    20.0f, 18000.0f);
                if (mm2.filterType == VF_STOCK) mm2v.filter.setCutoffHz(cutoff, mm2.resonance);
                else                             mm2v.varFilt.setCutoff(cutoff, mm2.resonance, mm2.filterType);
            }

            bool wrapped2 = false;
            float o2raw = mm2v.osc2.next(mm2.o2Wave, &wrapped2);
            float o3raw = mm2v.osc3.next(mm2.o3Wave);

            // Audio-rate FM: retune OSC1's phase increment every sample
            // from OSC3's own last sample, on top of whatever base
            // frequency setFreq() last gave it at control rate.
            if (mm2.o3ToO1Fm > 0.001f) {
                float fmHz = mm2v.osc1.baseHz * fastPow2(o3raw * mm2.o3ToO1Fm * (2.0f / 12.0f));
                mm2v.osc1.setFmHz(fmHz);
            }
            if (mm2.o2Sync && wrapped2) mm2v.osc1.resetPhase();
            float o1raw = mm2v.osc1.next(mm2.o1Wave);

            float o1out = o1raw * mm2.o1Level;
            float o2out = o2raw * mm2.o2Level;
            float o3out = o3raw * mm2.o3Level;
            if (mm2.ringOn) {
                float ring = o1raw * o2raw;
                o1out += (ring * mm2.o1Level - o1out) * mm2.ringMix;
            }
            float noiseOut = mm2v.noise.white() * mm2.noiseLvl;

            float voiceIn = (o1out + o2out + o3out + noiseOut) * 0.4f;
            float filtered = (mm2.filterType == VF_STOCK)
                ? mm2v.filter.process(voiceIn)
                : mm2v.varFilt.process(voiceIn, mm2.filterType, mm2.resonance);

            float amp = mm2v.ampEnv.process();
            active = 1;

            // MASTER used to be multiplied straight into the saturator
            // below, so turning the volume up also drove more distortion.
            // MM2_MIX_DRIVE is a fixed pre-saturator drive instead,
            // matching every other engine's own fixed-drive fix for the
            // same bug. MASTER itself is now applied as pure linear gain
            // after the saturator and the whole effect chain.
            const float MM2_MIX_DRIVE = 2.4f;
            out = fastTanh(filtered * MM2_MIX_DRIVE) * amp;
        }
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, mm2.tremDepth, mm2.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, mm2.dlyFb, mm2.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * mm2.revMix; r += rr * mm2.revMix;
        }
        l *= mm2.master; r *= mm2.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    va.busy[0] = active ? 1 : 0;
    return active;
}

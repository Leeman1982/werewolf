#include "prophet5.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  prophet5.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum P5ParamId : uint8_t {
    P5_OSCA_SAW = 0, P5_OSCA_PULSE, P5_OSCA_PW, P5_OSCA_SYNC,
    P5_OSCB_SAW, P5_OSCB_PULSE, P5_OSCB_TRI, P5_OSCB_PW, P5_OSCB_FINE, P5_OSCB_LOFREQ,
    P5_NOISE,
    P5_PM_ENV_AMT, P5_PM_ENV_TO_FREQA, P5_PM_ENV_TO_FILTER,
    P5_PM_OSCB_AMT, P5_PM_OSCB_TO_FREQA, P5_PM_OSCB_TO_FILTER,
    P5_LFO_RATE, P5_LFO_SHAPE, P5_LFO_TO_OSC, P5_LFO_TO_FILTER,
    P5_FILTER_CUTOFF, P5_FILTER_TYPE, P5_FILTER_RES, P5_FILTER_KBD, P5_FILTER_ENV_AMT,
    P5_FENV_A, P5_FENV_D, P5_FENV_S, P5_FENV_R,
    P5_AENV_A, P5_AENV_D, P5_AENV_S, P5_AENV_R,
    P5_UNISON, P5_GLIDE_ON, P5_GLIDE_TIME, P5_MASTER, P5_BEND_RANGE,

    P5_TREM_RATE, P5_TREM_DEPTH, P5_TREM_MIX,
    P5_DLY_TIME, P5_DLY_FB, P5_DLY_MIX, P5_REV_SIZE, P5_REV_MIX,

    P5_ARP_ENABLE, P5_ARP_PATTERN, P5_ARP_RATE, P5_ARP_GATE,
    P5_ARP_OCTAVES, P5_ARP_DIR, P5_ARP_LATCH, P5_ARP_SYNC,

    P5_NUM_PARAMS
};

static const char* const P5_ONOFF[2]       = { "OFF", "ON" };
static const char* const P5_KBD_NAMES[3]   = { "OFF", "HALF", "FULL" };
static const char* const P5_LFO_SHAPE_NAMES[3] = { "TRIANGLE", "SQUARE", "SAWTOOTH" };
static const char* const P5_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define P5ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [P5_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo P5_PARAM_INFO[] = {
    P5ROW("A SAW",    PK_CONT,   nullptr, 0,   0, 127,  20),
    P5ROW("A PULSE",  PK_CONT,   nullptr, 0,   0, 127,  21),
    P5ROW("A PW",     PK_CONT,   nullptr, 0,   0, 127,  22),
    P5ROW("A SYNC",   PK_TOGGLE, P5_ONOFF, 2,  0, 1,    23),
    P5ROW("B SAW",    PK_CONT,   nullptr, 0,   0, 127,  24),
    P5ROW("B PULSE",  PK_CONT,   nullptr, 0,   0, 127,  25),
    // Real Prophet-5 panels give Oscillator B a third waveform, Triangle,
    // that Oscillator A never had — see the file banner. CC 74 rather than
    // the next number in sequence (26) since this row was added after the
    // rest of the table was laid out and 26 onward are all already taken.
    P5ROW("B TRI",    PK_CONT,   nullptr, 0,   0, 127,  74),
    P5ROW("B PW",     PK_CONT,   nullptr, 0,   0, 127,  26),
    P5ROW("B FINE",   PK_CENTS,  nullptr, 0,  -700, 700, 27),
    P5ROW("B LO FREQ",PK_TOGGLE, P5_ONOFF, 2,  0, 1,    28),
    P5ROW("NOISE",    PK_CONT,   nullptr, 0,   0, 127,  29),
    P5ROW("PM ENV AMT",PK_CONT,  nullptr, 0,   0, 127,  30),
    P5ROW("PM ENV>A", PK_TOGGLE, P5_ONOFF, 2,  0, 1,    31),
    P5ROW("PM ENV>VCF",PK_TOGGLE,P5_ONOFF, 2,  0, 1,    32),
    P5ROW("PM OSCB AMT",PK_CONT, nullptr, 0,   0, 127,  33),
    P5ROW("PM OSCB>A",PK_TOGGLE, P5_ONOFF, 2,  0, 1,    34),
    P5ROW("PM OSCB>VCF",PK_TOGGLE,P5_ONOFF,2,  0, 1,    35),
    P5ROW("LFO RATE", PK_HZ,     nullptr, 0,   0, 20,   36),
    P5ROW("LFO SHAPE",PK_ENUM,   P5_LFO_SHAPE_NAMES,3,0,2,37),
    P5ROW("LFO>OSC",  PK_CONT,   nullptr, 0,   0, 127,  38),
    P5ROW("LFO>VCF",  PK_CONT,   nullptr, 0,   0, 127,  39),
    P5ROW("CUTOFF",   PK_HZ,     nullptr, 0,  20, 18000,40),
    P5ROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 73),
    P5ROW("RESONANCE",PK_CONT,   nullptr, 0,   0, 127,  41),
    P5ROW("KBD AMT",  PK_ENUM,   P5_KBD_NAMES, 3,0, 2,  42),
    P5ROW("VCF ENV AMT",PK_CONT, nullptr, 0,   0, 127,  43),
    P5ROW("VCF ATK",  PK_MS,     nullptr, 0,   1, 8000, 44),
    P5ROW("VCF DEC",  PK_MS,     nullptr, 0,   3, 12000,45),
    P5ROW("VCF SUS",  PK_CONT,   nullptr, 0,   0, 127,  46),
    P5ROW("VCF REL",  PK_MS,     nullptr, 0,   3, 12000,47),
    P5ROW("VCA ATK",  PK_MS,     nullptr, 0,   1, 8000, 48),
    P5ROW("VCA DEC",  PK_MS,     nullptr, 0,   3, 12000,49),
    P5ROW("VCA SUS",  PK_CONT,   nullptr, 0,   0, 127,  50),
    P5ROW("VCA REL",  PK_MS,     nullptr, 0,   3, 12000,51),
    P5ROW("UNISON",   PK_TOGGLE, P5_ONOFF, 2,  0, 1,    52),
    P5ROW("GLIDE",    PK_TOGGLE, P5_ONOFF, 2,  0, 1,    53),
    P5ROW("GLIDE TIME",PK_MS,    nullptr, 0,   0, 5000, 54),
    P5ROW("MASTER",   PK_CONT,   nullptr, 0,   0, 127,  55),
    P5ROW("BEND RNG", PK_INT,    nullptr, 0,   1, 24,   56),
    P5ROW("TREM RATE",PK_HZ,     nullptr, 0,   0, 20,   57),
    P5ROW("TREM DEPTH",PK_CONT,  nullptr, 0,   0, 127,  58),
    P5ROW("TREM MIX", PK_CONT,   nullptr, 0,   0, 127,  59),
    P5ROW("DLY TIME", PK_MS,     nullptr, 0,  20, 1200, 60),
    P5ROW("DLY FDBK", PK_CONT,   nullptr, 0,   0, 127,  61),
    P5ROW("DLY MIX",  PK_CONT,   nullptr, 0,   0, 127,  62),
    P5ROW("REV SIZE", PK_CONT,   nullptr, 0,   0, 127,  63),
    P5ROW("REV MIX",  PK_CONT,   nullptr, 0,   0, 127,  64),
    P5ROW("ARP ON",   PK_TOGGLE, P5_ONOFF, 2,  0, 1,    65),
    P5ROW("ARP PATT", PK_INT,    nullptr, 0,   0, 7,    66),
    P5ROW("ARP RATE", PK_CONT,   nullptr, 0,   0, 127,  67),
    P5ROW("ARP GATE", PK_CONT,   nullptr, 0,   0, 127,  68),
    P5ROW("ARP OCT",  PK_INT,    nullptr, 0,   1, 4,    69),
    P5ROW("ARP DIR",  PK_ENUM,   P5_ARP_DIR_NAMES,6,0,5,70),
    P5ROW("ARP LATCH",PK_TOGGLE, P5_ONOFF, 2,  0, 1,    71),
    P5ROW("ARP SYNC", PK_TOGGLE, P5_ONOFF, 2,  0, 1,    72),
};
static_assert(sizeof(P5_PARAM_INFO) / sizeof(P5_PARAM_INFO[0]) == P5_NUM_PARAMS,
              "P5_PARAM_INFO must have exactly P5_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float p5Raw[P5_NUM_PARAMS];

struct P5Decoded {
    float sawA, pulseA, pwA; bool syncA;
    float sawB, pulseB, triB, pwB, fineB; bool loFreqB;
    float noiseLvl;
    float pmEnvAmt; bool pmEnvToFreqA, pmEnvToFilter;
    float pmOscBAmt; bool pmOscBToFreqA, pmOscBToFilter;
    float lfoRateHz; uint8_t lfoShape; float lfoToOsc, lfoToFilter;
    float cutoffHz; uint8_t filterType; float resonance, kbdAmt, envAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    bool unison, glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static P5Decoded p5;

static void p5Decode() {
    p5.sawA = p5Raw[P5_OSCA_SAW]; p5.pulseA = p5Raw[P5_OSCA_PULSE];
    p5.pwA  = 0.05f + p5Raw[P5_OSCA_PW] * 0.45f;
    p5.syncA = p5Raw[P5_OSCA_SYNC] > 0.5f;

    p5.sawB = p5Raw[P5_OSCB_SAW]; p5.pulseB = p5Raw[P5_OSCB_PULSE]; p5.triB = p5Raw[P5_OSCB_TRI];
    p5.pwB  = 0.05f + p5Raw[P5_OSCB_PW] * 0.45f;
    p5.fineB = fastPow2(((p5Raw[P5_OSCB_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    p5.loFreqB = p5Raw[P5_OSCB_LOFREQ] > 0.5f;

    p5.noiseLvl = p5Raw[P5_NOISE];

    p5.pmEnvAmt = p5Raw[P5_PM_ENV_AMT];
    p5.pmEnvToFreqA = p5Raw[P5_PM_ENV_TO_FREQA] > 0.5f;
    p5.pmEnvToFilter = p5Raw[P5_PM_ENV_TO_FILTER] > 0.5f;
    p5.pmOscBAmt = p5Raw[P5_PM_OSCB_AMT];
    p5.pmOscBToFreqA = p5Raw[P5_PM_OSCB_TO_FREQA] > 0.5f;
    p5.pmOscBToFilter = p5Raw[P5_PM_OSCB_TO_FILTER] > 0.5f;

    p5.lfoRateHz = 0.1f + p5Raw[P5_LFO_RATE] * 19.9f;
    p5.lfoShape = (uint8_t)(p5Raw[P5_LFO_SHAPE] * 2.999f);
    p5.lfoToOsc = p5Raw[P5_LFO_TO_OSC];
    p5.lfoToFilter = p5Raw[P5_LFO_TO_FILTER];

    p5.cutoffHz = 20.0f * fastPow2(p5Raw[P5_FILTER_CUTOFF] * 9.8f);
    p5.filterType = (uint8_t)(p5Raw[P5_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    p5.resonance = p5Raw[P5_FILTER_RES];
    { static const float KBD_TAB[3] = { 0.0f, 0.5f, 1.0f };
      int i = (int)(p5Raw[P5_FILTER_KBD] * 2.999f); if (i < 0) i = 0; else if (i > 2) i = 2;
      p5.kbdAmt = KBD_TAB[i]; }
    p5.envAmt = p5Raw[P5_FILTER_ENV_AMT];

    p5.fA = 1.0f / (1.0f + p5Raw[P5_FENV_A] * 800.0f);
    p5.fD = 1.0f / (1.0f + p5Raw[P5_FENV_D] * 1600.0f);
    p5.fS = p5Raw[P5_FENV_S];
    p5.fR = 1.0f / (1.0f + p5Raw[P5_FENV_R] * 1600.0f);
    p5.aA = 1.0f / (1.0f + p5Raw[P5_AENV_A] * 800.0f);
    p5.aD = 1.0f / (1.0f + p5Raw[P5_AENV_D] * 1600.0f);
    p5.aS = p5Raw[P5_AENV_S];
    p5.aR = 1.0f / (1.0f + p5Raw[P5_AENV_R] * 1600.0f);

    p5.unison = p5Raw[P5_UNISON] > 0.5f;
    p5.glideOn = p5Raw[P5_GLIDE_ON] > 0.5f;
    p5.glideTimeS = 0.005f + p5Raw[P5_GLIDE_TIME] * 5.0f;
    p5.master = p5Raw[P5_MASTER];
    p5.bendRangeSemis = 1.0f + p5Raw[P5_BEND_RANGE] * 23.0f;

    p5.tremRate = p5Raw[P5_TREM_RATE] * 20.0f;
    p5.tremDepth = p5Raw[P5_TREM_DEPTH];
    p5.tremMix = p5Raw[P5_TREM_MIX];
    p5.dlyTimeMs = 20.0f + p5Raw[P5_DLY_TIME] * 1180.0f;
    p5.dlyFb = p5Raw[P5_DLY_FB];
    p5.dlyMix = p5Raw[P5_DLY_MIX];
    p5.revSize = p5Raw[P5_REV_SIZE];
    p5.revMix = p5Raw[P5_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n): every PK_ENUM/PK_TOGGLE decode here (and
// the shared arp reader in CarpenterSynth.ino) reads a raw INIT/delta byte
// as a proportion of 0..127, not a literal option index -- EI() returns the
// raw byte that round-trips through that decode back to option `idx` out of
// `n`. Raw 0 already decodes to index 0 on its own, so option-0 fields below
// are left as plain 0. OSCB_FINE (a bipolar +/-700 cent knob, center = 0
// cents at raw ~64) is NOT an enum -- it was previously left at the same 128
// literal Minimoog's own O2_FINE had, which is *nearly the top of the whole
// +/-700-cent range* rather than "a few cents sharp", so every preset that
// didn't override it detuned oscillator B almost a fifth sharp of oscillator
// A. Fixed the same way: default to a few cents (~64-70), and only push it
// further for presets that actually want a wide/detuned character.
static const uint8_t P5_INIT[] = {
    110,0,64,0,          // OSC A saw/pulse/pw/sync
    0,90,0,64,68,0,        // OSC B saw/pulse/tri/pw/fine/lofreq
    0,                    // noise
    0,0,0, 0,0,0,         // poly-mod
    30,0,0,0,             // LFO
    100,0,20,EI(2,3),60,  // filter cutoff/type/res/kbd(FULL)/envamt
    20,60,90,40,          // filter env ADSR
    5,70,110,50,          // amp env ADSR
    0,0,64,               // unison/glide/glidetime
    110,20,               // master/bend
    0,0,0, 0,0,0,0,0,     // output
    0,0,64,80,0,0,0,0,    // arp
};
static_assert(sizeof(P5_INIT) / sizeof(P5_INIT[0]) == P5_NUM_PARAMS,
              "P5_INIT must have exactly P5_NUM_PARAMS values");

#define P5_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta P5_P_DEEPBASS[] = { {P5_OSCA_SAW,110},{P5_OSCB_PULSE,100},{P5_OSCB_FINE,60},{P5_OSCB_LOFREQ,EI(1,2)},{P5_FILTER_CUTOFF,25},{P5_FILTER_RES,30},{P5_FILTER_ENV_AMT,90},{P5_FENV_A,0},{P5_FENV_D,45},{P5_AENV_D,50},{P5_AENV_S,90} };
static const PresetDelta P5_P_POLYBASS[] = { {P5_OSCA_SAW,100},{P5_OSCB_PULSE,90},{P5_OSCB_FINE,70},{P5_FILTER_CUTOFF,35},{P5_FILTER_RES,40},{P5_FILTER_ENV_AMT,80},{P5_FENV_A,0},{P5_FENV_D,35},{P5_AENV_A,0},{P5_AENV_D,30},{P5_AENV_S,60} };
static const PresetDelta P5_P_GROWLBASS[] = { {P5_OSCA_PULSE,110},{P5_OSCA_PW,30},{P5_OSCB_PULSE,90},{P5_OSCB_FINE,80},{P5_PM_OSCB_AMT,40},{P5_PM_OSCB_TO_FREQA,EI(1,2)},{P5_FILTER_CUTOFF,30},{P5_FILTER_RES,80},{P5_FILTER_ENV_AMT,90},{P5_FENV_D,40} };
static const PresetDelta P5_P_SEQBASS[] = { {P5_OSCA_SAW,110},{P5_OSCB_PULSE,80},{P5_FILTER_CUTOFF,40},{P5_FILTER_RES,60},{P5_FILTER_ENV_AMT,90},{P5_FENV_A,0},{P5_FENV_D,15},{P5_FENV_S,0},{P5_AENV_A,0},{P5_AENV_D,20},{P5_AENV_S,20} };
static const PresetDelta P5_P_SUBBASS[] = { {P5_OSCA_SAW,90},{P5_OSCB_LOFREQ,EI(1,2)},{P5_OSCB_PULSE,100},{P5_FILTER_CUTOFF,18},{P5_FILTER_RES,20},{P5_FILTER_ENV_AMT,50},{P5_AENV_D,60},{P5_AENV_S,100} };

// ── Leads ──
static const PresetDelta P5_P_BRASS[] = { {P5_OSCA_SAW,110},{P5_OSCB_SAW,100},{P5_OSCB_FINE,90},{P5_PM_ENV_AMT,50},{P5_PM_ENV_TO_FREQA,EI(1,2)},{P5_FILTER_ENV_AMT,90},{P5_FENV_A,15},{P5_FENV_D,50},{P5_AENV_A,10} };
static const PresetDelta P5_P_UNISON_LEAD[] = { {P5_UNISON,EI(1,2)},{P5_OSCA_SAW,127},{P5_OSCB_SAW,110},{P5_OSCB_FINE,95},{P5_GLIDE_ON,EI(1,2)},{P5_GLIDE_TIME,15},{P5_FILTER_RES,90} };
static const PresetDelta P5_P_SCREAMLEAD[] = { {P5_OSCA_SAW,127},{P5_OSCB_SAW,110},{P5_OSCB_FINE,90},{P5_FILTER_CUTOFF,100},{P5_FILTER_RES,110},{P5_FILTER_ENV_AMT,100},{P5_FENV_A,0},{P5_FENV_D,50} };
static const PresetDelta P5_P_SOFTLEAD[] = { {P5_OSCA_SAW,90},{P5_OSCB_TRI,70},{P5_OSCB_FINE,70},{P5_FILTER_CUTOFF,60},{P5_FILTER_RES,30},{P5_FENV_A,15},{P5_AENV_A,10} };
static const PresetDelta P5_P_PWMLEAD[] = { {P5_OSCA_PULSE,110},{P5_OSCA_PW,50},{P5_LFO_TO_OSC,40},{P5_LFO_RATE,25},{P5_FILTER_CUTOFF,70},{P5_FILTER_RES,40} };
static const PresetDelta P5_P_RESOLEAD[] = { {P5_OSCA_SAW,110},{P5_OSCB_SAW,90},{P5_OSCB_FINE,74},{P5_FILTER_CUTOFF,85},{P5_FILTER_RES,100},{P5_FILTER_ENV_AMT,60},{P5_FENV_A,0},{P5_FENV_D,40},{P5_GLIDE_ON,EI(1,2)},{P5_GLIDE_TIME,15} };

// ── Brass ──
static const PresetDelta P5_P_BRASSSTAB[] = { {P5_OSCA_SAW,110},{P5_OSCB_SAW,100},{P5_OSCB_FINE,72},{P5_FILTER_CUTOFF,55},{P5_FILTER_RES,20},{P5_FILTER_ENV_AMT,90},{P5_FENV_A,0},{P5_FENV_D,20},{P5_FENV_S,40},{P5_AENV_A,0},{P5_AENV_D,25},{P5_AENV_S,50} };
static const PresetDelta P5_P_ORCHBRASS[] = { {P5_OSCA_SAW,100},{P5_OSCB_SAW,90},{P5_OSCB_FINE,70},{P5_FILTER_CUTOFF,50},{P5_FILTER_ENV_AMT,60},{P5_FENV_A,30},{P5_FENV_D,60},{P5_AENV_A,25},{P5_AENV_D,50},{P5_AENV_S,100} };

// ── Stabs/keys ──
static const PresetDelta P5_P_STAB[] = { {P5_OSCA_PULSE,110},{P5_OSCA_PW,40},{P5_OSCB_PULSE,90},{P5_OSCB_FINE,90},{P5_FILTER_ENV_AMT,110},{P5_FENV_A,0},{P5_FENV_D,30},{P5_AENV_A,0},{P5_AENV_D,25} };
static const PresetDelta P5_P_EPIANO[] = { {P5_OSCA_PULSE,90},{P5_OSCA_PW,35},{P5_OSCB_PULSE,70},{P5_OSCB_FINE,68},{P5_FILTER_CUTOFF,65},{P5_FILTER_RES,15},{P5_FILTER_ENV_AMT,50},{P5_FENV_A,0},{P5_FENV_D,35},{P5_AENV_A,0},{P5_AENV_D,45},{P5_AENV_S,30},{P5_AENV_R,40} };
static const PresetDelta P5_P_CLAVFUNK[] = { {P5_OSCA_PULSE,110},{P5_OSCA_PW,22},{P5_FILTER_CUTOFF,55},{P5_FILTER_RES,60},{P5_FILTER_ENV_AMT,80},{P5_FENV_D,18},{P5_AENV_D,18},{P5_AENV_S,10} };

// ── Pads ──
static const PresetDelta P5_P_PAD[] = { {P5_OSCA_SAW,90},{P5_OSCB_SAW,70},{P5_OSCB_FINE,80},{P5_LFO_TO_OSC,10},{P5_FENV_A,60},{P5_FENV_D,90},{P5_AENV_A,70},{P5_AENV_D,100},{P5_AENV_S,90} };
static const PresetDelta P5_P_STRINGPAD[] = { {P5_OSCA_SAW,90},{P5_OSCB_SAW,80},{P5_OSCB_FINE,72},{P5_FILTER_CUTOFF,55},{P5_FENV_A,60},{P5_FENV_D,90},{P5_AENV_A,70},{P5_AENV_D,100},{P5_AENV_S,95},{P5_AENV_R,90} };
static const PresetDelta P5_P_GLASSPAD[] = { {P5_OSCA_PULSE,80},{P5_OSCA_PW,60},{P5_OSCB_PULSE,70},{P5_OSCB_PW,45},{P5_OSCB_FINE,75},{P5_LFO_TO_OSC,15},{P5_FILTER_CUTOFF,65},{P5_FENV_A,50},{P5_AENV_A,55},{P5_AENV_S,100} };
static const PresetDelta P5_P_WARMPAD[] = { {P5_OSCA_SAW,80},{P5_OSCB_SAW,55},{P5_OSCB_TRI,45},{P5_OSCB_FINE,68},{P5_FILTER_CUTOFF,40},{P5_FILTER_RES,15},{P5_FENV_A,80},{P5_AENV_A,75},{P5_AENV_D,50},{P5_AENV_S,100} };
static const PresetDelta P5_P_EVOLVINGPAD[] = { {P5_OSCA_SAW,90},{P5_OSCB_PULSE,80},{P5_OSCB_PW,55},{P5_OSCB_FINE,73},{P5_LFO_TO_FILTER,35},{P5_LFO_RATE,10},{P5_FILTER_CUTOFF,45},{P5_FENV_A,100},{P5_AENV_A,110},{P5_AENV_S,105} };

// ── Bells/plucks ──
static const PresetDelta P5_P_BELLPLUCK[] = { {P5_OSCA_PULSE,80},{P5_OSCA_PW,50},{P5_OSCB_PULSE,70},{P5_OSCB_FINE,95},{P5_PM_OSCB_AMT,50},{P5_PM_OSCB_TO_FREQA,EI(1,2)},{P5_FILTER_CUTOFF,90},{P5_FILTER_ENV_AMT,60},{P5_FENV_D,45},{P5_AENV_D,50},{P5_AENV_S,0} };
static const PresetDelta P5_P_MARIMBA[] = { {P5_OSCA_PULSE,90},{P5_OSCA_PW,40},{P5_FILTER_CUTOFF,55},{P5_FILTER_ENV_AMT,50},{P5_FENV_D,18},{P5_AENV_D,16},{P5_AENV_S,0} };
static const PresetDelta P5_P_HARPPLUCK[] = { {P5_OSCA_SAW,90},{P5_OSCB_SAW,60},{P5_OSCB_FINE,80},{P5_FILTER_CUTOFF,75},{P5_FILTER_RES,30},{P5_FENV_D,25},{P5_AENV_D,35},{P5_AENV_S,0} };

// ── SFX ──
static const PresetDelta P5_P_NOISEWIND[] = { {P5_OSCA_SAW,0},{P5_NOISE,110},{P5_FILTER_CUTOFF,30},{P5_FILTER_RES,20},{P5_AENV_A,90},{P5_AENV_D,60},{P5_AENV_S,110} };
static const PresetDelta P5_P_FILTERSWEEP[] = { {P5_OSCA_SAW,100},{P5_OSCB_SAW,90},{P5_OSCB_FINE,70},{P5_FILTER_CUTOFF,20},{P5_FILTER_RES,90},{P5_FILTER_ENV_AMT,120},{P5_FENV_A,80},{P5_FENV_D,90},{P5_AENV_A,10},{P5_AENV_S,110} };
static const PresetDelta P5_P_RINGFX[] = { {P5_OSCA_SAW,90},{P5_OSCB_PULSE,90},{P5_OSCB_FINE,110},{P5_PM_OSCB_AMT,90},{P5_PM_OSCB_TO_FREQA,EI(1,2)},{P5_FILTER_CUTOFF,70},{P5_FILTER_RES,60} };

// ── Arp ──
static const PresetDelta P5_P_ARPCLASSIC[] = { {P5_OSCA_PULSE,90},{P5_OSCA_PW,40},{P5_FILTER_CUTOFF,60},{P5_FILTER_RES,40},{P5_FILTER_ENV_AMT,60},{P5_FENV_D,25},{P5_AENV_D,20},{P5_AENV_S,30},{P5_ARP_ENABLE,EI(1,2)},{P5_ARP_RATE,80},{P5_ARP_GATE,70} };
static const PresetDelta P5_P_ARPPULSE[] = { {P5_OSCA_PULSE,100},{P5_OSCA_PW,25},{P5_OSCB_PULSE,80},{P5_OSCB_FINE,68},{P5_FILTER_CUTOFF,65},{P5_ARP_ENABLE,EI(1,2)},{P5_ARP_RATE,90},{P5_ARP_GATE,80},{P5_ARP_OCTAVES,48},{P5_ARP_DIR,EI(2,6)} };

static const Preset P5_PRESETS[] = {
    P5_PRESET("DEEP BASS",    P5_P_DEEPBASS),
    P5_PRESET("POLY BASS",    P5_P_POLYBASS),
    P5_PRESET("GROWL BASS",   P5_P_GROWLBASS),
    P5_PRESET("SEQ BASS",     P5_P_SEQBASS),
    P5_PRESET("SUB BASS",     P5_P_SUBBASS),
    P5_PRESET("P5 BRASS",     P5_P_BRASS),
    P5_PRESET("UNISON LEAD",  P5_P_UNISON_LEAD),
    P5_PRESET("SCREAM LEAD",  P5_P_SCREAMLEAD),
    P5_PRESET("SOFT LEAD",    P5_P_SOFTLEAD),
    P5_PRESET("PWM LEAD",     P5_P_PWMLEAD),
    P5_PRESET("RESO LEAD",    P5_P_RESOLEAD),
    P5_PRESET("BRASS STAB",   P5_P_BRASSSTAB),
    P5_PRESET("ORCH BRASS",   P5_P_ORCHBRASS),
    P5_PRESET("P5 STAB",      P5_P_STAB),
    P5_PRESET("E.PIANO",      P5_P_EPIANO),
    P5_PRESET("CLAV FUNK",    P5_P_CLAVFUNK),
    P5_PRESET("P5 PAD",       P5_P_PAD),
    P5_PRESET("STRING PAD",   P5_P_STRINGPAD),
    P5_PRESET("GLASS PAD",    P5_P_GLASSPAD),
    P5_PRESET("WARM PAD",     P5_P_WARMPAD),
    P5_PRESET("EVOLVING PAD", P5_P_EVOLVINGPAD),
    P5_PRESET("BELL PLUCK",   P5_P_BELLPLUCK),
    P5_PRESET("MARIMBA",      P5_P_MARIMBA),
    P5_PRESET("HARP PLUCK",   P5_P_HARPPLUCK),
    P5_PRESET("NOISE WIND",   P5_P_NOISEWIND),
    P5_PRESET("FILTER SWEEP", P5_P_FILTERSWEEP),
    P5_PRESET("RING FX",      P5_P_RINGFX),
    P5_PRESET("ARP CLASSIC",  P5_P_ARPCLASSIC),
    P5_PRESET("ARP PULSE",    P5_P_ARPPULSE),
};
#define P5_NUM_PRESETS (sizeof(P5_PRESETS) / sizeof(P5_PRESETS[0]))

static void p5ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < P5_NUM_PARAMS; i++) p5Raw[i] = (float)P5_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) p5Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    p5Decode();
}

static const EnginePageDef P5_PAGES[] = {
    { "OSC A/B",    P5_OSCA_SAW,     10 },
    { "POLY-MOD",   P5_PM_ENV_AMT,    6 },
    { "WHEEL-MOD",  P5_LFO_RATE,      4 },
    { "FILTER",     P5_FILTER_CUTOFF, 5 },
    { "ENVELOPES",  P5_FENV_A,        8 },
    { "VOICING",    P5_UNISON,        5 },
    { "OUTPUT",     P5_TREM_RATE,     8 },
    { "ARP",        P5_ARP_ENABLE,    8 },
};
#define P5_NUM_PAGES (sizeof(P5_PAGES) / sizeof(P5_PAGES[0]))

uint8_t p5MasterParamIdE() { return P5_MASTER; }
uint8_t p5FilterCutoffParamIdE()    { return P5_FILTER_CUTOFF; }
uint8_t p5FilterResonanceParamIdE() { return P5_FILTER_RES; }
uint8_t p5NumPagesE() { return (uint8_t)P5_NUM_PAGES; }
const EnginePageDef* p5PageE(uint8_t idx) { return (idx < P5_NUM_PAGES) ? &P5_PAGES[idx] : &P5_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
// P5_VOICES (10) is the array size — enough for the Prophet-10. How many of
// those slots are actually in play is a runtime count, set by
// p5SetPolyphonyE(): 5 for the Prophet-5 engine, 10 for the Prophet-10.
static P5Voice p5v[P5_VOICES];
static uint8_t p5ActivePoly = 5;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void p5SetPolyphonyE(uint8_t n) { p5ActivePoly = (n > P5_VOICES) ? P5_VOICES : n; }

// UNISON's own held-note stack.
//
// Unison fans one note across every internal voice, which makes it
// monophonic in exactly the way a MONO_LAST keyboard-style track is
// (see keystyle.cpp) — but the shared VoiceAlloc (voicealloc.h) that drives
// p5NoteOnE/p5NoteOffE was built around "one note-on -> one voice index,
// one note-off releases that same index" and knows nothing about unison
// fan-out. Left alone, that mismatch means: hold note A (unison fans to all
// voices), then play note B while still holding A (retargets all voices to
// B, correct — that's how real unison hardware behaves too), then release
// A — and the naive "note-off always silences every voice" behavior kills
// B as well, even though the player is still holding it down. This stack
// tracks what's actually held so a release only retargets/silences when the
// note being released is the one currently sounding.
#define P5_UNISON_MAX_HELD 8
static int     p5UnisonHeld[P5_UNISON_MAX_HELD];
static uint8_t p5UnisonHeldVel[P5_UNISON_MAX_HELD];
static uint8_t p5UnisonHeldCount = 0;

static void p5UnisonReset() { p5UnisonHeldCount = 0; }

static void p5UnisonPush(int note, uint8_t vel) {
    for (uint8_t i = 0; i < p5UnisonHeldCount; i++)
        if (p5UnisonHeld[i] == note) { p5UnisonHeldVel[i] = vel; return; }
    if (p5UnisonHeldCount < P5_UNISON_MAX_HELD) {
        p5UnisonHeld[p5UnisonHeldCount] = note;
        p5UnisonHeldVel[p5UnisonHeldCount] = vel;
        p5UnisonHeldCount++;
    }
}
// Returns true if `note` was found and removed.
static bool p5UnisonPop(int note) {
    for (uint8_t i = 0; i < p5UnisonHeldCount; i++) {
        if (p5UnisonHeld[i] == note) {
            for (uint8_t j = i; j < p5UnisonHeldCount - 1; j++) {
                p5UnisonHeld[j] = p5UnisonHeld[j + 1];
                p5UnisonHeldVel[j] = p5UnisonHeldVel[j + 1];
            }
            p5UnisonHeldCount--;
            return true;
        }
    }
    return false;
}

// Fixed per-voice detune, used only in UNISON — a few cents apart per voice
// is what turns "N copies of one oscillator" into the huge unison sound
// rather than one loud voice with phasing artifacts. Spread scales with
// however many voices are actually active.
static void p5AssignUnisonDetune() {
    for (int i = 0; i < p5ActivePoly; i++)
        p5v[i].detuneCents = ((float)i - (p5ActivePoly - 1) * 0.5f) * 4.0f;
}

void p5EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void p5EngineActivate() {
    for (int i = 0; i < P5_VOICES; i++) p5v[i] = P5Voice();
    p5AssignUnisonDetune();
    p5UnisonReset();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void p5ParamsInitE() { p5ApplyDeltas(nullptr, 0); }
void p5SetParameterE(uint8_t id, float v) { if (id < P5_NUM_PARAMS) { p5Raw[id] = v; p5Decode(); } }
float p5GetParameterE(uint8_t id) { return (id < P5_NUM_PARAMS) ? p5Raw[id] : 0.0f; }
void p5FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= P5_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = P5_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(p5Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(p5Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool p5ParamIsLockableE(uint8_t id) { return id < P5_NUM_PARAMS; }
uint8_t p5NumParamsE() { return P5_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo P5_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* p5ParamInfoE(uint8_t id) {
    return (id < P5_NUM_PARAMS) ? &P5_PARAM_INFO[id] : &P5_INVALID_PARAM;
}
uint8_t p5NumPresetsE() { return (uint8_t)P5_NUM_PRESETS; }
const char* p5PresetNameE(uint8_t idx) { return P5_PRESETS[idx].name; }
void p5LoadPresetE(uint8_t idx) { p5ApplyDeltas(P5_PRESETS[idx].deltas, P5_PRESETS[idx].n); }
void p5LoadInitPatchE() { p5ApplyDeltas(nullptr, 0); }
void p5SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < P5_NUM_PARAMS; i++) out[i] = (uint8_t)(p5Raw[i] * 255.0f + 0.5f);
}
void p5RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < P5_NUM_PARAMS; i++) p5Raw[i] = (float)in[i] * (1.0f / 255.0f);
    p5Decode();
}

uint8_t p5NumVoicesE() { return p5ActivePoly; }
void    p5VoiceInitE() { p5EngineActivate(); }

static inline void p5TriggerVoice(P5Voice& v, int note, uint8_t vel, bool trigEnv) {
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (p5.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (p5.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(p5.fA, p5.fD, p5.fS, p5.fR);
        v.ampEnv.trigger(p5.aA, p5.aD, p5.aS, p5.aR);
    }
}

void p5NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (p5.unison) {
        // One incoming note-on fans out to every internal voice — unison is
        // "one note plays all five", which is the opposite of what the
        // shared allocator (one note -> one voice index) is built for, so
        // it is handled entirely inside the engine instead. p5UnisonHeld
        // tracks which notes are actually down so a later release can tell
        // whether it's the currently-sounding note letting go, or just one
        // of possibly several notes held under it (see p5NoteOffE).
        p5UnisonPush(note, vel);
        for (int i = 0; i < p5ActivePoly; i++) p5TriggerVoice(p5v[i], note, vel, trigEnv);
        return;
    }
    if (voiceIdx < p5ActivePoly) p5TriggerVoice(p5v[voiceIdx], note, vel, trigEnv);
}
void p5NoteOffE(uint8_t voiceIdx, int note) {
    if (p5.unison) {
        // note < 0: no specific key released (the CPU polyphony guard
        // force-releasing a voice by index alone) — treat it the same as
        // "everything let go" rather than guessing which held note it meant.
        // A release for a note p5UnisonPush() had no room to track (more
        // than P5_UNISON_MAX_HELD notes down at once) leaves the stack
        // exactly as it was — still correct, since it just falls through to
        // "retarget to whatever's still tracked" below rather than wrongly
        // silencing notes that really are still held.
        if (note < 0) p5UnisonReset(); else p5UnisonPop(note);
        if (p5UnisonHeldCount == 0) {
            for (int i = 0; i < p5ActivePoly; i++) { p5v[i].gate = false; p5v[i].filtEnv.release(); p5v[i].ampEnv.release(); }
            return;
        }
        // Still holding at least one other note: retarget every voice to it
        // (last-note priority) instead of releasing — the note that was
        // actually let go isn't the one currently sounding.
        const uint8_t top = p5UnisonHeldCount - 1;
        for (int i = 0; i < p5ActivePoly; i++)
            p5TriggerVoice(p5v[i], p5UnisonHeld[top], p5UnisonHeldVel[top], true);
        return;
    }
    if (voiceIdx >= p5ActivePoly) return;
    p5v[voiceIdx].gate = false;
    p5v[voiceIdx].filtEnv.release();
    p5v[voiceIdx].ampEnv.release();
}
void p5AllOffE() { p5UnisonReset(); for (int i = 0; i < p5ActivePoly; i++) p5NoteOffE(i, -1); }
void p5SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < p5ActivePoly) p5v[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int p5RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * p5.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = p5.tremMix > 0.004f;
    const bool fxDelay  = p5.dlyMix  > 0.004f;
    const bool fxReverb = p5.revMix  > 0.004f;
    g_fxChorus.setRate(p5.tremRate);
    g_fxReverb.setRoom(p5.revSize, 0.4f);
    g_fxDelay.setTimeMs(p5.dlyTimeMs, false);

    static uint32_t lfoPhase = 0;
    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        lfoPhase += (uint32_t)(p5.lfoRateHz * PHASE_PER_HZ);
        float lfo;
        switch (p5.lfoShape) {
        case 1:  lfo = (lfoPhase & 0x80000000u) ? -1.0f : 1.0f; break;
        case 2:  lfo = 2.0f * phaseToFloat(lfoPhase) - 1.0f; break;
        default: lfo = sineFromPhase(lfoPhase); break;
        }
        const float lfoOscSemis = lfo * p5.lfoToOsc * modWheel * 2.0f;      // vibrato, wheel-scaled
        const float lfoFiltSemis = lfo * p5.lfoToFilter * modWheel * 24.0f;

        float mix = 0.0f;
        int sounding = 0;
        const int nv = p5ActivePoly;

        for (int i = 0; i < nv; i++) {
            P5Voice& v = p5v[i];
            const bool alive = v.ampEnv.stage != ADSR_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                float detuneMul = fastPow2((v.detuneCents * (p5.unison ? 1.0f : 0.0f)) / 1200.0f);
                float base = v.currentFreq * bendMul * detuneMul;

                const float envF = v.filtEnv.process();

                float freqASemis = lfoOscSemis;
                if (p5.pmEnvToFreqA)  freqASemis += p5.pmEnvAmt * envF * 24.0f;
                // Osc B's own raw waveform, sampled once per control tick as
                // the Poly-Mod source — cheap, and 8 updates/block is plenty
                // for a modulation source rather than an audible carrier.
                float oscBRaw = v.oscB.saw(phaseToFloat(v.oscB.phase));
                if (p5.pmOscBToFreqA) freqASemis += p5.pmOscBAmt * oscBRaw * 12.0f;

                v.oscA.setFreq(base * fastPow2(freqASemis / 12.0f));
                v.oscB.setFreq(base * p5.fineB * fastPow2(lfoOscSemis / 12.0f));

                float kbdSemis = p5.kbdAmt * (float)(v.note - 60);
                float filtSemis = kbdSemis + p5.envAmt * envF * 48.0f + lfoFiltSemis;
                if (p5.pmEnvToFilter)  filtSemis += p5.pmEnvAmt * envF * 36.0f;
                if (p5.pmOscBToFilter) filtSemis += p5.pmOscBAmt * oscBRaw * 24.0f;
                float cutoff = safeClamp(p5.cutoffHz * fastPow2(filtSemis / 12.0f), 20.0f, 18000.0f);
                if (p5.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, p5.resonance);
                else                            v.varFilt.setCutoff(cutoff, p5.resonance, p5.filterType);
            }

            bool wrappedB = false;
            float tB = v.oscB.advance(&wrappedB);
            if (p5.syncA && wrappedB) v.oscA.resetPhase();
            bool wrappedA = false;
            float tA = v.oscA.advance(&wrappedA);

            float oscAOut = v.oscA.saw(tA) * p5.sawA + v.oscA.pulse(tA, p5.pwA) * p5.pulseA;
            // Oscillator B alone gets a third waveform on the real panel —
            // Triangle — that Oscillator A never had; see P5_OSCB_TRI.
            float oscBOut = v.oscB.saw(tB) * p5.sawB + v.oscB.pulse(tB, p5.pwB) * p5.pulseB
                          + v.oscB.triangle(tB) * p5.triB;
            float noiseOut = v.noise.white() * p5.noiseLvl;

            float voiceIn = (oscAOut + oscBOut + noiseOut) * 0.5f;
            float filtered = (p5.filterType == VF_STOCK)
                ? v.filter.process(voiceIn)
                : v.varFilt.process(voiceIn, p5.filterType, p5.resonance);
            float amp = v.ampEnv.process();
            // No velocity scaling here, deliberately: the original Prophet-5/
            // -10 keybed (Rev 1 through 3.x — everything this engine models)
            // had no velocity sensitivity and no aftertouch at all. Every
            // note plays at the same level regardless of how hard it's
            // struck; velocity and aftertouch weren't added until the 2020
            // Rev 4 reissue, a different instrument from the one modelled
            // here. v.velocity/velNorm are still tracked (for anything a
            // caller wants them for) but never reach the audio path.
            mix += filtered * amp;
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER used to be multiplied straight into the saturator below, so
        // turning the volume up also drove more distortion. P5_MIX_DRIVE is
        // a fixed pre-saturator drive instead, tuned from a probe over every
        // stock preset at both polyphonies this code serves (Prophet-5's 5
        // voices and Prophet-10's 10 — the 10-voice case runs hotter, peaking
        // 0.48-0.80, so that's the case this constant is sized for). MASTER
        // itself is now applied as pure linear gain after the saturator and
        // the whole effect chain.
        const float P5_MIX_DRIVE = 1.6f;
        float out = fastTanh(mix * P5_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, p5.tremDepth, p5.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, p5.dlyFb, p5.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * p5.revMix; r += rr * p5.revMix;
        }
        l *= p5.master; r *= p5.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

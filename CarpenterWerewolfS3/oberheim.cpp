#include "oberheim.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  oberheim.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

const float OB_VOICE_DETUNE[OB_VOICES] = { 0.0f, -6.5f, 5.0f, -3.5f };     // cents
const float OB_VOICE_FILT[OB_VOICES]   = { 0.0f, 0.045f, -0.035f, 0.06f }; // oct-ish
const float OB_VOICE_LEVEL[OB_VOICES]  = { 1.000f, 0.965f, 1.035f, 0.978f };

enum ObParamId : uint8_t {
    OB_OSC1_SAW = 0, OB_OSC1_PULSE, OB_OSC1_PW,
    OB_OSC2_SAW, OB_OSC2_PULSE, OB_OSC2_PW, OB_OSC2_FINE,
    OB_NOISE,
    OB_FILTER_CUTOFF, OB_FILTER_TYPE, OB_FILTER_RES, OB_FILTER_MODE, OB_FILTER_ENV_AMT, OB_FILTER_KBD,
    OB_FENV_A, OB_FENV_D, OB_FENV_S, OB_FENV_R,
    OB_AENV_A, OB_AENV_D, OB_AENV_S, OB_AENV_R,
    OB_LFO_RATE, OB_LFO_TO_OSC, OB_LFO_TO_FILTER,
    OB_GLIDE_ON, OB_GLIDE_TIME, OB_MASTER, OB_BEND_RANGE,

    OB_TREM_RATE, OB_TREM_DEPTH, OB_TREM_MIX,
    OB_DLY_TIME, OB_DLY_FB, OB_DLY_MIX, OB_REV_SIZE, OB_REV_MIX,

    OB_ARP_ENABLE, OB_ARP_PATTERN, OB_ARP_RATE, OB_ARP_GATE,
    OB_ARP_OCTAVES, OB_ARP_DIR, OB_ARP_LATCH, OB_ARP_SYNC,

    OB_NUM_PARAMS
};

static const char* const OB_ONOFF[2]       = { "OFF", "ON" };
static const char* const OB_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define OBROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [OB_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo OB_PARAM_INFO[] = {
    OBROW("O1 SAW",   PK_CONT,   nullptr, 0,   0, 127,  20),
    OBROW("O1 PULSE", PK_CONT,   nullptr, 0,   0, 127,  21),
    OBROW("O1 PW",    PK_CONT,   nullptr, 0,   0, 127,  22),
    OBROW("O2 SAW",   PK_CONT,   nullptr, 0,   0, 127,  23),
    OBROW("O2 PULSE", PK_CONT,   nullptr, 0,   0, 127,  24),
    OBROW("O2 PW",    PK_CONT,   nullptr, 0,   0, 127,  25),
    OBROW("O2 FINE",  PK_CENTS,  nullptr, 0,-700, 700,  26),
    OBROW("NOISE",    PK_CONT,   nullptr, 0,   0, 127,  27),
    OBROW("CUTOFF",   PK_HZ,     nullptr, 0,  20, 18000, 28),
    OBROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 29),
    OBROW("RESONANCE",PK_CONT,   nullptr, 0,   0, 127,  30),
    OBROW("MODE",     PK_CONT,   nullptr, 0,   0, 100,  31),
    OBROW("VCF ENV AMT",PK_CONT, nullptr, 0,   0, 127,  33),
    OBROW("KBD AMT",  PK_CONT,   nullptr, 0,   0, 127,  34),
    OBROW("VCF ATK",  PK_MS,     nullptr, 0,   1, 8000,  35),
    OBROW("VCF DEC",  PK_MS,     nullptr, 0,   3, 12000, 36),
    OBROW("VCF SUS",  PK_CONT,   nullptr, 0,   0, 127,  37),
    OBROW("VCF REL",  PK_MS,     nullptr, 0,   3, 12000, 39),
    OBROW("VCA ATK",  PK_MS,     nullptr, 0,   1, 8000,  40),
    OBROW("VCA DEC",  PK_MS,     nullptr, 0,   3, 12000, 41),
    OBROW("VCA SUS",  PK_CONT,   nullptr, 0,   0, 127,  42),
    OBROW("VCA REL",  PK_MS,     nullptr, 0,   3, 12000, 43),
    OBROW("LFO RATE", PK_HZ,     nullptr, 0,   0, 20,    44),
    OBROW("LFO>OSC",  PK_CONT,   nullptr, 0,   0, 127,   45),
    OBROW("LFO>VCF",  PK_CONT,   nullptr, 0,   0, 127,   46),
    OBROW("GLIDE",    PK_TOGGLE, OB_ONOFF, 2,  0, 1,     47),
    OBROW("GLIDE TIME",PK_MS,    nullptr, 0,   0, 5000,  48),
    OBROW("MASTER",   PK_CONT,   nullptr, 0,   0, 127,   49),
    OBROW("BEND RNG", PK_INT,    nullptr, 0,   1, 24,    50),
    OBROW("TREM RATE",PK_HZ,     nullptr, 0,   0, 20,    51),
    OBROW("TREM DEPTH",PK_CONT,  nullptr, 0,   0, 127,   52),
    OBROW("TREM MIX", PK_CONT,   nullptr, 0,   0, 127,   53),
    OBROW("DLY TIME", PK_MS,     nullptr, 0,  20, 1200,  54),
    OBROW("DLY FDBK", PK_CONT,   nullptr, 0,   0, 127,   55),
    OBROW("DLY MIX",  PK_CONT,   nullptr, 0,   0, 127,   56),
    OBROW("REV SIZE", PK_CONT,   nullptr, 0,   0, 127,   57),
    OBROW("REV MIX",  PK_CONT,   nullptr, 0,   0, 127,   58),
    OBROW("ARP ON",   PK_TOGGLE, OB_ONOFF, 2,  0, 1,     59),
    OBROW("ARP PATT", PK_INT,    nullptr, 0,   0, 7,     60),
    OBROW("ARP RATE", PK_CONT,   nullptr, 0,   0, 127,   61),
    OBROW("ARP GATE", PK_CONT,   nullptr, 0,   0, 127,   62),
    OBROW("ARP OCT",  PK_INT,    nullptr, 0,   1, 4,     63),
    OBROW("ARP DIR",  PK_ENUM,   OB_ARP_DIR_NAMES, 6, 0, 5, 65),
    OBROW("ARP LATCH",PK_TOGGLE, OB_ONOFF, 2,  0, 1,     66),
    OBROW("ARP SYNC", PK_TOGGLE, OB_ONOFF, 2,  0, 1,     67),
};
static_assert(sizeof(OB_PARAM_INFO) / sizeof(OB_PARAM_INFO[0]) == OB_NUM_PARAMS,
              "OB_PARAM_INFO must have exactly OB_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float obRaw[OB_NUM_PARAMS];

struct ObDecoded {
    float sawA, pulseA, pwA;
    float sawB, pulseB, pwB, fineB;
    float noiseLvl;
    float cutoffHz; uint8_t filterType; float resonance, filterMode01, envAmt, kbdAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    float lfoRateHz, lfoToOsc, lfoToFilter;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static ObDecoded ob;

static void obDecode() {
    ob.sawA = obRaw[OB_OSC1_SAW]; ob.pulseA = obRaw[OB_OSC1_PULSE];
    ob.pwA  = 0.05f + obRaw[OB_OSC1_PW] * 0.45f;

    ob.sawB = obRaw[OB_OSC2_SAW]; ob.pulseB = obRaw[OB_OSC2_PULSE];
    ob.pwB  = 0.05f + obRaw[OB_OSC2_PW] * 0.45f;
    ob.fineB = fastPow2(((obRaw[OB_OSC2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);

    ob.noiseLvl = obRaw[OB_NOISE];

    ob.cutoffHz = 20.0f * fastPow2(obRaw[OB_FILTER_CUTOFF] * 9.8f);
    ob.filterType = (uint8_t)(obRaw[OB_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    ob.resonance = obRaw[OB_FILTER_RES];
    ob.filterMode01 = obRaw[OB_FILTER_MODE];
    ob.envAmt = obRaw[OB_FILTER_ENV_AMT];
    ob.kbdAmt = obRaw[OB_FILTER_KBD];

    ob.fA = 1.0f / (1.0f + obRaw[OB_FENV_A] * 800.0f);
    ob.fD = 1.0f / (1.0f + obRaw[OB_FENV_D] * 1600.0f);
    ob.fS = obRaw[OB_FENV_S];
    ob.fR = 1.0f / (1.0f + obRaw[OB_FENV_R] * 1600.0f);
    ob.aA = 1.0f / (1.0f + obRaw[OB_AENV_A] * 800.0f);
    ob.aD = 1.0f / (1.0f + obRaw[OB_AENV_D] * 1600.0f);
    ob.aS = obRaw[OB_AENV_S];
    ob.aR = 1.0f / (1.0f + obRaw[OB_AENV_R] * 1600.0f);

    ob.lfoRateHz = 0.1f + obRaw[OB_LFO_RATE] * 19.9f;
    ob.lfoToOsc = obRaw[OB_LFO_TO_OSC];
    ob.lfoToFilter = obRaw[OB_LFO_TO_FILTER];

    ob.glideOn = obRaw[OB_GLIDE_ON] > 0.5f;
    ob.glideTimeS = 0.005f + obRaw[OB_GLIDE_TIME] * 5.0f;
    ob.master = obRaw[OB_MASTER];
    ob.bendRangeSemis = 1.0f + obRaw[OB_BEND_RANGE] * 23.0f;

    ob.tremRate = obRaw[OB_TREM_RATE] * 20.0f;
    ob.tremDepth = obRaw[OB_TREM_DEPTH];
    ob.tremMix = obRaw[OB_TREM_MIX];
    ob.dlyTimeMs = 20.0f + obRaw[OB_DLY_TIME] * 1180.0f;
    ob.dlyFb = obRaw[OB_DLY_FB];
    ob.dlyMix = obRaw[OB_DLY_MIX];
    ob.revSize = obRaw[OB_REV_SIZE];
    ob.revMix = obRaw[OB_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n): every PK_ENUM/PK_TOGGLE decode here (and
// the shared arp reader in CarpenterSynth.ino) reads a raw INIT/delta byte
// as a proportion of 0..127, not a literal option index -- EI() returns the
// raw byte that round-trips through that decode back to option `idx` out of
// `n`. MODE is a plain continuous knob (0=LP, ~42=Notch, ~85=BP, 127=HP --
// see semModeMorph in oberheim.h), not an enum, so presets just write the
// raw position they want directly.
static const uint8_t OB_INIT[] = {
    110,0,64,             // osc1 saw/pulse/pw
    0,90,64,64,           // osc2 saw/pulse/pw/fine (fine centered = in tune)
    0,                    // noise
    100,0,20,0,60,0,      // filter cutoff/type/res/mode(LP)/envamt/kbd
    20,60,90,40,          // filter env ADSR
    5,70,110,50,          // amp env ADSR
    0,0,0,                // LFO rate/to-osc/to-filter
    0,64,                 // glide/glidetime
    110,20,               // master/bend
    0,0,0, 0,0,0,0,0,     // output
    0,0,64,80,0,0,0,0,    // arp
};
static_assert(sizeof(OB_INIT) / sizeof(OB_INIT[0]) == OB_NUM_PARAMS,
              "OB_INIT must have exactly OB_NUM_PARAMS values");

#define OB_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta OB_P_BASS[]      = { {OB_OSC1_SAW,110},{OB_OSC2_PULSE,90},{OB_OSC2_FINE,60},{OB_FILTER_CUTOFF,25},{OB_FILTER_RES,50},{OB_FILTER_ENV_AMT,90},{OB_FENV_D,30} };
static const PresetDelta OB_P_GROWLBASS[] = { {OB_OSC1_SAW,110},{OB_OSC2_SAW,90},{OB_OSC2_FINE,74},{OB_FILTER_CUTOFF,28},{OB_FILTER_RES,70},{OB_FILTER_MODE,42},{OB_FILTER_ENV_AMT,80},{OB_FENV_D,35} };
static const PresetDelta OB_P_SQUAREBASS[]= { {OB_OSC1_PULSE,110},{OB_OSC1_PW,20},{OB_OSC2_PULSE,0},{OB_FILTER_CUTOFF,30},{OB_FILTER_RES,55},{OB_FILTER_ENV_AMT,70},{OB_FENV_D,25} };
static const PresetDelta OB_P_RESOBASS[]  = { {OB_OSC1_SAW,110},{OB_OSC2_PULSE,0},{OB_FILTER_CUTOFF,26},{OB_FILTER_RES,110},{OB_FILTER_ENV_AMT,90},{OB_FENV_A,0},{OB_FENV_D,30},{OB_FENV_S,0} };
static const PresetDelta OB_P_NOTCHBASS[] = { {OB_OSC1_SAW,100},{OB_OSC2_SAW,80},{OB_OSC2_FINE,68},{OB_FILTER_CUTOFF,32},{OB_FILTER_RES,60},{OB_FILTER_MODE,42},{OB_FENV_D,28} };

// ── Leads ──
static const PresetDelta OB_P_LEAD[]      = { {OB_OSC1_SAW,110},{OB_OSC2_SAW,90},{OB_OSC2_FINE,72},{OB_FILTER_CUTOFF,80},{OB_FILTER_RES,60},{OB_FILTER_ENV_AMT,40},{OB_FENV_A,0},{OB_FENV_D,40} };
static const PresetDelta OB_P_HPLEAD[]    = { {OB_OSC1_PULSE,110},{OB_OSC1_PW,30},{OB_FILTER_CUTOFF,55},{OB_FILTER_RES,70},{OB_FILTER_MODE,127},{OB_FENV_A,0},{OB_FENV_D,40} };
static const PresetDelta OB_P_BPLEAD[]    = { {OB_OSC1_SAW,100},{OB_OSC2_SAW,80},{OB_OSC2_FINE,70},{OB_FILTER_CUTOFF,65},{OB_FILTER_RES,95},{OB_FILTER_MODE,85},{OB_FENV_D,35} };
static const PresetDelta OB_P_DETUNELEAD[]= { {OB_OSC1_SAW,110},{OB_OSC2_SAW,110},{OB_OSC2_FINE,95},{OB_FILTER_CUTOFF,80},{OB_FILTER_RES,50} };
static const PresetDelta OB_P_GLIDELEAD[] = { {OB_OSC1_SAW,110},{OB_OSC2_PULSE,80},{OB_OSC2_FINE,74},{OB_FILTER_CUTOFF,75},{OB_FILTER_RES,55},{OB_GLIDE_ON,EI(1,2)},{OB_GLIDE_TIME,18} };
static const PresetDelta OB_P_SCREAMLEAD[]= { {OB_OSC1_SAW,110},{OB_FILTER_CUTOFF,90},{OB_FILTER_RES,120},{OB_FILTER_ENV_AMT,60},{OB_FENV_A,0},{OB_FENV_D,45} };

// ── Pads ──
static const PresetDelta OB_P_FOURVOICEPAD[]={ {OB_OSC1_SAW,90},{OB_OSC2_SAW,80},{OB_OSC2_FINE,74},{OB_FILTER_CUTOFF,50},{OB_FILTER_RES,20},{OB_FENV_A,70},{OB_AENV_A,70},{OB_AENV_D,50},{OB_AENV_S,100} };
static const PresetDelta OB_P_NOTCHPAD[]  = { {OB_OSC1_PULSE,80},{OB_OSC1_PW,55},{OB_OSC2_PULSE,70},{OB_OSC2_PW,45},{OB_OSC2_FINE,73},{OB_FILTER_CUTOFF,48},{OB_FILTER_MODE,42},{OB_FENV_A,50},{OB_AENV_A,60},{OB_AENV_S,100} };
static const PresetDelta OB_P_HPSHIMMER[] = { {OB_OSC1_SAW,80},{OB_OSC2_SAW,75},{OB_OSC2_FINE,80},{OB_FILTER_CUTOFF,70},{OB_FILTER_MODE,110},{OB_AENV_A,90},{OB_AENV_S,100},{OB_REV_MIX,50} };
static const PresetDelta OB_P_WARMLPPAD[] = { {OB_OSC1_SAW,80},{OB_OSC2_PULSE,70},{OB_OSC2_PW,60},{OB_OSC2_FINE,70},{OB_FILTER_CUTOFF,42},{OB_FILTER_RES,15},{OB_AENV_A,80},{OB_AENV_D,50},{OB_AENV_S,100} };
static const PresetDelta OB_P_SWEEPPAD[]  = { {OB_OSC1_SAW,90},{OB_OSC2_SAW,80},{OB_OSC2_FINE,73},{OB_FILTER_CUTOFF,20},{OB_FILTER_RES,55},{OB_FILTER_ENV_AMT,110},{OB_FENV_A,100},{OB_FENV_D,110},{OB_AENV_A,90},{OB_AENV_S,100} };

// ── Brass ──
static const PresetDelta OB_P_BRASS[]     = { {OB_OSC1_SAW,110},{OB_OSC1_PULSE,40},{OB_OSC2_SAW,100},{OB_OSC2_FINE,72},{OB_FILTER_CUTOFF,55},{OB_FILTER_ENV_AMT,80},{OB_FENV_A,15},{OB_FENV_D,50},{OB_AENV_A,10} };
static const PresetDelta OB_P_BRASSSTAB[] = { {OB_OSC1_SAW,110},{OB_OSC2_SAW,100},{OB_OSC2_FINE,72},{OB_FILTER_CUTOFF,60},{OB_FILTER_ENV_AMT,90},{OB_FENV_A,0},{OB_FENV_D,20},{OB_FENV_S,40},{OB_AENV_A,0},{OB_AENV_D,25} };
static const PresetDelta OB_P_MELLOWBRASS[]={ {OB_OSC1_PULSE,90},{OB_OSC1_PW,40},{OB_OSC2_PULSE,80},{OB_OSC2_FINE,70},{OB_FILTER_CUTOFF,45},{OB_FILTER_ENV_AMT,50},{OB_FENV_A,30},{OB_AENV_A,25} };

// ── Bells/plucks ──
static const PresetDelta OB_P_BPPLUCK[]   = { {OB_OSC1_SAW,90},{OB_FILTER_CUTOFF,70},{OB_FILTER_RES,110},{OB_FILTER_MODE,85},{OB_FILTER_ENV_AMT,70},{OB_FENV_D,30},{OB_AENV_D,35},{OB_AENV_S,0} };
static const PresetDelta OB_P_HPPLUCK[]   = { {OB_OSC1_PULSE,90},{OB_OSC1_PW,35},{OB_FILTER_CUTOFF,60},{OB_FILTER_RES,80},{OB_FILTER_MODE,127},{OB_FENV_D,25},{OB_AENV_D,28},{OB_AENV_S,0} };
static const PresetDelta OB_P_MARIMBA[]   = { {OB_OSC1_PULSE,90},{OB_OSC1_PW,40},{OB_FILTER_CUTOFF,55},{OB_FILTER_ENV_AMT,50},{OB_FENV_D,18},{OB_AENV_D,16},{OB_AENV_S,0} };
static const PresetDelta OB_P_GLASSBELLS[]= { {OB_OSC1_SAW,80},{OB_OSC2_SAW,80},{OB_OSC2_FINE,90},{OB_FILTER_CUTOFF,90},{OB_FILTER_MODE,127},{OB_FILTER_RES,40},{OB_FENV_D,45},{OB_AENV_D,50},{OB_AENV_S,0} };

// ── SFX ──
static const PresetDelta OB_P_NOISESWEEP[]= { {OB_OSC1_SAW,0},{OB_NOISE,110},{OB_FILTER_CUTOFF,25},{OB_FILTER_RES,90},{OB_FILTER_ENV_AMT,120},{OB_FENV_A,90},{OB_FENV_D,100},{OB_AENV_A,10},{OB_AENV_S,110} };
static const PresetDelta OB_P_NOTCHFX[]   = { {OB_OSC1_SAW,90},{OB_OSC2_SAW,90},{OB_OSC2_FINE,85},{OB_FILTER_CUTOFF,45},{OB_FILTER_RES,110},{OB_FILTER_MODE,42},{OB_LFO_TO_FILTER,60},{OB_LFO_RATE,20} };
static const PresetDelta OB_P_WINDFX[]    = { {OB_OSC1_SAW,0},{OB_NOISE,90},{OB_FILTER_CUTOFF,35},{OB_FILTER_RES,20},{OB_AENV_A,100},{OB_AENV_S,110},{OB_REV_MIX,40} };
static const PresetDelta OB_P_MODESWEEP[] = { {OB_OSC1_SAW,100},{OB_OSC2_SAW,90},{OB_OSC2_FINE,72},{OB_FILTER_CUTOFF,55},{OB_FILTER_RES,80},{OB_FILTER_ENV_AMT,0},{OB_LFO_TO_FILTER,50},{OB_LFO_RATE,15},{OB_AENV_S,110} };

// ── Arp ──
static const PresetDelta OB_P_ARPCLASSIC[]= { {OB_OSC1_PULSE,90},{OB_OSC1_PW,40},{OB_FILTER_CUTOFF,60},{OB_FILTER_RES,50},{OB_FILTER_ENV_AMT,50},{OB_FENV_D,20},{OB_AENV_D,18},{OB_AENV_S,30},{OB_ARP_ENABLE,EI(1,2)},{OB_ARP_RATE,80},{OB_ARP_GATE,70} };
static const PresetDelta OB_P_ARPOCTAVES[]= { {OB_OSC1_SAW,100},{OB_FILTER_CUTOFF,70},{OB_FILTER_RES,40},{OB_ARP_ENABLE,EI(1,2)},{OB_ARP_RATE,90},{OB_ARP_GATE,80},{OB_ARP_OCTAVES,80},{OB_ARP_DIR,EI(2,6)} };
static const PresetDelta OB_P_ARPPULSE[]  = { {OB_OSC1_PULSE,100},{OB_OSC1_PW,25},{OB_OSC2_PULSE,80},{OB_OSC2_FINE,68},{OB_FILTER_CUTOFF,65},{OB_ARP_ENABLE,EI(1,2)},{OB_ARP_RATE,90},{OB_ARP_GATE,75} };

static const Preset OB_PRESETS[] = {
    OB_PRESET("OB BASS",      OB_P_BASS),
    OB_PRESET("GROWL BASS",   OB_P_GROWLBASS),
    OB_PRESET("SQUARE BASS",  OB_P_SQUAREBASS),
    OB_PRESET("RESO BASS",    OB_P_RESOBASS),
    OB_PRESET("NOTCH BASS",   OB_P_NOTCHBASS),
    OB_PRESET("OB LEAD",      OB_P_LEAD),
    OB_PRESET("HP LEAD",      OB_P_HPLEAD),
    OB_PRESET("BP LEAD",      OB_P_BPLEAD),
    OB_PRESET("DETUNE LEAD",  OB_P_DETUNELEAD),
    OB_PRESET("GLIDE LEAD",   OB_P_GLIDELEAD),
    OB_PRESET("SCREAM LEAD",  OB_P_SCREAMLEAD),
    OB_PRESET("FOUR-VOICE PAD",OB_P_FOURVOICEPAD),
    OB_PRESET("NOTCH PAD",    OB_P_NOTCHPAD),
    OB_PRESET("HP SHIMMER",   OB_P_HPSHIMMER),
    OB_PRESET("WARM LP PAD",  OB_P_WARMLPPAD),
    OB_PRESET("SWEEP PAD",    OB_P_SWEEPPAD),
    OB_PRESET("OB BRASS",     OB_P_BRASS),
    OB_PRESET("BRASS STAB",   OB_P_BRASSSTAB),
    OB_PRESET("MELLOW BRASS", OB_P_MELLOWBRASS),
    OB_PRESET("BP PLUCK",     OB_P_BPPLUCK),
    OB_PRESET("HP PLUCK",     OB_P_HPPLUCK),
    OB_PRESET("MARIMBA",      OB_P_MARIMBA),
    OB_PRESET("GLASS BELLS",  OB_P_GLASSBELLS),
    OB_PRESET("NOISE SWEEP",  OB_P_NOISESWEEP),
    OB_PRESET("NOTCH FX",     OB_P_NOTCHFX),
    OB_PRESET("WIND FX",      OB_P_WINDFX),
    OB_PRESET("MODE SWEEP",   OB_P_MODESWEEP),
    OB_PRESET("ARP CLASSIC",  OB_P_ARPCLASSIC),
    OB_PRESET("ARP OCTAVES",  OB_P_ARPOCTAVES),
    OB_PRESET("ARP PULSE",    OB_P_ARPPULSE),
};
#define OB_NUM_PRESETS (sizeof(OB_PRESETS) / sizeof(OB_PRESETS[0]))

static void obApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < OB_NUM_PARAMS; i++) obRaw[i] = (float)OB_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) obRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    obDecode();
}

static const EnginePageDef OB_PAGES[] = {
    { "OSC 1/2",    OB_OSC1_SAW,      7 },
    { "NOISE/LFO",  OB_NOISE,         4 },
    { "FILTER",     OB_FILTER_CUTOFF, 6 },
    { "ENVELOPES",  OB_FENV_A,        8 },
    { "VOICING",    OB_GLIDE_ON,      4 },
    { "OUTPUT",     OB_TREM_RATE,     8 },
    { "ARP",        OB_ARP_ENABLE,    8 },
};
#define OB_NUM_PAGES (sizeof(OB_PAGES) / sizeof(OB_PAGES[0]))

uint8_t obMasterParamIdE() { return OB_MASTER; }
uint8_t obFilterCutoffParamIdE()    { return OB_FILTER_CUTOFF; }
uint8_t obFilterResonanceParamIdE() { return OB_FILTER_RES; }
uint8_t obNumPagesE() { return (uint8_t)OB_NUM_PAGES; }
const EnginePageDef* obPageE(uint8_t idx) { return (idx < OB_NUM_PAGES) ? &OB_PAGES[idx] : &OB_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static ObVoice obv[OB_VOICES];
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void obEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void obEngineActivate() {
    for (int i = 0; i < OB_VOICES; i++) obv[i] = ObVoice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void obParamsInitE() { obApplyDeltas(nullptr, 0); }
void obSetParameterE(uint8_t id, float v) { if (id < OB_NUM_PARAMS) { obRaw[id] = v; obDecode(); } }
float obGetParameterE(uint8_t id) { return (id < OB_NUM_PARAMS) ? obRaw[id] : 0.0f; }
void obFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= OB_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = OB_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(obRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(obRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool obParamIsLockableE(uint8_t id) { return id < OB_NUM_PARAMS; }
uint8_t obNumParamsE() { return OB_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo OB_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* obParamInfoE(uint8_t id) {
    return (id < OB_NUM_PARAMS) ? &OB_PARAM_INFO[id] : &OB_INVALID_PARAM;
}
uint8_t obNumPresetsE() { return (uint8_t)OB_NUM_PRESETS; }
const char* obPresetNameE(uint8_t idx) { return OB_PRESETS[idx].name; }
void obLoadPresetE(uint8_t idx) { obApplyDeltas(OB_PRESETS[idx].deltas, OB_PRESETS[idx].n); }
void obLoadInitPatchE() { obApplyDeltas(nullptr, 0); }
void obSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < OB_NUM_PARAMS; i++) out[i] = (uint8_t)(obRaw[i] * 255.0f + 0.5f);
}
void obRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < OB_NUM_PARAMS; i++) obRaw[i] = (float)in[i] * (1.0f / 255.0f);
    obDecode();
}

uint8_t obNumVoicesE() { return OB_VOICES; }
void    obVoiceInitE() { obEngineActivate(); }

void obNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= OB_VOICES) return;
    ObVoice& v = obv[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (ob.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (ob.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(ob.fA, ob.fD, ob.fS, ob.fR);
        v.ampEnv.trigger(ob.aA, ob.aD, ob.aS, ob.aR);
    }
}
void obNoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= OB_VOICES) return;
    ObVoice& v = obv[voiceIdx];
    v.gate = false;
    v.filtEnv.release();
    v.ampEnv.release();
}
void obAllOffE() { for (int i = 0; i < OB_VOICES; i++) obNoteOffE(i); }
void obSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < OB_VOICES) obv[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int obRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * ob.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = ob.tremMix > 0.004f;
    const bool fxDelay  = ob.dlyMix  > 0.004f;
    const bool fxReverb = ob.revMix  > 0.004f;
    g_fxChorus.setRate(ob.tremRate);
    g_fxReverb.setRoom(ob.revSize, 0.4f);
    g_fxDelay.setTimeMs(ob.dlyTimeMs, false);

    static uint32_t lfoPhase = 0;
    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        lfoPhase += (uint32_t)(ob.lfoRateHz * PHASE_PER_HZ);
        const float lfo = sineFromPhase(lfoPhase);
        const float lfoOscSemis = lfo * ob.lfoToOsc * modWheel * 2.0f;
        const float lfoFiltSemis = lfo * ob.lfoToFilter * modWheel * 24.0f;

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < OB_VOICES; i++) {
            ObVoice& v = obv[i];
            const bool alive = v.ampEnv.stage != OB_E_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                // Per-voice analogue drift: four not-quite-identical SEM
                // cards, not four copies of one circuit -- see oberheim.h.
                float detuneMul = fastPow2(OB_VOICE_DETUNE[i] / 1200.0f);
                float base = v.currentFreq * bendMul * detuneMul;

                v.osc1.setFreq(base * fastPow2(lfoOscSemis / 12.0f));
                v.osc2.setFreq(base * ob.fineB * fastPow2(lfoOscSemis / 12.0f));

                const float envF = v.filtEnv.process();
                float kbdSemis = ob.kbdAmt * (float)(v.note - 60);
                float filtSemis = kbdSemis + ob.envAmt * envF * 48.0f + lfoFiltSemis;
                float cutoff = safeClamp(
                    ob.cutoffHz * fastPow2((filtSemis + OB_VOICE_FILT[i] * 1200.0f) / 12.0f),
                    20.0f, 18000.0f);
                if (ob.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, ob.resonance);
                else                            v.varFilt.setCutoff(cutoff, ob.resonance, ob.filterType);
            }

            float tA = v.osc1.advance();
            float tB = v.osc2.advance();
            float oscAOut = v.osc1.saw(tA) * ob.sawA + v.osc1.pulse(tA, ob.pwA) * ob.pulseA;
            float oscBOut = v.osc2.saw(tB) * ob.sawB + v.osc2.pulse(tB, ob.pwB) * ob.pulseB;
            float noiseOut = v.noise.white() * ob.noiseLvl;

            float voiceIn = (oscAOut + oscBOut + noiseOut) * 0.5f;
            float filtered;
            if (ob.filterType == VF_STOCK) {
                float lp, bp, hp;
                v.filter.process(voiceIn, &lp, &bp, &hp);
                filtered = semModeMorph(lp, bp, hp, ob.filterMode01);
            } else {
                filtered = v.varFilt.process(voiceIn, ob.filterType, ob.resonance);
            }

            float amp = v.ampEnv.process();
            float velScale = (0.5f + 0.5f * v.velNorm) * OB_VOICE_LEVEL[i];

            mix += filtered * amp * velScale;
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER used to be multiplied straight into the saturator below, so
        // turning the volume up also drove more distortion. OB_MIX_DRIVE is
        // a fixed pre-saturator drive instead, matching every other
        // engine's own fixed-drive fix for the same bug (see
        // testMasterIsLinearPostSaturationGain). MASTER itself is now
        // applied as pure linear gain after the saturator and the whole
        // effect chain.
        const float OB_MIX_DRIVE = 1.5f;
        float out = fastTanh(mix * OB_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, ob.tremDepth, ob.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, ob.dlyFb, ob.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * ob.revMix; r += rr * ob.revMix;
        }
        l *= ob.master; r *= ob.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

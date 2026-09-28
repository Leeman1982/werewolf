#include "obx.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  obx.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum ObxParamId : uint8_t {
    OBX_OSC1_SAW = 0, OBX_OSC1_PULSE,
    OBX_OSC2_SAW, OBX_OSC2_PULSE, OBX_OSC2_FINE,
    OBX_NOISE,
    OBX_LFO_RATE, OBX_LFO_TO_OSC, OBX_LFO_TO_FILTER,
    OBX_XMOD, OBX_SYNC,
    OBX_SH_RATE, OBX_SH_TO_PITCH, OBX_SH_TO_FILTER,
    OBX_FILTER_CUTOFF, OBX_FILTER_TYPE, OBX_FILTER_RES, OBX_FILTER_ENV_AMT, OBX_FILTER_KBD,
    OBX_FENV_A, OBX_FENV_D, OBX_FENV_S, OBX_FENV_R,
    OBX_AENV_A, OBX_AENV_D, OBX_AENV_S, OBX_AENV_R,
    OBX_UNISON, OBX_HOLD,
    OBX_GLIDE_ON, OBX_GLIDE_TIME,
    OBX_MASTER, OBX_BEND_RANGE,

    OBX_TREM_RATE, OBX_TREM_DEPTH, OBX_TREM_MIX,
    OBX_DLY_TIME, OBX_DLY_FB, OBX_DLY_MIX, OBX_REV_SIZE, OBX_REV_MIX,

    OBX_ARP_ENABLE, OBX_ARP_PATTERN, OBX_ARP_RATE, OBX_ARP_GATE,
    OBX_ARP_OCTAVES, OBX_ARP_DIR, OBX_ARP_LATCH, OBX_ARP_SYNC,

    OBX_NUM_PARAMS
};

static const char* const OBX_ONOFF[2]      = { "OFF", "ON" };
static const char* const OBX_KBD_NAMES[3]  = { "OFF", "HALF", "FULL" };
static const char* const OBX_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define OBXROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [OBX_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo OBX_PARAM_INFO[] = {
    OBXROW("O1 SAW",   PK_CONT,   nullptr, 0,    0, 127,  20),
    OBXROW("O1 PULSE", PK_CONT,   nullptr, 0,    0, 127,  21),
    OBXROW("O2 SAW",   PK_CONT,   nullptr, 0,    0, 127,  22),
    OBXROW("O2 PULSE", PK_CONT,   nullptr, 0,    0, 127,  23),
    OBXROW("O2 FINE",  PK_CENTS,  nullptr, 0,   -700, 700, 24),
    OBXROW("NOISE",    PK_CONT,   nullptr, 0,    0, 127,  25),
    OBXROW("LFO RATE", PK_HZ,     nullptr, 0,    0, 20,   26),
    OBXROW("LFO>OSC",  PK_CONT,   nullptr, 0,    0, 127,  27),
    OBXROW("LFO>VCF",  PK_CONT,   nullptr, 0,    0, 127,  28),
    OBXROW("XMOD",     PK_CONT,   nullptr, 0,    0, 127,  29),
    OBXROW("SYNC",     PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    30),
    OBXROW("S&H RATE", PK_HZ,     nullptr, 0,    1, 30,   31),
    OBXROW("S&H>PITCH",PK_CONT,   nullptr, 0,    0, 127,  33),
    OBXROW("S&H>VCF",  PK_CONT,   nullptr, 0,    0, 127,  34),
    OBXROW("CUTOFF",   PK_HZ,     nullptr, 0,   20, 18000,35),
    OBXROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 36),
    OBXROW("RESONANCE",PK_CONT,   nullptr, 0,    0, 127,  37),
    OBXROW("VCF ENV AMT",PK_CONT, nullptr, 0,    0, 127,  39),
    OBXROW("KBD AMT",  PK_ENUM,   OBX_KBD_NAMES, 3,0, 2,  40),
    OBXROW("VCF ATK",  PK_MS,     nullptr, 0,    1, 8000, 41),
    OBXROW("VCF DEC",  PK_MS,     nullptr, 0,    3, 12000,42),
    OBXROW("VCF SUS",  PK_CONT,   nullptr, 0,    0, 127,  43),
    OBXROW("VCF REL",  PK_MS,     nullptr, 0,    3, 12000,44),
    OBXROW("VCA ATK",  PK_MS,     nullptr, 0,    1, 8000, 45),
    OBXROW("VCA DEC",  PK_MS,     nullptr, 0,    3, 12000,46),
    OBXROW("VCA SUS",  PK_CONT,   nullptr, 0,    0, 127,  47),
    OBXROW("VCA REL",  PK_MS,     nullptr, 0,    3, 12000,48),
    OBXROW("UNISON",   PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    49),
    OBXROW("HOLD",     PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    50),
    OBXROW("GLIDE",    PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    51),
    OBXROW("GLIDE TIME",PK_MS,    nullptr, 0,    0, 5000, 52),
    OBXROW("MASTER",   PK_CONT,   nullptr, 0,    0, 127,  53),
    OBXROW("BEND RNG", PK_INT,    nullptr, 0,    1, 24,   54),
    OBXROW("TREM RATE",PK_HZ,     nullptr, 0,    0, 20,   55),
    OBXROW("TREM DEPTH",PK_CONT,  nullptr, 0,    0, 127,  56),
    OBXROW("TREM MIX", PK_CONT,   nullptr, 0,    0, 127,  57),
    OBXROW("DLY TIME", PK_MS,     nullptr, 0,   20, 1200, 58),
    OBXROW("DLY FDBK", PK_CONT,   nullptr, 0,    0, 127,  59),
    OBXROW("DLY MIX",  PK_CONT,   nullptr, 0,    0, 127,  60),
    OBXROW("REV SIZE", PK_CONT,   nullptr, 0,    0, 127,  61),
    OBXROW("REV MIX",  PK_CONT,   nullptr, 0,    0, 127,  62),
    OBXROW("ARP ON",   PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    63),
    OBXROW("ARP PATT", PK_INT,    nullptr, 0,    0, 7,    65),
    OBXROW("ARP RATE", PK_CONT,   nullptr, 0,    0, 127,  66),
    OBXROW("ARP GATE", PK_CONT,   nullptr, 0,    0, 127,  67),
    OBXROW("ARP OCT",  PK_INT,    nullptr, 0,    1, 4,    68),
    OBXROW("ARP DIR",  PK_ENUM,   OBX_ARP_DIR_NAMES,6,0,5,69),
    OBXROW("ARP LATCH",PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    70),
    OBXROW("ARP SYNC", PK_TOGGLE, OBX_ONOFF, 2,  0, 1,    71),
};
static_assert(sizeof(OBX_PARAM_INFO) / sizeof(OBX_PARAM_INFO[0]) == OBX_NUM_PARAMS,
              "OBX_PARAM_INFO must have exactly OBX_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float obxRaw[OBX_NUM_PARAMS];

struct ObxDecoded {
    float osc1Saw, osc1Pulse;
    float osc2Saw, osc2Pulse, osc2Fine;
    float noiseLvl;
    float lfoRateHz, lfoToOsc, lfoToFilter;
    float xmodAmt; bool syncOn;
    float shRateHz, shToPitch, shToFilter;
    float cutoffHz; uint8_t filterType; float resonance, envAmt, kbdAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    bool unison, hold, glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static ObxDecoded obx;

static void obxDecode() {
    obx.osc1Saw = obxRaw[OBX_OSC1_SAW]; obx.osc1Pulse = obxRaw[OBX_OSC1_PULSE];
    obx.osc2Saw = obxRaw[OBX_OSC2_SAW]; obx.osc2Pulse = obxRaw[OBX_OSC2_PULSE];
    obx.osc2Fine = fastPow2(((obxRaw[OBX_OSC2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    obx.noiseLvl = obxRaw[OBX_NOISE];

    obx.lfoRateHz = 0.1f + obxRaw[OBX_LFO_RATE] * 19.9f;
    obx.lfoToOsc = obxRaw[OBX_LFO_TO_OSC];
    obx.lfoToFilter = obxRaw[OBX_LFO_TO_FILTER];

    obx.xmodAmt = obxRaw[OBX_XMOD];
    obx.syncOn  = obxRaw[OBX_SYNC] > 0.5f;

    obx.shRateHz   = 1.0f + obxRaw[OBX_SH_RATE] * 29.0f;
    obx.shToPitch  = obxRaw[OBX_SH_TO_PITCH];
    obx.shToFilter = obxRaw[OBX_SH_TO_FILTER];

    obx.cutoffHz = 20.0f * fastPow2(obxRaw[OBX_FILTER_CUTOFF] * 9.8f);
    obx.filterType = (uint8_t)(obxRaw[OBX_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    obx.resonance = obxRaw[OBX_FILTER_RES];
    obx.envAmt = obxRaw[OBX_FILTER_ENV_AMT];
    { static const float KBD_TAB[3] = { 0.0f, 0.5f, 1.0f };
      int i = (int)(obxRaw[OBX_FILTER_KBD] * 2.999f); if (i < 0) i = 0; else if (i > 2) i = 2;
      obx.kbdAmt = KBD_TAB[i]; }

    obx.fA = 1.0f / (1.0f + obxRaw[OBX_FENV_A] * 800.0f);
    obx.fD = 1.0f / (1.0f + obxRaw[OBX_FENV_D] * 1600.0f);
    obx.fS = obxRaw[OBX_FENV_S];
    obx.fR = 1.0f / (1.0f + obxRaw[OBX_FENV_R] * 1600.0f);
    obx.aA = 1.0f / (1.0f + obxRaw[OBX_AENV_A] * 800.0f);
    obx.aD = 1.0f / (1.0f + obxRaw[OBX_AENV_D] * 1600.0f);
    obx.aS = obxRaw[OBX_AENV_S];
    obx.aR = 1.0f / (1.0f + obxRaw[OBX_AENV_R] * 1600.0f);

    obx.unison = obxRaw[OBX_UNISON] > 0.5f;
    obx.hold   = obxRaw[OBX_HOLD] > 0.5f;
    obx.glideOn = obxRaw[OBX_GLIDE_ON] > 0.5f;
    obx.glideTimeS = 0.005f + obxRaw[OBX_GLIDE_TIME] * 5.0f;
    obx.master = obxRaw[OBX_MASTER];
    obx.bendRangeSemis = 1.0f + obxRaw[OBX_BEND_RANGE] * 23.0f;

    obx.tremRate = obxRaw[OBX_TREM_RATE] * 20.0f;
    obx.tremDepth = obxRaw[OBX_TREM_DEPTH];
    obx.tremMix = obxRaw[OBX_TREM_MIX];
    obx.dlyTimeMs = 20.0f + obxRaw[OBX_DLY_TIME] * 1180.0f;
    obx.dlyFb = obxRaw[OBX_DLY_FB];
    obx.dlyMix = obxRaw[OBX_DLY_MIX];
    obx.revSize = obxRaw[OBX_REV_SIZE];
    obx.revMix = obxRaw[OBX_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) — every PK_ENUM/PK_TOGGLE below that isn't
// option 0 goes through it, same discipline as every other engine's table.
static const uint8_t OBX_INIT[] = {
    100, 0,             // OSC1 saw/pulse
    0, 90, 66,           // OSC2 saw/pulse/fine (a few cents sharp)
    0,                   // noise
    20, 0, 0,             // LFO rate/to-osc/to-filter (off by default)
    0, 0,                // xmod/sync
    10, 0, 0,             // S&H rate/to-pitch/to-filter
    90, 0, 20, 60, EI(2,3), // filter cutoff/type/res/envamt/kbd(FULL)
    15, 55, 90, 30,       // filter env ADSR
    5, 65, 110, 40,       // amp env ADSR
    0, 0,                 // unison/hold
    0, 64,                // glide on/time
    110, 20,              // master/bend
    0,0,0, 0,0,0,0,0,     // output
    0,0,64,80,0,0,0,0,    // arp
};
static_assert(sizeof(OBX_INIT) / sizeof(OBX_INIT[0]) == OBX_NUM_PARAMS,
              "OBX_INIT must have exactly OBX_NUM_PARAMS values");

#define OBX_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta OBX_P_PUNCHBASS[] = { {OBX_OSC1_PULSE,110},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,90},{OBX_OSC2_FINE,64},{OBX_FILTER_CUTOFF,30},{OBX_FILTER_RES,35},{OBX_FILTER_ENV_AMT,100},{OBX_FENV_A,0},{OBX_FENV_D,25},{OBX_FENV_S,0},{OBX_AENV_D,25},{OBX_AENV_S,20} };
static const PresetDelta OBX_P_FATBASS[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,60},{OBX_FILTER_CUTOFF,25},{OBX_FILTER_RES,45},{OBX_FILTER_ENV_AMT,80},{OBX_FENV_D,40} };
static const PresetDelta OBX_P_SYNCBASS[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,90},{OBX_SYNC,EI(1,2)},{OBX_FILTER_CUTOFF,35},{OBX_FILTER_RES,60},{OBX_FILTER_ENV_AMT,90},{OBX_FENV_D,30} };
static const PresetDelta OBX_P_SUBBASS[] = { {OBX_OSC1_SAW,90},{OBX_OSC2_PULSE,0},{OBX_FILTER_CUTOFF,16},{OBX_FILTER_RES,15},{OBX_FILTER_ENV_AMT,40},{OBX_AENV_D,55},{OBX_AENV_S,100} };
static const PresetDelta OBX_P_XMODBASS[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,80},{OBX_XMOD,55},{OBX_FILTER_CUTOFF,30},{OBX_FILTER_RES,50},{OBX_FILTER_ENV_AMT,90},{OBX_FENV_D,35} };

// ── Leads ──
static const PresetDelta OBX_P_XMODLEAD[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,80},{OBX_XMOD,70},{OBX_FILTER_CUTOFF,85},{OBX_FILTER_RES,70},{OBX_FILTER_ENV_AMT,70},{OBX_FENV_A,0},{OBX_FENV_D,50} };
static const PresetDelta OBX_P_SYNCLEAD[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,110},{OBX_OSC2_FINE,110},{OBX_SYNC,EI(1,2)},{OBX_FILTER_CUTOFF,95},{OBX_FILTER_RES,80},{OBX_FILTER_ENV_AMT,60},{OBX_GLIDE_ON,EI(1,2)},{OBX_GLIDE_TIME,10} };
static const PresetDelta OBX_P_SCREAMLEAD[] = { {OBX_OSC1_PULSE,110},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,100},{OBX_OSC2_FINE,90},{OBX_FILTER_CUTOFF,100},{OBX_FILTER_RES,110},{OBX_FILTER_ENV_AMT,100},{OBX_FENV_A,0},{OBX_FENV_D,55} };
static const PresetDelta OBX_P_SOFTLEAD[] = { {OBX_OSC1_SAW,90},{OBX_OSC2_PULSE,70},{OBX_OSC2_FINE,70},{OBX_FILTER_CUTOFF,60},{OBX_FILTER_RES,30},{OBX_FENV_A,15},{OBX_AENV_A,10} };
static const PresetDelta OBX_P_PWLEAD[] = { {OBX_OSC1_PULSE,100},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,80},{OBX_OSC2_FINE,72},{OBX_FILTER_CUTOFF,70},{OBX_FILTER_RES,45} };
static const PresetDelta OBX_P_UNISONLEAD[] = { {OBX_UNISON,EI(1,2)},{OBX_OSC1_SAW,127},{OBX_OSC2_SAW,110},{OBX_OSC2_FINE,95},{OBX_GLIDE_ON,EI(1,2)},{OBX_GLIDE_TIME,15},{OBX_FILTER_RES,90} };

// ── Brass/stabs ──
static const PresetDelta OBX_P_OBXBRASS[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,74},{OBX_FILTER_ENV_AMT,90},{OBX_FENV_A,12},{OBX_FENV_D,55},{OBX_AENV_A,8} };
static const PresetDelta OBX_P_BRASSSTAB[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,70},{OBX_FILTER_CUTOFF,55},{OBX_FILTER_ENV_AMT,95},{OBX_FENV_A,0},{OBX_FENV_D,22},{OBX_FENV_S,35},{OBX_AENV_D,25},{OBX_AENV_S,45} };
static const PresetDelta OBX_P_POLYSTAB[] = { {OBX_OSC1_PULSE,100},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,90},{OBX_OSC2_FINE,90},{OBX_FILTER_ENV_AMT,110},{OBX_FENV_D,25},{OBX_AENV_D,20} };
static const PresetDelta OBX_P_ORCHHIT[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,110},{OBX_OSC2_FINE,90},{OBX_NOISE,20},{OBX_FILTER_CUTOFF,90},{OBX_FILTER_RES,40},{OBX_FILTER_ENV_AMT,80},{OBX_FENV_D,30},{OBX_AENV_D,35},{OBX_AENV_S,60} };
static const PresetDelta OBX_P_HORRORSTAB[] = { {OBX_OSC1_SAW,110},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,120},{OBX_XMOD,40},{OBX_FILTER_CUTOFF,70},{OBX_FILTER_RES,85},{OBX_FILTER_ENV_AMT,100},{OBX_FENV_D,35},{OBX_AENV_D,40},{OBX_AENV_S,30} };

// ── Pads ──
static const PresetDelta OBX_P_STRINGPAD[] = { {OBX_OSC1_SAW,90},{OBX_OSC2_SAW,80},{OBX_OSC2_FINE,72},{OBX_FILTER_CUTOFF,55},{OBX_FENV_A,60},{OBX_FENV_D,90},{OBX_AENV_A,70},{OBX_AENV_D,100},{OBX_AENV_S,95},{OBX_AENV_R,90} };
static const PresetDelta OBX_P_WARMPAD[] = { {OBX_OSC1_SAW,80},{OBX_OSC2_PULSE,70},{OBX_OSC2_FINE,68},{OBX_FILTER_CUTOFF,45},{OBX_FILTER_RES,15},{OBX_FENV_A,80},{OBX_AENV_A,85},{OBX_AENV_D,55},{OBX_AENV_S,105} };
static const PresetDelta OBX_P_SHPAD[] = { {OBX_OSC1_SAW,85},{OBX_OSC2_SAW,75},{OBX_OSC2_FINE,70},{OBX_SH_RATE,20},{OBX_SH_TO_FILTER,45},{OBX_FILTER_CUTOFF,60},{OBX_FILTER_RES,30},{OBX_FENV_A,50},{OBX_AENV_A,90},{OBX_AENV_S,100} };
static const PresetDelta OBX_P_EVOLVEPAD[] = { {OBX_OSC1_PULSE,80},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,75},{OBX_OSC2_FINE,73},{OBX_SH_RATE,5},{OBX_SH_TO_FILTER,60},{OBX_FILTER_CUTOFF,40},{OBX_FENV_A,110},{OBX_AENV_A,110},{OBX_AENV_S,105} };

// ── Bells/plucks ──
static const PresetDelta OBX_P_BELLPLUCK[] = { {OBX_OSC1_PULSE,90},{OBX_OSC1_SAW,0},{OBX_OSC2_PULSE,80},{OBX_OSC2_FINE,110},{OBX_XMOD,30},{OBX_FILTER_CUTOFF,90},{OBX_FILTER_ENV_AMT,60},{OBX_FENV_D,45},{OBX_AENV_D,50},{OBX_AENV_S,0} };
static const PresetDelta OBX_P_MARIMBA[] = { {OBX_OSC1_PULSE,90},{OBX_OSC1_SAW,0},{OBX_FILTER_CUTOFF,55},{OBX_FILTER_ENV_AMT,50},{OBX_FENV_D,18},{OBX_AENV_D,16},{OBX_AENV_S,0} };
static const PresetDelta OBX_P_GLASSPLUCK[] = { {OBX_OSC1_SAW,80},{OBX_OSC2_SAW,70},{OBX_OSC2_FINE,120},{OBX_XMOD,60},{OBX_FILTER_CUTOFF,95},{OBX_FILTER_RES,50},{OBX_FENV_D,30},{OBX_AENV_D,35},{OBX_AENV_S,0} };

// ── HOLD / "unison chord" showcase (see obx.h file banner point 4) ──
static const PresetDelta OBX_P_CHORDSTAB[] = { {OBX_HOLD,EI(1,2)},{OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,70},{OBX_FILTER_CUTOFF,65},{OBX_FILTER_ENV_AMT,90},{OBX_FENV_D,25},{OBX_AENV_D,25},{OBX_AENV_S,40} };
static const PresetDelta OBX_P_ONEFINGERBRASS[] = { {OBX_HOLD,EI(1,2)},{OBX_OSC1_SAW,110},{OBX_OSC2_SAW,100},{OBX_OSC2_FINE,75},{OBX_FILTER_CUTOFF,60},{OBX_FILTER_ENV_AMT,95},{OBX_FENV_A,10},{OBX_FENV_D,45},{OBX_AENV_A,8},{OBX_AENV_D,35},{OBX_AENV_S,70} };
static const PresetDelta OBX_P_HAUNTEDCHORD[] = { {OBX_HOLD,EI(1,2)},{OBX_OSC1_SAW,100},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,130},{OBX_XMOD,25},{OBX_FILTER_CUTOFF,45},{OBX_FILTER_RES,35},{OBX_FENV_A,40},{OBX_AENV_A,50},{OBX_AENV_D,70},{OBX_AENV_S,90},{OBX_REV_MIX,40} };

// ── SFX ──
static const PresetDelta OBX_P_SHSEQFX[] = { {OBX_OSC1_PULSE,90},{OBX_OSC1_SAW,0},{OBX_SH_RATE,90},{OBX_SH_TO_PITCH,70},{OBX_FILTER_CUTOFF,70},{OBX_FILTER_RES,40} };
static const PresetDelta OBX_P_SIRENFX[] = { {OBX_OSC1_SAW,100},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,127},{OBX_XMOD,90},{OBX_FILTER_CUTOFF,80},{OBX_FILTER_RES,60},{OBX_AENV_A,20},{OBX_AENV_S,127} };
static const PresetDelta OBX_P_NOISESWEEP[] = { {OBX_OSC1_SAW,0},{OBX_OSC2_SAW,0},{OBX_NOISE,110},{OBX_FILTER_CUTOFF,25},{OBX_FILTER_RES,90},{OBX_FILTER_ENV_AMT,120},{OBX_FENV_A,0},{OBX_FENV_D,127},{OBX_AENV_A,0},{OBX_AENV_D,127},{OBX_AENV_S,127} };

// ── Arp ──
static const PresetDelta OBX_P_ARPCLASSIC[] = { {OBX_OSC1_PULSE,90},{OBX_OSC1_SAW,0},{OBX_FILTER_CUTOFF,60},{OBX_FILTER_RES,40},{OBX_FILTER_ENV_AMT,60},{OBX_FENV_D,25},{OBX_AENV_D,20},{OBX_AENV_S,30},{OBX_ARP_ENABLE,EI(1,2)},{OBX_ARP_RATE,80},{OBX_ARP_GATE,70} };
static const PresetDelta OBX_P_ARPXMOD[] = { {OBX_OSC1_SAW,100},{OBX_OSC2_SAW,90},{OBX_OSC2_FINE,80},{OBX_XMOD,45},{OBX_FILTER_CUTOFF,65},{OBX_ARP_ENABLE,EI(1,2)},{OBX_ARP_RATE,90},{OBX_ARP_GATE,80},{OBX_ARP_OCTAVES,48},{OBX_ARP_DIR,EI(2,6)} };

static const Preset OBX_PRESETS[] = {
    OBX_PRESET("PUNCH BASS",   OBX_P_PUNCHBASS),
    OBX_PRESET("FAT BASS",     OBX_P_FATBASS),
    OBX_PRESET("SYNC BASS",    OBX_P_SYNCBASS),
    OBX_PRESET("SUB BASS",     OBX_P_SUBBASS),
    OBX_PRESET("XMOD BASS",    OBX_P_XMODBASS),
    OBX_PRESET("XMOD LEAD",    OBX_P_XMODLEAD),
    OBX_PRESET("SYNC LEAD",    OBX_P_SYNCLEAD),
    OBX_PRESET("SCREAM LEAD",  OBX_P_SCREAMLEAD),
    OBX_PRESET("SOFT LEAD",    OBX_P_SOFTLEAD),
    OBX_PRESET("PW LEAD",      OBX_P_PWLEAD),
    OBX_PRESET("UNISON LEAD",  OBX_P_UNISONLEAD),
    OBX_PRESET("OBX BRASS",    OBX_P_OBXBRASS),
    OBX_PRESET("BRASS STAB",   OBX_P_BRASSSTAB),
    OBX_PRESET("POLY STAB",    OBX_P_POLYSTAB),
    OBX_PRESET("ORCH HIT",     OBX_P_ORCHHIT),
    OBX_PRESET("HORROR STAB",  OBX_P_HORRORSTAB),
    OBX_PRESET("STRING PAD",   OBX_P_STRINGPAD),
    OBX_PRESET("WARM PAD",     OBX_P_WARMPAD),
    OBX_PRESET("S&H PAD",      OBX_P_SHPAD),
    OBX_PRESET("EVOLVE PAD",   OBX_P_EVOLVEPAD),
    OBX_PRESET("BELL PLUCK",   OBX_P_BELLPLUCK),
    OBX_PRESET("MARIMBA",      OBX_P_MARIMBA),
    OBX_PRESET("GLASS PLUCK",  OBX_P_GLASSPLUCK),
    OBX_PRESET("CHORD STAB",   OBX_P_CHORDSTAB),
    OBX_PRESET("1-FINGER BRASS",OBX_P_ONEFINGERBRASS),
    OBX_PRESET("HAUNTED CHORD",OBX_P_HAUNTEDCHORD),
    OBX_PRESET("S&H SEQ FX",   OBX_P_SHSEQFX),
    OBX_PRESET("SIREN FX",     OBX_P_SIRENFX),
    OBX_PRESET("NOISE SWEEP",  OBX_P_NOISESWEEP),
    OBX_PRESET("ARP CLASSIC",  OBX_P_ARPCLASSIC),
    OBX_PRESET("ARP XMOD",     OBX_P_ARPXMOD),
};
#define OBX_NUM_PRESETS (sizeof(OBX_PRESETS) / sizeof(OBX_PRESETS[0]))

static void obxApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < OBX_NUM_PARAMS; i++) obxRaw[i] = (float)OBX_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) obxRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    obxDecode();
}

static const EnginePageDef OBX_PAGES[] = {
    { "OSC 1/2",    OBX_OSC1_SAW,     6 },   // ...through NOISE
    { "LFO/MOD",    OBX_LFO_RATE,     5 },   // LFO rate/to-osc/to-filter, xmod, sync
    { "S&H",        OBX_SH_RATE,      3 },
    { "FILTER",     OBX_FILTER_CUTOFF,5 },
    { "ENVELOPES",  OBX_FENV_A,       8 },
    { "VOICING",    OBX_UNISON,       6 },   // ...through MASTER/BEND RNG
    { "OUTPUT",     OBX_TREM_RATE,    8 },
    { "ARP",        OBX_ARP_ENABLE,   8 },
};
#define OBX_NUM_PAGES (sizeof(OBX_PAGES) / sizeof(OBX_PAGES[0]))

uint8_t obxMasterParamIdE() { return OBX_MASTER; }
uint8_t obxFilterCutoffParamIdE()    { return OBX_FILTER_CUTOFF; }
uint8_t obxFilterResonanceParamIdE() { return OBX_FILTER_RES; }
uint8_t obxNumPagesE() { return (uint8_t)OBX_NUM_PAGES; }
const EnginePageDef* obxPageE(uint8_t idx) { return (idx < OBX_NUM_PAGES) ? &OBX_PAGES[idx] : &OBX_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static ObxVoice obxv[OBX_VOICES];
extern VoiceAlloc va;

// Plain UNISON's own held-note stack — identical technique to Prophet-5's
// (see prophet5.cpp): fans one note across every voice, which is
// monophonic in exactly the way a MONO_LAST keyboard-style track is, so a
// release has to know whether the note letting go is the one actually
// sounding or just one of several still held underneath it.
#define OBX_UNISON_MAX_HELD 8
static int     obxUnisonHeld[OBX_UNISON_MAX_HELD];
static uint8_t obxUnisonHeldVel[OBX_UNISON_MAX_HELD];
static uint8_t obxUnisonHeldCount = 0;
static void obxUnisonReset() { obxUnisonHeldCount = 0; }
static void obxUnisonPush(int note, uint8_t vel) {
    for (uint8_t i = 0; i < obxUnisonHeldCount; i++)
        if (obxUnisonHeld[i] == note) { obxUnisonHeldVel[i] = vel; return; }
    if (obxUnisonHeldCount < OBX_UNISON_MAX_HELD) {
        obxUnisonHeld[obxUnisonHeldCount] = note;
        obxUnisonHeldVel[obxUnisonHeldCount] = vel;
        obxUnisonHeldCount++;
    }
}
static bool obxUnisonPop(int note) {
    for (uint8_t i = 0; i < obxUnisonHeldCount; i++) {
        if (obxUnisonHeld[i] == note) {
            for (uint8_t j = i; j < obxUnisonHeldCount - 1; j++) {
                obxUnisonHeld[j] = obxUnisonHeld[j + 1];
                obxUnisonHeldVel[j] = obxUnisonHeldVel[j + 1];
            }
            obxUnisonHeldCount--;
            return true;
        }
    }
    return false;
}

// HOLD / "unison chord" state — see obx.h's file banner, point 4. Built
// entirely at the engine level, same reasoning as Prophet-5's UNISON: the
// shared voice allocator only knows "one note -> one voice", and both of
// these performance features fundamentally aren't that.
static int     obxHeldNotes[OBX_VOICES];
static uint8_t obxHeldVel[OBX_VOICES];
static uint8_t obxHeldCount = 0;
static int     obxPendingOffsets[OBX_VOICES];
static uint8_t obxPendingVel[OBX_VOICES];
static uint8_t obxPendingSize = 0;
static int     obxChordOffsets[OBX_VOICES];
static uint8_t obxChordVel[OBX_VOICES];
static uint8_t obxChordSize = 0;
static bool    obxChordArmed = false;
static int     obxChordTriggerNote = -1;

static void obxHoldReset() {
    obxHeldCount = 0; obxPendingSize = 0; obxChordSize = 0;
    obxChordArmed = false; obxChordTriggerNote = -1;
}
static void obxHeldPush(int note, uint8_t vel) {
    for (uint8_t i = 0; i < obxHeldCount; i++) if (obxHeldNotes[i] == note) { obxHeldVel[i] = vel; return; }
    if (obxHeldCount < OBX_VOICES) { obxHeldNotes[obxHeldCount] = note; obxHeldVel[obxHeldCount] = vel; obxHeldCount++; }
    if (obxHeldCount >= 2) {
        // Live snapshot of whatever's down right now, refreshed on every
        // note added — becomes the memorized shape once the whole group
        // lets go (see obxNoteOffE).
        int root = obxHeldNotes[0];
        for (uint8_t i = 1; i < obxHeldCount; i++) if (obxHeldNotes[i] < root) root = obxHeldNotes[i];
        for (uint8_t i = 0; i < obxHeldCount; i++) { obxPendingOffsets[i] = obxHeldNotes[i] - root; obxPendingVel[i] = obxHeldVel[i]; }
        obxPendingSize = obxHeldCount;
    }
}
static void obxHeldPop(int note) {
    for (uint8_t i = 0; i < obxHeldCount; i++) {
        if (obxHeldNotes[i] == note) {
            for (uint8_t j = i; j < obxHeldCount - 1; j++) { obxHeldNotes[j] = obxHeldNotes[j + 1]; obxHeldVel[j] = obxHeldVel[j + 1]; }
            obxHeldCount--;
            return;
        }
    }
}

void obxEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void obxEngineActivate() {
    for (int i = 0; i < OBX_VOICES; i++) obxv[i] = ObxVoice();
    obxUnisonReset();
    obxHoldReset();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void obxParamsInitE() { obxApplyDeltas(nullptr, 0); }
void obxSetParameterE(uint8_t id, float v) { if (id < OBX_NUM_PARAMS) { obxRaw[id] = v; obxDecode(); } }
float obxGetParameterE(uint8_t id) { return (id < OBX_NUM_PARAMS) ? obxRaw[id] : 0.0f; }
void obxFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= OBX_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = OBX_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(obxRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(obxRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool obxParamIsLockableE(uint8_t id) { return id < OBX_NUM_PARAMS; }
uint8_t obxNumParamsE() { return OBX_NUM_PARAMS; }
static const ParamInfo OBX_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* obxParamInfoE(uint8_t id) {
    return (id < OBX_NUM_PARAMS) ? &OBX_PARAM_INFO[id] : &OBX_INVALID_PARAM;
}
uint8_t obxNumPresetsE() { return (uint8_t)OBX_NUM_PRESETS; }
const char* obxPresetNameE(uint8_t idx) { return OBX_PRESETS[idx].name; }
void obxLoadPresetE(uint8_t idx) { obxApplyDeltas(OBX_PRESETS[idx].deltas, OBX_PRESETS[idx].n); }
void obxLoadInitPatchE() { obxApplyDeltas(nullptr, 0); }
void obxSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < OBX_NUM_PARAMS; i++) out[i] = (uint8_t)(obxRaw[i] * 255.0f + 0.5f);
}
void obxRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < OBX_NUM_PARAMS; i++) obxRaw[i] = (float)in[i] * (1.0f / 255.0f);
    obxDecode();
}

uint8_t obxNumVoicesE() { return OBX_VOICES; }
void    obxVoiceInitE() { obxEngineActivate(); }

static inline void obxTriggerVoice(ObxVoice& v, int note, uint8_t vel, bool trigEnv) {
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (obx.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (obx.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(obx.fA, obx.fD, obx.fS, obx.fR);
        v.ampEnv.trigger(obx.aA, obx.aD, obx.aS, obx.aR);
    }
}
static inline void obxReleaseVoice(ObxVoice& v) {
    v.gate = false; v.filtEnv.release(); v.ampEnv.release();
}

void obxNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (obx.hold) {
        if (obxChordArmed && obxHeldCount == 0) {
            // Nothing else physically down and a shape is memorized: this
            // key transposes and replays it — see obx.h file banner, point 4.
            obxChordTriggerNote = note;
            obxHeldPush(note, vel);
            for (uint8_t i = 0; i < obxChordSize && i < OBX_VOICES; i++)
                obxTriggerVoice(obxv[i], note + obxChordOffsets[i], obxChordVel[i], trigEnv);
            return;
        }
        // Live shape-capture: a fresh note joining/starting a group
        // supersedes whatever was memorized before.
        obxChordArmed = false;
        obxHeldPush(note, vel);
        if (voiceIdx < OBX_VOICES) obxTriggerVoice(obxv[voiceIdx], note, vel, trigEnv);
        return;
    }
    if (obx.unison) {
        obxUnisonPush(note, vel);
        for (int i = 0; i < OBX_VOICES; i++) obxTriggerVoice(obxv[i], note, vel, trigEnv);
        return;
    }
    if (voiceIdx < OBX_VOICES) obxTriggerVoice(obxv[voiceIdx], note, vel, trigEnv);
}

void obxNoteOffE(uint8_t voiceIdx, int note) {
    if (obx.hold) {
        if (obxChordTriggerNote == note && note >= 0) {
            for (uint8_t i = 0; i < obxChordSize && i < OBX_VOICES; i++) obxReleaseVoice(obxv[i]);
            obxChordTriggerNote = -1;
            obxHeldPop(note);
            return;
        }
        if (note >= 0) obxHeldPop(note); else obxHeldCount = 0;
        if (obxHeldCount == 0 && obxPendingSize >= 2) {
            // The live-held group just fully released: memorize its shape.
            for (uint8_t i = 0; i < obxPendingSize; i++) { obxChordOffsets[i] = obxPendingOffsets[i]; obxChordVel[i] = obxPendingVel[i]; }
            obxChordSize = obxPendingSize;
            obxChordArmed = true;
            obxPendingSize = 0;
        }
        if (voiceIdx < OBX_VOICES) obxReleaseVoice(obxv[voiceIdx]);
        return;
    }
    if (obx.unison) {
        if (note < 0) obxUnisonReset(); else obxUnisonPop(note);
        if (obxUnisonHeldCount == 0) {
            for (int i = 0; i < OBX_VOICES; i++) obxReleaseVoice(obxv[i]);
            return;
        }
        const uint8_t top = obxUnisonHeldCount - 1;
        for (int i = 0; i < OBX_VOICES; i++)
            obxTriggerVoice(obxv[i], obxUnisonHeld[top], obxUnisonHeldVel[top], true);
        return;
    }
    if (voiceIdx >= OBX_VOICES) return;
    obxReleaseVoice(obxv[voiceIdx]);
}
void obxAllOffE() {
    obxUnisonReset(); obxHoldReset();
    for (int i = 0; i < OBX_VOICES; i++) obxReleaseVoice(obxv[i]);
}
void obxSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < OBX_VOICES) obxv[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int obxRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * obx.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = obx.tremMix > 0.004f;
    const bool fxDelay  = obx.dlyMix  > 0.004f;
    const bool fxReverb = obx.revMix  > 0.004f;
    g_fxChorus.setRate(obx.tremRate);
    g_fxReverb.setRoom(obx.revSize, 0.4f);
    g_fxDelay.setTimeMs(obx.dlyTimeMs, false);

    // Shared S&H clock; each voice draws from its OWN noise generator on
    // every wrap, which is what makes this "polyphonic" S&H rather than one
    // shared random value copied to every voice — see obx.h file banner,
    // point 3.
    static uint32_t shPhaseClock = 0;
    uint32_t shInc = freqToPhaseInc(obx.shRateHz);

    // The real OB-X's own "enhanced LFO" — one shared sine source routed to
    // both oscillator pitch (vibrato) and filter cutoff, wheel-scaled like
    // every other engine's own Wheel-Mod convention.
    static uint32_t lfoPhase = 0;

    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        uint32_t prevSh = shPhaseClock;
        shPhaseClock += shInc;
        const bool shWrap = (shPhaseClock < prevSh);

        lfoPhase += (uint32_t)(obx.lfoRateHz * PHASE_PER_HZ);
        const float lfo = sineFromPhase(lfoPhase);
        const float lfoPitchSemis = lfo * obx.lfoToOsc * modWheel * 2.0f;
        const float lfoFiltSemis  = lfo * obx.lfoToFilter * modWheel * 24.0f;

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < OBX_VOICES; i++) {
            ObxVoice& v = obxv[i];
            const bool alive = v.ampEnv.stage != ADSR_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (shWrap) v.shValue = v.noise.white();

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                float driftMul = fastPow2(OBX_VOICE_DETUNE[i] / 1200.0f);
                float shPitchSemis = obx.shToPitch * v.shValue * 7.0f;
                float base = v.currentFreq * bendMul * driftMul * fastPow2((shPitchSemis + lfoPitchSemis) / 12.0f);

                v.osc1.setFreq(base);
                v.osc2.setFreq(base * obx.osc2Fine);

                const float envF = v.filtEnv.process();
                float kbdSemis = obx.kbdAmt * (float)(v.note - 60);
                float shFiltOct = obx.shToFilter * v.shValue * 3.0f;
                float cutoff = obx.cutoffHz * fastPow2(OBX_VOICE_FILT[i] +
                    (kbdSemis + lfoFiltSemis) / 12.0f + obx.envAmt * envF * 4.0f + shFiltOct);
                if (cutoff < 20.0f) cutoff = 20.0f; else if (cutoff > 18000.0f) cutoff = 18000.0f;
                if (obx.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, obx.resonance);
                else                             v.varFilt.setCutoff(cutoff, obx.resonance, obx.filterType);
            }

            // CROSS MOD: OSC2 into OSC1's frequency at audio rate — the
            // original OB-X's own panel feature (see file banner point 2).
            bool wrapped2 = false;
            float t2 = v.osc2.advance(&wrapped2);
            float o2raw = v.osc2.saw(t2) * obx.osc2Saw + v.osc2.pulse(t2, 0.5f) * obx.osc2Pulse;
            if (obx.xmodAmt > 0.001f) {
                float fmHz = v.osc1.baseHz * fastPow2(o2raw * obx.xmodAmt * (3.0f / 12.0f));
                v.osc1.setFmHz(fmHz);
            }
            if (obx.syncOn && wrapped2) v.osc1.resetPhase();
            float t1 = v.osc1.advance();
            float o1raw = v.osc1.saw(t1) * obx.osc1Saw + v.osc1.pulse(t1, 0.5f) * obx.osc1Pulse;

            float noiseOut = v.noise.white() * obx.noiseLvl;
            float voiceIn = (o1raw + o2raw + noiseOut) * 0.5f;

            float filtered;
            if (obx.filterType == VF_STOCK) {
                float lp, bp, hp;
                v.filter.process(voiceIn, &lp, &bp, &hp);
                filtered = lp;
            } else {
                filtered = v.varFilt.process(voiceIn, obx.filterType, obx.resonance);
            }
            float amp = v.ampEnv.process();
            float levelTrim = OBX_VOICE_LEVEL[i];

            mix += filtered * amp * levelTrim;
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER is pure linear gain after the saturator, not baked into it
        // — same discipline every engine in this codebase follows. OBX_MIX_
        // DRIVE is a reasoned estimate sized the same way as this codebase's
        // other from-scratch engines (Jupiter-8/System 100/Moog Modular):
        // no physical unit to probe against, so it's set to keep an 8-voice
        // 2-oscillator chord in the same moderate tanh-knee range those did,
        // not measured from a real OB-X.
        const float OBX_MIX_DRIVE = 1.7f;
        float out = fastTanh(mix * OBX_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, obx.tremDepth, obx.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, obx.dlyFb, obx.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * obx.revMix; r += rr * obx.revMix;
        }
        l *= obx.master; r *= obx.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

// Per-voice fixed offsets — see obx.h file banner point 5: Auto-Tune keeps
// pitch close (much smaller spread than the hand-tuned Four-Voice/SEM
// engine's OB_VOICE_DETUNE), but filter/level trim is untouched by it and
// stays comparably spread.
const float OBX_VOICE_DETUNE[OBX_VOICES] = {
    0.0f, -0.6f, 0.4f, -0.3f, 0.7f, -0.5f, 0.2f, -0.4f
};
const float OBX_VOICE_FILT[OBX_VOICES] = {
    0.0f, 0.020f, -0.015f, 0.028f, -0.022f, 0.011f, -0.030f, 0.017f
};
const float OBX_VOICE_LEVEL[OBX_VOICES] = {
    1.000f, 0.978f, 1.021f, 0.989f, 1.013f, 0.971f, 1.026f, 0.994f
};

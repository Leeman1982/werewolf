#include "minimoog.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  minimoog.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum MMParamId : uint8_t {
    MM_OSC1_WAVE = 0, MM_OSC1_OCT, MM_OSC1_LEVEL,
    MM_OSC2_WAVE, MM_OSC2_OCT, MM_OSC2_FINE, MM_OSC2_LEVEL,
    MM_OSC3_WAVE, MM_OSC3_OCT, MM_OSC3_FINE, MM_OSC3_LEVEL, MM_OSC3_ASLFO,
    MM_NOISE_LEVEL, MM_NOISE_COLOR,
    MM_MOD_MIX, MM_MOD_TO_FILTER, MM_MOD_TO_OSC,
    MM_GLIDE_ON, MM_GLIDE_TIME,
    MM_FILTER_CUTOFF, MM_FILTER_TYPE, MM_FILTER_EMPHASIS, MM_FILTER_KBD_TRACK, MM_FILTER_ENV_AMT,
    MM_FILTER_A, MM_FILTER_D, MM_FILTER_S,
    MM_AMP_A, MM_AMP_D, MM_AMP_S,
    MM_MASTER, MM_BEND_RANGE,

    MM_TREM_RATE, MM_TREM_DEPTH, MM_TREM_MIX,
    MM_DLY_TIME, MM_DLY_FB, MM_DLY_MIX, MM_REV_SIZE, MM_REV_MIX,

    MM_ARP_ENABLE, MM_ARP_PATTERN, MM_ARP_RATE, MM_ARP_GATE,
    MM_ARP_OCTAVES, MM_ARP_DIR, MM_ARP_LATCH, MM_ARP_SYNC,

    MM_NUM_PARAMS
};

static const char* const MM_WAVE_NAMES[6]   = { "TRI", "TRISAW", "SAW", "SQ NARROW", "PULSE", "PULSE WIDE" };
// Osc 3's own panel legend names position 2 "REV SAW", not "TRISAW" — see
// MMOsc::isOsc3 in minimoog.h for what it actually renders as.
static const char* const MM_OSC3_WAVE_NAMES[6] = { "TRI", "REV SAW", "SAW", "SQ NARROW", "PULSE", "PULSE WIDE" };
static const char* const MM_OCT_NAMES[5]    = { "32'", "16'", "8'", "4'", "2'" };
static const char* const MM_KBD_NAMES[4]    = { "OFF", "1/3", "2/3", "FULL" };
static const char* const MM_ONOFF[2]        = { "OFF", "ON" };
static const char* const MM_NOISE_NAMES[2]  = { "WHITE", "PINK" };
static const char* const MM_ARP_DIR_NAMES[6]= { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define MMROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [MM_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo MM_PARAM_INFO[] = {
    MMROW("O1 WAVE",  PK_ENUM,   MM_WAVE_NAMES, 6, 0, 5,   20),
    MMROW("O1 OCT",   PK_ENUM,   MM_OCT_NAMES,  5, 0, 4,   21),
    MMROW("O1 LEVEL", PK_CONT,   nullptr, 0,    0, 127,    22),
    MMROW("O2 WAVE",  PK_ENUM,   MM_WAVE_NAMES, 6, 0, 5,   23),
    MMROW("O2 OCT",   PK_ENUM,   MM_OCT_NAMES,  5, 0, 4,   24),
    MMROW("O2 FINE",  PK_CENTS,  nullptr, 0,   -700, 700,  25),
    MMROW("O2 LEVEL", PK_CONT,   nullptr, 0,    0, 127,    26),
    MMROW("O3 WAVE",  PK_ENUM,   MM_WAVE_NAMES, 6, 0, 5,   27),
    MMROW("O3 OCT",   PK_ENUM,   MM_OCT_NAMES,  5, 0, 4,   28),
    MMROW("O3 FINE",  PK_CENTS,  nullptr, 0,   -700, 700,  29),
    MMROW("O3 LEVEL", PK_CONT,   nullptr, 0,    0, 127,    30),
    MMROW("O3 AS LFO",PK_TOGGLE, MM_ONOFF, 2,   0, 1,      31),
    MMROW("NOISE LVL",PK_CONT,   nullptr, 0,    0, 127,    32),
    MMROW("NOISE CLR",PK_TOGGLE, MM_NOISE_NAMES,2,0, 1,    33),
    MMROW("MOD MIX",  PK_CONT,   nullptr, 0,    0, 127,    34),
    MMROW("MOD->VCF", PK_TOGGLE, MM_ONOFF, 2,   0, 1,      35),
    MMROW("MOD->VCO", PK_TOGGLE, MM_ONOFF, 2,   0, 1,      36),
    MMROW("GLIDE",    PK_TOGGLE, MM_ONOFF, 2,   0, 1,      37),
    MMROW("GLIDE TIME",PK_MS,    nullptr, 0,    0, 5000,   38),
    MMROW("CUTOFF",   PK_HZ,     nullptr, 0,   20, 18000,  39),
    MMROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 67),
    MMROW("EMPHASIS", PK_CONT,   nullptr, 0,    0, 127,    40),
    MMROW("KBD TRACK",PK_ENUM,   MM_KBD_NAMES, 4,0, 3,     41),
    MMROW("VCF AMT",  PK_CONT,   nullptr, 0,    0, 127,    42),
    MMROW("VCF ATK",  PK_MS,     nullptr, 0,    1, 8000,   43),
    MMROW("VCF DEC",  PK_MS,     nullptr, 0,    3, 12000,  44),
    MMROW("VCF SUS",  PK_CONT,   nullptr, 0,    0, 127,    45),
    MMROW("VCA ATK",  PK_MS,     nullptr, 0,    1, 8000,   46),
    MMROW("VCA DEC",  PK_MS,     nullptr, 0,    3, 12000,  47),
    MMROW("VCA SUS",  PK_CONT,   nullptr, 0,    0, 127,    48),
    MMROW("MASTER",   PK_CONT,   nullptr, 0,    0, 127,    49),
    MMROW("BEND RNG", PK_INT,    nullptr, 0,    1, 24,      50),
    MMROW("TREM RATE",PK_HZ,     nullptr, 0,    0, 20,      51),
    MMROW("TREM DEPTH",PK_CONT,  nullptr, 0,    0, 127,    52),
    MMROW("TREM MIX", PK_CONT,   nullptr, 0,    0, 127,    53),
    MMROW("DLY TIME", PK_MS,     nullptr, 0,   20, 1200,   54),
    MMROW("DLY FDBK", PK_CONT,   nullptr, 0,    0, 127,    55),
    MMROW("DLY MIX",  PK_CONT,   nullptr, 0,    0, 127,    56),
    MMROW("REV SIZE", PK_CONT,   nullptr, 0,    0, 127,    57),
    MMROW("REV MIX",  PK_CONT,   nullptr, 0,    0, 127,    58),
    MMROW("ARP ON",   PK_TOGGLE, MM_ONOFF, 2,   0, 1,      59),
    MMROW("ARP PATT", PK_INT,    nullptr, 0,    0, 7,      60),
    MMROW("ARP RATE", PK_CONT,   nullptr, 0,    0, 127,    61),
    MMROW("ARP GATE", PK_CONT,   nullptr, 0,    0, 127,    62),
    MMROW("ARP OCT",  PK_INT,    nullptr, 0,    1, 4,      63),
    MMROW("ARP DIR",  PK_ENUM,   MM_ARP_DIR_NAMES,6,0,5,   64),
    MMROW("ARP LATCH",PK_TOGGLE, MM_ONOFF, 2,   0, 1,      65),
    MMROW("ARP SYNC", PK_TOGGLE, MM_ONOFF, 2,   0, 1,      66),
};
static_assert(sizeof(MM_PARAM_INFO) / sizeof(MM_PARAM_INFO[0]) == MM_NUM_PARAMS,
              "MM_PARAM_INFO must have exactly MM_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float mmRaw[MM_NUM_PARAMS];

struct MMDecoded {
    uint8_t o1Wave, o2Wave, o3Wave;
    float   o1OctMul, o2OctMul, o3OctMul;
    float   o2FineRatio, o3FineRatio;
    float   o1Level, o2Level, o3Level;
    bool    o3AsLfo;
    float   noiseLevel; bool noisePink;
    float   modMix; bool modToFilter, modToOsc;
    bool    glideOn; float glideTimeS;
    float   cutoffHz; uint8_t filterType; float emphasis, kbdTrackAmt, envAmt;
    float   fA, fD, fS, aA, aD, aS;
    float   master; float bendRangeSemis;
    float   tremRate, tremDepth, tremMix;
    float   dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static MMDecoded mm;

static inline float octMulFromEnum(float v) {
    static const float TAB[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int i = (int)(v * 4.999f); if (i < 0) i = 0; else if (i > 4) i = 4;
    return TAB[i];
}

static void mmDecode() {
    mm.o1Wave = (uint8_t)(mmRaw[MM_OSC1_WAVE] * 5.999f);
    mm.o1OctMul = octMulFromEnum(mmRaw[MM_OSC1_OCT]);
    mm.o1Level = mmRaw[MM_OSC1_LEVEL];

    mm.o2Wave = (uint8_t)(mmRaw[MM_OSC2_WAVE] * 5.999f);
    mm.o2OctMul = octMulFromEnum(mmRaw[MM_OSC2_OCT]);
    mm.o2FineRatio = fastPow2(((mmRaw[MM_OSC2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    mm.o2Level = mmRaw[MM_OSC2_LEVEL];

    mm.o3Wave = (uint8_t)(mmRaw[MM_OSC3_WAVE] * 5.999f);
    mm.o3OctMul = octMulFromEnum(mmRaw[MM_OSC3_OCT]);
    mm.o3FineRatio = fastPow2(((mmRaw[MM_OSC3_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    mm.o3Level = mmRaw[MM_OSC3_LEVEL];
    mm.o3AsLfo = mmRaw[MM_OSC3_ASLFO] > 0.5f;

    mm.noiseLevel = mmRaw[MM_NOISE_LEVEL];
    mm.noisePink  = mmRaw[MM_NOISE_COLOR] > 0.5f;

    mm.modMix = mmRaw[MM_MOD_MIX];
    mm.modToFilter = mmRaw[MM_MOD_TO_FILTER] > 0.5f;
    mm.modToOsc    = mmRaw[MM_MOD_TO_OSC] > 0.5f;

    mm.glideOn = mmRaw[MM_GLIDE_ON] > 0.5f;
    mm.glideTimeS = 0.005f + mmRaw[MM_GLIDE_TIME] * 5.0f;

    mm.cutoffHz = 20.0f * fastPow2(mmRaw[MM_FILTER_CUTOFF] * 9.8f);   // 20 Hz .. ~18 kHz
    mm.filterType = (uint8_t)(mmRaw[MM_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    mm.emphasis = mmRaw[MM_FILTER_EMPHASIS];
    static const float KBD_TAB[4] = { 0.0f, 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
    { int i = (int)(mmRaw[MM_FILTER_KBD_TRACK] * 3.999f); if (i < 0) i = 0; else if (i > 3) i = 3;
      mm.kbdTrackAmt = KBD_TAB[i]; }
    mm.envAmt = mmRaw[MM_FILTER_ENV_AMT];

    mm.fA = 1.0f / (1.0f + mmRaw[MM_FILTER_A] * 800.0f);
    mm.fD = 1.0f / (1.0f + mmRaw[MM_FILTER_D] * 1600.0f);
    mm.fS = mmRaw[MM_FILTER_S];
    mm.aA = 1.0f / (1.0f + mmRaw[MM_AMP_A] * 800.0f);
    mm.aD = 1.0f / (1.0f + mmRaw[MM_AMP_D] * 1600.0f);
    mm.aS = mmRaw[MM_AMP_S];

    mm.master = mmRaw[MM_MASTER];
    mm.bendRangeSemis = 1.0f + mmRaw[MM_BEND_RANGE] * 23.0f;

    mm.tremRate = mmRaw[MM_TREM_RATE] * 20.0f;
    mm.tremDepth = mmRaw[MM_TREM_DEPTH];
    mm.tremMix = mmRaw[MM_TREM_MIX];
    mm.dlyTimeMs = 20.0f + mmRaw[MM_DLY_TIME] * 1180.0f;
    mm.dlyFb = mmRaw[MM_DLY_FB];
    mm.dlyMix = mmRaw[MM_DLY_MIX];
    mm.revSize = mmRaw[MM_REV_SIZE];
    mm.revMix = mmRaw[MM_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// Every *Decode()/mmFormatParameterE() PK_ENUM/PK_TOGGLE read here (and the
// shared arp reader in CarpenterSynth.ino) treats a raw INIT/delta byte as a
// PROPORTION of 0..127, not a literal option index -- `i = (int)(raw/127.0 *
// numOpts)`. EI(idx, n) (param_types.h) returns the raw byte at the center of
// option `idx`'s bucket so it round-trips through that decode regardless of
// n; every enum/toggle field below that isn't option 0 goes through it (raw
// 0 already decodes to index 0 on its own, so those are left as plain 0).
static const uint8_t MM_INIT[] = {
    // OSC 1: SAW, 8'
    EI(2,6), EI(2,5), 110,
    // OSC 2: SAW, 8', a few cents sharp -- real analogue-oscillator thickness
    EI(2,6), EI(2,5), 68, 90,
    // OSC 3: TRISAW an octave down (16'), classic Model D low-end reinforcement
    EI(1,6), EI(1,5), 64, 0, 0,
    // Noise off (white), mod mix off, glide off
    0, 0,  0,0,0,  0,64,
    // Filter: cutoff, stock ladder, emphasis, 2/3 kbd tracking, env amount
    90, 0, 40, EI(2,4), 80,
    // Filter env ADSR (no release stage on the real Model D)
    40, 90, 110,
    // Amp env ADSR
    30, 110, 110,
    // Master / bend range
    110, 20,
    // Output FX off
    0,0,0, 0,0,0,0,0,
    // Arp off, pattern/rate/gate defaults, 1 octave, up, no latch/sync
    0, 0, 64, 80, 0, 0, 0, 0,
};
static_assert(sizeof(MM_INIT) / sizeof(MM_INIT[0]) == MM_NUM_PARAMS,
              "MM_INIT must have exactly MM_NUM_PARAMS values");

#define MM_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta MM_P_BASS[] = { {MM_OSC1_OCT,0},{MM_OSC2_WAVE,EI(4,6)},{MM_OSC2_OCT,0},{MM_OSC2_LEVEL,100},{MM_FILTER_CUTOFF,25},{MM_FILTER_EMPHASIS,70},{MM_FILTER_ENV_AMT,110},{MM_FILTER_A,0},{MM_FILTER_D,40} };
static const PresetDelta MM_P_SUBBASS[] = { {MM_OSC1_WAVE,0},{MM_OSC1_OCT,0},{MM_OSC2_LEVEL,0},{MM_OSC3_WAVE,0},{MM_OSC3_OCT,0},{MM_OSC3_LEVEL,90},{MM_FILTER_CUTOFF,18},{MM_FILTER_EMPHASIS,45},{MM_FILTER_ENV_AMT,60},{MM_FILTER_D,50},{MM_AMP_D,50},{MM_AMP_S,100} };
static const PresetDelta MM_P_ACIDBASS[] = { {MM_OSC1_WAVE,EI(3,6)},{MM_OSC1_OCT,0},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_FILTER_CUTOFF,30},{MM_FILTER_EMPHASIS,118},{MM_FILTER_ENV_AMT,127},{MM_FILTER_A,0},{MM_FILTER_D,35},{MM_FILTER_S,0},{MM_GLIDE_ON,EI(1,2)},{MM_GLIDE_TIME,10} };
static const PresetDelta MM_P_FUNKBASS[] = { {MM_OSC1_OCT,0},{MM_OSC2_WAVE,EI(3,6)},{MM_OSC2_OCT,0},{MM_OSC2_LEVEL,90},{MM_FILTER_CUTOFF,45},{MM_FILTER_EMPHASIS,80},{MM_FILTER_ENV_AMT,100},{MM_FILTER_A,0},{MM_FILTER_D,20},{MM_FILTER_S,0},{MM_AMP_D,25},{MM_AMP_S,40} };
static const PresetDelta MM_P_GROWLBASS[] = { {MM_OSC1_OCT,0},{MM_OSC2_OCT,0},{MM_OSC2_FINE,80},{MM_OSC3_LEVEL,60},{MM_OSC3_OCT,0},{MM_OSC3_WAVE,EI(3,6)},{MM_FILTER_CUTOFF,22},{MM_FILTER_EMPHASIS,100},{MM_FILTER_ENV_AMT,90},{MM_FILTER_D,55} };

// ── Leads ──
static const PresetDelta MM_P_LEAD[] = { {MM_OSC2_FINE,76},{MM_GLIDE_ON,EI(1,2)},{MM_GLIDE_TIME,20},{MM_FILTER_CUTOFF,90},{MM_FILTER_EMPHASIS,90} };
static const PresetDelta MM_P_TBLEAD[] = { {MM_OSC1_WAVE,EI(3,6)},{MM_OSC2_LEVEL,0},{MM_FILTER_CUTOFF,55},{MM_FILTER_EMPHASIS,110},{MM_FILTER_ENV_AMT,90},{MM_FILTER_D,35},{MM_GLIDE_ON,EI(1,2)},{MM_GLIDE_TIME,8} };
static const PresetDelta MM_P_FIFTHLEAD[] = { {MM_OSC2_FINE,127},{MM_FILTER_CUTOFF,75},{MM_FILTER_EMPHASIS,60} };
static const PresetDelta MM_P_FATLEAD[] = { {MM_OSC3_LEVEL,70},{MM_OSC3_OCT,EI(2,5)},{MM_OSC3_WAVE,EI(2,6)},{MM_OSC3_FINE,70},{MM_FILTER_CUTOFF,80},{MM_FILTER_EMPHASIS,70},{MM_GLIDE_ON,EI(1,2)},{MM_GLIDE_TIME,12} };
static const PresetDelta MM_P_SOLOSAX[] = { {MM_OSC1_WAVE,EI(3,6)},{MM_OSC2_WAVE,EI(1,6)},{MM_OSC2_FINE,70},{MM_FILTER_CUTOFF,60},{MM_FILTER_EMPHASIS,55},{MM_FILTER_ENV_AMT,50},{MM_FILTER_A,20},{MM_AMP_A,15} };
static const PresetDelta MM_P_SCREAMLEAD[] = { {MM_OSC1_OCT,EI(3,5)},{MM_OSC2_OCT,EI(3,5)},{MM_FILTER_CUTOFF,95},{MM_FILTER_EMPHASIS,120},{MM_FILTER_ENV_AMT,110},{MM_FILTER_A,0},{MM_FILTER_D,70} };

// ── Brass ──
static const PresetDelta MM_P_BRASS[] = { {MM_OSC1_WAVE,EI(4,6)},{MM_OSC2_WAVE,EI(4,6)},{MM_OSC2_FINE,72},{MM_FILTER_ENV_AMT,90},{MM_FILTER_A,15},{MM_FILTER_D,60},{MM_AMP_A,10} };
static const PresetDelta MM_P_BRASSWELL[] = { {MM_OSC1_WAVE,EI(4,6)},{MM_OSC2_WAVE,EI(4,6)},{MM_OSC2_FINE,70},{MM_FILTER_ENV_AMT,70},{MM_FILTER_A,60},{MM_FILTER_D,80},{MM_FILTER_S,100},{MM_AMP_A,55},{MM_AMP_D,80},{MM_AMP_S,110} };
static const PresetDelta MM_P_FRENCHHORN[] = { {MM_OSC1_WAVE,EI(1,6)},{MM_OSC2_WAVE,EI(1,6)},{MM_OSC2_FINE,68},{MM_FILTER_CUTOFF,45},{MM_FILTER_EMPHASIS,30},{MM_FILTER_ENV_AMT,40},{MM_FILTER_A,40},{MM_AMP_A,35},{MM_AMP_D,60},{MM_AMP_S,100} };

// ── Pads/strings ──
static const PresetDelta MM_P_STRINGPAD[] = { {MM_OSC1_WAVE,EI(1,6)},{MM_OSC2_WAVE,EI(1,6)},{MM_OSC2_FINE,72},{MM_OSC3_LEVEL,40},{MM_OSC3_OCT,EI(2,5)},{MM_FILTER_CUTOFF,55},{MM_FILTER_EMPHASIS,25},{MM_FILTER_A,70},{MM_AMP_A,60},{MM_AMP_D,40},{MM_AMP_S,100},{MM_TREM_MIX,40},{MM_REV_MIX,40} };
static const PresetDelta MM_P_WARMPAD[] = { {MM_OSC1_WAVE,0},{MM_OSC2_WAVE,0},{MM_OSC2_FINE,70},{MM_FILTER_CUTOFF,45},{MM_FILTER_EMPHASIS,20},{MM_FILTER_A,80},{MM_AMP_A,70},{MM_AMP_D,50},{MM_AMP_S,105},{MM_REV_MIX,45},{MM_REV_SIZE,90} };
static const PresetDelta MM_P_CHOIRPAD[] = { {MM_OSC1_WAVE,EI(5,6)},{MM_OSC2_WAVE,EI(5,6)},{MM_OSC2_FINE,73},{MM_FILTER_CUTOFF,50},{MM_FILTER_EMPHASIS,15},{MM_FILTER_A,90},{MM_AMP_A,85},{MM_AMP_D,60},{MM_AMP_S,110},{MM_TREM_MIX,30},{MM_REV_MIX,50},{MM_REV_SIZE,100} };
static const PresetDelta MM_P_SPACEPAD[] = { {MM_OSC1_WAVE,0},{MM_OSC2_WAVE,EI(1,6)},{MM_OSC2_FINE,75},{MM_OSC3_LEVEL,35},{MM_OSC3_OCT,EI(3,5)},{MM_FILTER_CUTOFF,50},{MM_FILTER_A,110},{MM_AMP_A,120},{MM_AMP_D,60},{MM_AMP_S,110},{MM_TREM_MIX,50},{MM_TREM_RATE,20},{MM_DLY_MIX,30},{MM_DLY_TIME,60},{MM_REV_MIX,70},{MM_REV_SIZE,120} };

// ── Bells/plucks ──
static const PresetDelta MM_P_VIBES[] = { {MM_OSC1_WAVE,0},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,50},{MM_OSC3_OCT,EI(3,5)},{MM_FILTER_CUTOFF,70},{MM_FILTER_EMPHASIS,90},{MM_FILTER_ENV_AMT,60},{MM_FILTER_A,0},{MM_FILTER_D,55},{MM_FILTER_S,0},{MM_AMP_A,0},{MM_AMP_D,60},{MM_AMP_S,0} };
static const PresetDelta MM_P_MARIMBA[] = { {MM_OSC1_WAVE,0},{MM_OSC1_OCT,EI(3,5)},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_FILTER_CUTOFF,55},{MM_FILTER_EMPHASIS,40},{MM_FILTER_ENV_AMT,50},{MM_FILTER_D,20},{MM_FILTER_S,0},{MM_AMP_A,0},{MM_AMP_D,18},{MM_AMP_S,0} };
static const PresetDelta MM_P_KALIMBA[] = { {MM_OSC1_WAVE,EI(1,6)},{MM_OSC1_OCT,EI(4,5)},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_FILTER_CUTOFF,80},{MM_FILTER_EMPHASIS,50},{MM_FILTER_D,12},{MM_AMP_D,12},{MM_AMP_S,0} };
static const PresetDelta MM_P_CLAV[] = { {MM_OSC1_WAVE,EI(4,6)},{MM_OSC2_WAVE,EI(3,6)},{MM_OSC2_FINE,80},{MM_FILTER_CUTOFF,60},{MM_FILTER_EMPHASIS,90},{MM_FILTER_ENV_AMT,80},{MM_FILTER_A,0},{MM_FILTER_D,15},{MM_FILTER_S,0},{MM_AMP_A,0},{MM_AMP_D,20},{MM_AMP_S,10} };

// ── SFX/textures ──
static const PresetDelta MM_P_SELFOSC[] = { {MM_OSC1_LEVEL,0},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_NOISE_LEVEL,20},{MM_FILTER_CUTOFF,60},{MM_FILTER_EMPHASIS,127} };
static const PresetDelta MM_P_NOISESWEEP[] = { {MM_OSC1_LEVEL,0},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_NOISE_LEVEL,110},{MM_FILTER_CUTOFF,30},{MM_FILTER_EMPHASIS,100},{MM_FILTER_ENV_AMT,120},{MM_FILTER_A,0},{MM_FILTER_D,127},{MM_FILTER_S,60},{MM_AMP_A,0},{MM_AMP_D,127},{MM_AMP_S,127} };
static const PresetDelta MM_P_WINDFX[] = { {MM_OSC1_LEVEL,0},{MM_OSC2_LEVEL,0},{MM_OSC3_LEVEL,0},{MM_NOISE_LEVEL,90},{MM_NOISE_COLOR,EI(1,2)},{MM_FILTER_CUTOFF,35},{MM_FILTER_EMPHASIS,40},{MM_AMP_A,100},{MM_AMP_D,60},{MM_AMP_S,110},{MM_REV_MIX,40} };
static const PresetDelta MM_P_SIRENFX[] = { {MM_OSC2_LEVEL,0},{MM_OSC3_ASLFO,EI(1,2)},{MM_OSC3_OCT,EI(2,5)},{MM_MOD_TO_OSC,EI(1,2)},{MM_FILTER_CUTOFF,70},{MM_FILTER_EMPHASIS,40},{MM_AMP_A,20},{MM_AMP_S,127} };

// ── Sequenced/arp ──
static const PresetDelta MM_P_ARPCLASSIC[] = { {MM_OSC1_WAVE,EI(3,6)},{MM_FILTER_CUTOFF,65},{MM_FILTER_EMPHASIS,70},{MM_FILTER_ENV_AMT,60},{MM_FILTER_D,30},{MM_AMP_D,25},{MM_AMP_S,40},{MM_ARP_ENABLE,EI(1,2)},{MM_ARP_RATE,80},{MM_ARP_GATE,70} };
static const PresetDelta MM_P_ARPOCTAVES[] = { {MM_OSC1_WAVE,EI(4,6)},{MM_FILTER_CUTOFF,70},{MM_FILTER_EMPHASIS,60},{MM_ARP_ENABLE,EI(1,2)},{MM_ARP_RATE,90},{MM_ARP_GATE,80},{MM_ARP_OCTAVES,80},{MM_ARP_DIR,EI(2,6)} };
static const PresetDelta MM_P_SEQPLUCK[] = { {MM_OSC1_WAVE,EI(4,6)},{MM_OSC2_LEVEL,0},{MM_FILTER_CUTOFF,60},{MM_FILTER_EMPHASIS,90},{MM_FILTER_ENV_AMT,90},{MM_FILTER_A,0},{MM_FILTER_D,20},{MM_FILTER_S,0},{MM_AMP_A,0},{MM_AMP_D,25},{MM_AMP_S,10} };

// ── Drone ──
static const PresetDelta MM_P_DRONE[] = { {MM_OSC1_WAVE,0},{MM_OSC1_OCT,0},{MM_OSC2_WAVE,0},{MM_OSC2_OCT,0},{MM_OSC2_FINE,66},{MM_OSC3_LEVEL,50},{MM_OSC3_OCT,0},{MM_NOISE_LEVEL,15},{MM_FILTER_CUTOFF,40},{MM_FILTER_EMPHASIS,100},{MM_FILTER_A,127},{MM_AMP_A,127},{MM_AMP_D,60},{MM_AMP_S,127},{MM_REV_MIX,60},{MM_REV_SIZE,127} };

static const Preset MM_PRESETS[] = {
    MM_PRESET("MOOG BASS",    MM_P_BASS),
    MM_PRESET("SUB BASS",     MM_P_SUBBASS),
    MM_PRESET("ACID BASS",    MM_P_ACIDBASS),
    MM_PRESET("FUNK BASS",    MM_P_FUNKBASS),
    MM_PRESET("GROWL BASS",   MM_P_GROWLBASS),
    MM_PRESET("MOOG LEAD",    MM_P_LEAD),
    MM_PRESET("TB LEAD",      MM_P_TBLEAD),
    MM_PRESET("FIFTH LEAD",   MM_P_FIFTHLEAD),
    MM_PRESET("FAT LEAD",     MM_P_FATLEAD),
    MM_PRESET("SOLO SAX",     MM_P_SOLOSAX),
    MM_PRESET("SCREAM LEAD",  MM_P_SCREAMLEAD),
    MM_PRESET("MOOG BRASS",   MM_P_BRASS),
    MM_PRESET("BRASS SWELL",  MM_P_BRASSWELL),
    MM_PRESET("FRENCH HORN",  MM_P_FRENCHHORN),
    MM_PRESET("STRING PAD",   MM_P_STRINGPAD),
    MM_PRESET("WARM PAD",     MM_P_WARMPAD),
    MM_PRESET("CHOIR PAD",    MM_P_CHOIRPAD),
    MM_PRESET("SPACE PAD",    MM_P_SPACEPAD),
    MM_PRESET("VIBES",        MM_P_VIBES),
    MM_PRESET("MARIMBA",      MM_P_MARIMBA),
    MM_PRESET("KALIMBA",      MM_P_KALIMBA),
    MM_PRESET("CLAV",         MM_P_CLAV),
    MM_PRESET("SELF-OSC FLT", MM_P_SELFOSC),
    MM_PRESET("NOISE SWEEP",  MM_P_NOISESWEEP),
    MM_PRESET("WIND FX",      MM_P_WINDFX),
    MM_PRESET("SIREN FX",     MM_P_SIRENFX),
    MM_PRESET("ARP CLASSIC",  MM_P_ARPCLASSIC),
    MM_PRESET("ARP OCTAVES",  MM_P_ARPOCTAVES),
    MM_PRESET("SEQ PLUCK",    MM_P_SEQPLUCK),
    MM_PRESET("DRONE",        MM_P_DRONE),
};
#define MM_NUM_PRESETS (sizeof(MM_PRESETS) / sizeof(MM_PRESETS[0]))

static void mmApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < MM_NUM_PARAMS; i++) mmRaw[i] = (float)MM_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) mmRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    mmDecode();
}

static const EnginePageDef MM_PAGES[] = {
    { "OSC 1/2/3",  MM_OSC1_WAVE,   17 },
    { "MOD/GLIDE",  MM_MOD_MIX,      4 },
    { "FILTER",     MM_FILTER_CUTOFF,8 },
    { "ENVELOPES",  MM_FILTER_A,     6 },
    { "VOICING",    MM_MASTER,       2 },
    { "OUTPUT",     MM_TREM_RATE,    8 },
    { "ARP",        MM_ARP_ENABLE,   8 },
};
#define MM_NUM_PAGES (sizeof(MM_PAGES) / sizeof(MM_PAGES[0]))

uint8_t mmMasterParamIdE() { return MM_MASTER; }
uint8_t mmFilterCutoffParamIdE()    { return MM_FILTER_CUTOFF; }
// The real Minimoog panel says "Emphasis", not "Resonance" -- same knob.
uint8_t mmFilterResonanceParamIdE() { return MM_FILTER_EMPHASIS; }
uint8_t mmNumPagesE() { return (uint8_t)MM_NUM_PAGES; }
const EnginePageDef* mmPageE(uint8_t idx) { return (idx < MM_NUM_PAGES) ? &MM_PAGES[idx] : &MM_PAGES[0]; }

// ─── Voice ──────────────────────────────────────────────────────────────
static MMVoice mmv;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void mmEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void mmEngineActivate() {
    mmv = MMVoice();
    mmv.osc3.isOsc3 = true;   // see MMOsc::isOsc3 — osc3's TRISAW slot is a reverse saw, not a hybrid
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void mmParamsInitE() { mmApplyDeltas(nullptr, 0); }
void mmSetParameterE(uint8_t id, float v) { if (id < MM_NUM_PARAMS) { mmRaw[id] = v; mmDecode(); } }
float mmGetParameterE(uint8_t id) { return (id < MM_NUM_PARAMS) ? mmRaw[id] : 0.0f; }
void mmFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= MM_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = MM_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(mmRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        const char* const* opts = (id == MM_OSC3_WAVE) ? MM_OSC3_WAVE_NAMES : pi.opts;
        snprintf(buf, n, "%s", opts[i]);
    } else {
        int val = pi.dispMin + (int)(mmRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool mmParamIsLockableE(uint8_t id) { return id < MM_NUM_PARAMS; }
uint8_t mmNumParamsE() { return MM_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo MM_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* mmParamInfoE(uint8_t id) {
    return (id < MM_NUM_PARAMS) ? &MM_PARAM_INFO[id] : &MM_INVALID_PARAM;
}
uint8_t mmNumPresetsE() { return (uint8_t)MM_NUM_PRESETS; }
const char* mmPresetNameE(uint8_t idx) { return MM_PRESETS[idx].name; }
void mmLoadPresetE(uint8_t idx) { mmApplyDeltas(MM_PRESETS[idx].deltas, MM_PRESETS[idx].n); }
void mmLoadInitPatchE() { mmApplyDeltas(nullptr, 0); }
void mmSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < MM_NUM_PARAMS; i++) out[i] = (uint8_t)(mmRaw[i] * 255.0f + 0.5f);
}
void mmRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < MM_NUM_PARAMS; i++) mmRaw[i] = (float)in[i] * (1.0f / 255.0f);
    mmDecode();
}

uint8_t mmNumVoicesE() { return MM_VOICES; }
void    mmVoiceInitE() { mmEngineActivate(); }

void mmNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    (void)voiceIdx;
    float freq = noteToFreq(note);
    mmv.note = note; mmv.velocity = vel; mmv.velNorm = (float)vel * (1.0f / 127.0f);
    mmv.gate = true;
    mmv.targetFreq = freq;
    if (mm.glideOn && mmv.currentFreq > 20.0f) {
        mmv.gliding = true;
        mmv.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (mm.glideTimeS * SAMPLE_RATE_F);
        if (mmv.glideCoef > 1.0f) mmv.glideCoef = 1.0f;
    } else {
        mmv.currentFreq = freq;
        mmv.gliding = false;
    }
    if (trigEnv) {
        mmv.filtEnv.trigger(mm.fA, mm.fD, mm.fS);
        mmv.ampEnv.trigger(mm.aA, mm.aD, mm.aS);
    }
}
void mmNoteOffE(uint8_t voiceIdx) {
    (void)voiceIdx;
    mmv.gate = false;
    mmv.filtEnv.release();
    mmv.ampEnv.release();
}
void mmAllOffE() { mmNoteOffE(0); }
void mmSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    (void)voiceIdx; mmv.aftertouch = (float)pressure * (1.0f / 127.0f);
}

int mmRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * mm.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = mm.tremMix > 0.004f;
    const bool fxDelay  = mm.dlyMix  > 0.004f;
    const bool fxReverb = mm.revMix  > 0.004f;
    g_fxChorus.setRate(mm.tremRate);
    g_fxReverb.setRoom(mm.revSize, 0.4f);
    g_fxDelay.setTimeMs(mm.dlyTimeMs, false);

    // Sampled every audio sample (cheap: no transcendentals), but only
    // *applied* to the filter cutoff / oscillator pitch at control rate —
    // same reasoning as the CS-80 engine avoiding expf() on the audio path.
    // A held-and-updated-8x-per-block modulation signal still reaches well
    // into audible LFO/gentle-FM territory; it just isn't full audio-rate FM.
    static float modSampleHeld = 0.0f;

    int active = 0;
    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        const bool alive = mmv.ampEnv.stage != MME_IDLE;

        float out = 0.0f;
        // Idle: leave oscillators/filter completely untouched rather than
        // running them on nothing. Minimoog's ladder genuinely self-
        // oscillates (see ladder_filter.h) — with no note gating it, the
        // *filter* runs forever regardless of note-on/off, so between notes
        // it was still being driven by the free-running oscillators every
        // single sample, at whatever Emphasis was dialled in, with nothing
        // ever giving it a rest. Any resonant energy that built up (or an
        // outright self-sustaining tone once Emphasis got anywhere near the
        // self-osc threshold) had no path back to silence short of a full
        // engine switch (mmVoiceInitE() reconstructing MMVoice from
        // scratch) -- which is exactly the "sounds distorted/glitched,
        // stopping and restarting the pattern doesn't really fix it, only
        // switching engines does" pattern this was reported as. Every other
        // engine in this build (Prophet-5/10, CS-80) already skips a fully
        // idle voice's oscillator/filter work outright; this brings
        // Minimoog in line with that, so its filter actually goes quiet
        // and stays quiet between notes like the others do.
        if (alive) {
            float o3 = mmv.osc3.next();
            float nz = mmv.noise.white();
            float modSample = o3 + (nz - o3) * mm.modMix;   // MOD MIX: 0=OSC3, 1=NOISE
            if (ctrl) modSampleHeld = modSample;

            if (ctrl) {
                if (mmv.gliding) {
                    float d = mmv.targetFreq - mmv.currentFreq;
                    if (d < 0.05f && d > -0.05f) { mmv.currentFreq = mmv.targetFreq; mmv.gliding = false; }
                    else mmv.currentFreq += d * mmv.glideCoef;
                }
                float base = mmv.currentFreq * bendMul;

                // Oscillator modulation: OSC3 (or free-running as an LFO) or
                // noise, crossfaded by MOD MIX, retunes osc1/osc2 — never osc3
                // itself, exactly as the panel wires it. Depth is the mod wheel:
                // there is no separate depth knob on the real instrument either.
                float oscModRatio = 1.0f;
                if (mm.modToOsc) oscModRatio = fastPow2(modSampleHeld * modWheel * 1.0f);

                float freq1 = base * mm.o1OctMul * oscModRatio;
                float freq2 = base * mm.o2OctMul * mm.o2FineRatio * oscModRatio;
                float freq3raw;
                if (mm.o3AsLfo) {
                    static const float LFO_HZ[5] = { 0.1f, 0.5f, 2.0f, 8.0f, 25.0f };
                    int oi = (int)((mm.o3OctMul >= 3.9f) ? 4 : (mm.o3OctMul >= 1.9f) ? 3 :
                                   (mm.o3OctMul >= 0.9f) ? 2 : (mm.o3OctMul >= 0.4f) ? 1 : 0);
                    freq3raw = LFO_HZ[oi];
                } else {
                    freq3raw = base * mm.o3OctMul * mm.o3FineRatio;
                }
                mmv.osc1.setFreq(freq1);
                mmv.osc2.setFreq(freq2);
                mmv.osc3.setFreq(freq3raw);

                const float envF = mmv.filtEnv.process();
                float kbdSemis = mm.kbdTrackAmt * (float)(mmv.note - 60);
                float filterModSemis = mm.modToFilter ? (modSampleHeld * modWheel * 24.0f) : 0.0f;
                float cutoff = mm.cutoffHz *
                    fastPow2((kbdSemis + mm.envAmt * envF * 48.0f + filterModSemis) / 12.0f);
                if (cutoff < 20.0f) cutoff = 20.0f; else if (cutoff > 18000.0f) cutoff = 18000.0f;
                if (mm.filterType == VF_STOCK) mmv.filter.setCutoffHz(cutoff, mm.emphasis);
                else                            mmv.varFilt.setCutoff(cutoff, mm.emphasis, mm.filterType);
            }

            float o1 = mmv.osc1.next() * mm.o1Level;
            float o2 = mmv.osc2.next() * mm.o2Level;
            float o3out = mm.o3AsLfo ? 0.0f : o3 * mm.o3Level;
            float noiseOut = nz * mm.noiseLevel;
            float mixIn = o1 + o2 + o3out + noiseOut;

            // The real Model D's CP3-style discrete mixer has its own soft
            // "OVERLOAD" clipping, distinct from (and upstream of) both the
            // filter's own resonance-driven saturation and the final output
            // stage below — a documented, audible layer of the "fat Moog"
            // sound that shows up specifically when the individual level
            // knobs are run hot enough to beat against each other. Mild by
            // design: tanh(x)/x is within a couple of percent of 1 below
            // about 0.3, so a single- or two-oscillator patch (most bells,
            // plucks, leads) passes through essentially unchanged, and only
            // patches stacking all three oscillators plus noise near full
            // actually reach the soft knee.
            const float MM_MIXER_DRIVE = 1.15f;
            float mixerIn = fastTanh(mixIn * 0.5f * MM_MIXER_DRIVE) * (1.0f / MM_MIXER_DRIVE);

            float filtered = (mm.filterType == VF_STOCK)
                ? mmv.filter.process(mixerIn)
                : mmv.varFilt.process(mixerIn, mm.filterType, mm.emphasis);
            float amp = mmv.ampEnv.process();
            active = 1;

            // MASTER used to be multiplied straight into the saturator below, so
            // turning the volume up also drove more distortion. MM_MIX_DRIVE is
            // a fixed pre-saturator drive instead, tuned from a probe over every
            // stock preset (peaks ran 0.24-0.43 — a single oscillator-heavy
            // voice runs far cooler than the CS-80's always-both-channels mix).
            // MASTER itself is now applied as pure linear gain after the
            // saturator and the whole effect chain; `amp` (the note's own
            // amplitude envelope) stays exactly where it was, applied to the
            // saturator's output, since it was never part of the drive bug.
            const float MM_MIX_DRIVE = 3.0f;
            out = fastTanh(filtered * MM_MIX_DRIVE) * amp;
        }
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, mm.tremDepth, mm.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, mm.dlyFb, mm.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * mm.revMix; r += rr * mm.revMix;
        }
        l *= mm.master; r *= mm.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    va.busy[0] = active ? 1 : 0;
    return active;
}

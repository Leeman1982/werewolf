#include "arp2600.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  arp2600.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum A26ParamId : uint8_t {
    A26_OSC1_WAVE = 0, A26_OSC1_LEVEL,
    A26_OSC2_WAVE, A26_OSC2_TRACK, A26_OSC2_FINE, A26_OSC2_LEVEL,
    A26_OSC3_WAVE, A26_OSC3_FINE, A26_OSC3_LEVEL,
    A26_NOISE,
    A26_SH_RATE, A26_SH_TO_PITCH, A26_SH_TO_FILTER,
    A26_RING_ON, A26_RING_MIX,
    A26_FILTER_CUTOFF, A26_FILTER_TYPE, A26_FILTER_RES, A26_FILTER_MODE, A26_FILTER_ENV_AMT, A26_FILTER_KBD,
    A26_FENV_A, A26_FENV_D, A26_FENV_S, A26_FENV_R,
    A26_AENV_A, A26_AENV_D, A26_AENV_S, A26_AENV_R,
    A26_GLIDE_ON, A26_GLIDE_TIME, A26_MASTER, A26_BEND_RANGE,

    A26_TREM_RATE, A26_TREM_DEPTH, A26_TREM_MIX,
    A26_DLY_TIME, A26_DLY_FB, A26_DLY_MIX, A26_REV_SIZE, A26_REV_MIX,

    A26_ARP_ENABLE, A26_ARP_PATTERN, A26_ARP_RATE, A26_ARP_GATE,
    A26_ARP_OCTAVES, A26_ARP_DIR, A26_ARP_LATCH, A26_ARP_SYNC,

    A26_NUM_PARAMS
};

static const char* const A26_WAVE_NAMES[4]  = { "SAW", "TRIANGLE", "PULSE", "SINE" };
static const char* const A26_ONOFF[2]       = { "OFF", "ON" };
static const char* const A26_MODE_NAMES[3]  = { "LOWPASS", "BANDPASS", "HIGHPASS" };
static const char* const A26_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define A26ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [A26_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build —
// this way a mismatch between the enum and this table is a compile error.
static const ParamInfo A26_PARAM_INFO[] = {
    A26ROW("OSC1 WAVE", PK_ENUM,   A26_WAVE_NAMES, 4, 0, 3,   20),
    A26ROW("OSC1 LVL",  PK_CONT,   nullptr, 0,     0, 127,   21),
    A26ROW("OSC2 WAVE", PK_ENUM,   A26_WAVE_NAMES, 4, 0, 3,   22),
    A26ROW("OSC2 TRACK",PK_TOGGLE, A26_ONOFF, 2,   0, 1,     23),
    A26ROW("OSC2 FREQ", PK_CENTS,  nullptr, 0,    -700, 700, 24),
    A26ROW("OSC2 LVL",  PK_CONT,   nullptr, 0,     0, 127,   25),
    A26ROW("OSC3 WAVE", PK_ENUM,   A26_WAVE_NAMES, 4, 0, 3,   26),
    A26ROW("OSC3 FREQ", PK_CENTS,  nullptr, 0,    -700, 700, 27),
    A26ROW("OSC3 LVL",  PK_CONT,   nullptr, 0,     0, 127,   28),
    A26ROW("NOISE",     PK_CONT,   nullptr, 0,     0, 127,   29),
    A26ROW("S&H RATE",  PK_HZ,     nullptr, 0,     0, 40,    30),
    A26ROW("S&H>PITCH", PK_CONT,   nullptr, 0,     0, 127,   31),
    A26ROW("S&H>VCF",   PK_CONT,   nullptr, 0,     0, 127,   32),
    A26ROW("RING MOD",  PK_TOGGLE, A26_ONOFF, 2,   0, 1,     33),
    A26ROW("RING MIX",  PK_CONT,   nullptr, 0,     0, 127,   34),
    A26ROW("CUTOFF",    PK_HZ,     nullptr, 0,    20, 18000, 35),
    A26ROW("FILT TYPE", PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 68),
    A26ROW("RESONANCE", PK_CONT,   nullptr, 0,     0, 127,   36),
    A26ROW("FILT MODE", PK_ENUM,   A26_MODE_NAMES, 3, 0, 2,  37),
    A26ROW("VCF ENV AMT",PK_CONT,  nullptr, 0,     0, 127,   38),
    A26ROW("KBD AMT",   PK_CONT,   nullptr, 0,     0, 127,   39),
    A26ROW("VCF ATK",   PK_MS,     nullptr, 0,     1, 8000,  40),
    A26ROW("VCF DEC",   PK_MS,     nullptr, 0,     3, 12000, 41),
    A26ROW("VCF SUS",   PK_CONT,   nullptr, 0,     0, 127,   42),
    A26ROW("VCF REL",   PK_MS,     nullptr, 0,     3, 12000, 43),
    A26ROW("VCA ATK",   PK_MS,     nullptr, 0,     1, 8000,  44),
    A26ROW("VCA DEC",   PK_MS,     nullptr, 0,     3, 12000, 45),
    A26ROW("VCA SUS",   PK_CONT,   nullptr, 0,     0, 127,   46),
    A26ROW("VCA REL",   PK_MS,     nullptr, 0,     3, 12000, 47),
    A26ROW("GLIDE",     PK_TOGGLE, A26_ONOFF, 2,   0, 1,     48),
    A26ROW("GLIDE TIME",PK_MS,     nullptr, 0,     0, 5000,  49),
    A26ROW("MASTER",    PK_CONT,   nullptr, 0,     0, 127,   50),
    A26ROW("BEND RNG",  PK_INT,    nullptr, 0,     1, 24,    51),
    A26ROW("TREM RATE", PK_HZ,     nullptr, 0,     0, 20,    52),
    A26ROW("TREM DEPTH",PK_CONT,   nullptr, 0,     0, 127,   53),
    A26ROW("TREM MIX",  PK_CONT,   nullptr, 0,     0, 127,   54),
    A26ROW("DLY TIME",  PK_MS,     nullptr, 0,    20, 1200,  55),
    A26ROW("DLY FDBK",  PK_CONT,   nullptr, 0,     0, 127,   56),
    A26ROW("DLY MIX",   PK_CONT,   nullptr, 0,     0, 127,   57),
    A26ROW("REV SIZE",  PK_CONT,   nullptr, 0,     0, 127,   58),
    A26ROW("REV MIX",   PK_CONT,   nullptr, 0,     0, 127,   59),
    A26ROW("ARP ON",    PK_TOGGLE, A26_ONOFF, 2,   0, 1,     60),
    A26ROW("ARP PATT",  PK_INT,    nullptr, 0,     0, 7,     61),
    A26ROW("ARP RATE",  PK_CONT,   nullptr, 0,     0, 127,   62),
    A26ROW("ARP GATE",  PK_CONT,   nullptr, 0,     0, 127,   63),
    A26ROW("ARP OCT",   PK_INT,    nullptr, 0,     1, 4,     64),
    A26ROW("ARP DIR",   PK_ENUM,   A26_ARP_DIR_NAMES, 6, 0, 5, 65),
    A26ROW("ARP LATCH", PK_TOGGLE, A26_ONOFF, 2,   0, 1,     66),
    A26ROW("ARP SYNC",  PK_TOGGLE, A26_ONOFF, 2,   0, 1,     67),
};
static_assert(sizeof(A26_PARAM_INFO) / sizeof(A26_PARAM_INFO[0]) == A26_NUM_PARAMS,
              "A26_PARAM_INFO must have exactly A26_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float a26Raw[A26_NUM_PARAMS];

struct A26Decoded {
    uint8_t o1Wave; float o1Level;
    uint8_t o2Wave; bool o2Track; float o2Fine, o2Level;
    uint8_t o3Wave; float o3Fine, o3Level;
    float noiseLvl;
    float shRateHz, shToPitch, shToFilter;
    bool ringOn; float ringMix;
    float cutoffHz; uint8_t filterType; float resonance; uint8_t filterMode; float envAmt, kbdAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static A26Decoded a26;

static void a26Decode() {
    a26.o1Wave = (uint8_t)(a26Raw[A26_OSC1_WAVE] * 3.999f);
    a26.o1Level = a26Raw[A26_OSC1_LEVEL];

    a26.o2Wave = (uint8_t)(a26Raw[A26_OSC2_WAVE] * 3.999f);
    a26.o2Track = a26Raw[A26_OSC2_TRACK] > 0.5f;
    a26.o2Fine = a26Raw[A26_OSC2_FINE];
    a26.o2Level = a26Raw[A26_OSC2_LEVEL];

    a26.o3Wave = (uint8_t)(a26Raw[A26_OSC3_WAVE] * 3.999f);
    a26.o3Fine = a26Raw[A26_OSC3_FINE];
    a26.o3Level = a26Raw[A26_OSC3_LEVEL];

    a26.noiseLvl = a26Raw[A26_NOISE];

    a26.shRateHz = 0.2f + a26Raw[A26_SH_RATE] * 39.8f;
    a26.shToPitch = a26Raw[A26_SH_TO_PITCH];
    a26.shToFilter = a26Raw[A26_SH_TO_FILTER];

    a26.ringOn = a26Raw[A26_RING_ON] > 0.5f;
    a26.ringMix = a26Raw[A26_RING_MIX];

    a26.cutoffHz = 20.0f * fastPow2(a26Raw[A26_FILTER_CUTOFF] * 9.8f);
    a26.filterType = (uint8_t)(a26Raw[A26_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    a26.resonance = a26Raw[A26_FILTER_RES];
    a26.filterMode = (uint8_t)(a26Raw[A26_FILTER_MODE] * 2.999f);
    a26.envAmt = a26Raw[A26_FILTER_ENV_AMT];
    a26.kbdAmt = a26Raw[A26_FILTER_KBD];

    a26.fA = 1.0f / (1.0f + a26Raw[A26_FENV_A] * 800.0f);
    a26.fD = 1.0f / (1.0f + a26Raw[A26_FENV_D] * 1600.0f);
    a26.fS = a26Raw[A26_FENV_S];
    a26.fR = 1.0f / (1.0f + a26Raw[A26_FENV_R] * 1600.0f);
    a26.aA = 1.0f / (1.0f + a26Raw[A26_AENV_A] * 800.0f);
    a26.aD = 1.0f / (1.0f + a26Raw[A26_AENV_D] * 1600.0f);
    a26.aS = a26Raw[A26_AENV_S];
    a26.aR = 1.0f / (1.0f + a26Raw[A26_AENV_R] * 1600.0f);

    a26.glideOn = a26Raw[A26_GLIDE_ON] > 0.5f;
    a26.glideTimeS = 0.005f + a26Raw[A26_GLIDE_TIME] * 5.0f;
    a26.master = a26Raw[A26_MASTER];
    a26.bendRangeSemis = 1.0f + a26Raw[A26_BEND_RANGE] * 23.0f;

    a26.tremRate = a26Raw[A26_TREM_RATE] * 20.0f;
    a26.tremDepth = a26Raw[A26_TREM_DEPTH];
    a26.tremMix = a26Raw[A26_TREM_MIX];
    a26.dlyTimeMs = 20.0f + a26Raw[A26_DLY_TIME] * 1180.0f;
    a26.dlyFb = a26Raw[A26_DLY_FB];
    a26.dlyMix = a26Raw[A26_DLY_MIX];
    a26.revSize = a26Raw[A26_REV_SIZE];
    a26.revMix = a26Raw[A26_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n): every PK_ENUM/PK_TOGGLE decode here (and
// the shared arp reader in CarpenterSynth.ino) reads a raw INIT/delta byte
// as a proportion of 0..127, not a literal option index. Ported straight
// from the source ESP32 build, A26_INIT's own OSC2_TRACK field had exactly
// the bug this codebase's Minimoog port was found to have throughout: the
// literal "1" it used to mean "tracking ON" decodes as OFF (1/127 is nowhere
// near the >0.5 toggle threshold), which would have left oscillator 2 stuck
// at a near-0 Hz fixed pitch instead of tracking the keyboard on every
// preset that didn't explicitly override it. Fixed at the source (EI(1,2))
// rather than ported forward.
static const uint8_t A26_INIT[] = {
    0,110,                    // osc1
    0,EI(1,2),64,0,           // osc2 (wave saw, TRACK ON, in-tune fine, level 0)
    0,64,0,                   // osc3
    0,                        // noise
    30,0,0,                   // S&H
    0,0,                      // ring
    110,0,20,0,60,0,          // filter (cutoff, type, res, mode, envamt, kbd)
    5,60,90,40,                // filter env
    2,70,110,50,                // amp env
    0,64,110,20,                // glide/master/bend
    0,0,0, 0,0,0,0,0,          // output
    0,0,64,80,0,0,0,0,         // arp
};
static_assert(sizeof(A26_INIT) / sizeof(A26_INIT[0]) == A26_NUM_PARAMS,
              "A26_INIT must have exactly A26_NUM_PARAMS values");

#define A26_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta A26_P_BASS[]     = { {A26_OSC1_WAVE,0},{A26_OSC1_LEVEL,127},{A26_OSC2_WAVE,EI(2,4)},{A26_OSC2_LEVEL,60},{A26_FILTER_CUTOFF,20},{A26_FILTER_RES,60},{A26_FILTER_ENV_AMT,90},{A26_FENV_D,30} };
static const PresetDelta A26_P_DEEPBASS[] = { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,110},{A26_OSC2_LEVEL,0},{A26_FILTER_CUTOFF,16},{A26_FILTER_RES,20},{A26_FENV_D,40},{A26_AENV_D,40},{A26_AENV_S,90} };
static const PresetDelta A26_P_SQUAREBASS[]={ {A26_OSC1_WAVE,EI(2,4)},{A26_OSC1_LEVEL,110},{A26_OSC2_LEVEL,0},{A26_FILTER_CUTOFF,28},{A26_FILTER_RES,50},{A26_FILTER_ENV_AMT,80},{A26_FENV_D,25},{A26_AENV_D,25},{A26_AENV_S,40} };
static const PresetDelta A26_P_RESOBASS[] = { {A26_OSC1_LEVEL,110},{A26_OSC2_LEVEL,0},{A26_FILTER_CUTOFF,25},{A26_FILTER_RES,110},{A26_FILTER_ENV_AMT,100},{A26_FENV_A,0},{A26_FENV_D,30},{A26_FENV_S,0} };
static const PresetDelta A26_P_DUOBASS[]  = { {A26_OSC1_LEVEL,110},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,70},{A26_OSC2_LEVEL,90},{A26_FILTER_CUTOFF,26},{A26_FILTER_RES,45},{A26_FILTER_ENV_AMT,80},{A26_FENV_D,30},{A26_AENV_D,30},{A26_AENV_S,60} };

// ── Leads ──
static const PresetDelta A26_P_SAWLEAD[]  = { {A26_OSC1_LEVEL,110},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,72},{A26_OSC2_LEVEL,80},{A26_FILTER_CUTOFF,85},{A26_FILTER_RES,60},{A26_FILTER_ENV_AMT,50},{A26_FENV_A,0},{A26_FENV_D,40} };
static const PresetDelta A26_P_PULSELEAD[]= { {A26_OSC1_WAVE,EI(2,4)},{A26_OSC1_LEVEL,110},{A26_OSC2_LEVEL,0},{A26_FILTER_CUTOFF,75},{A26_FILTER_RES,70},{A26_FILTER_ENV_AMT,50},{A26_FENV_D,35} };
static const PresetDelta A26_P_GLIDELEAD[]= { {A26_OSC1_LEVEL,110},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,74},{A26_OSC2_LEVEL,70},{A26_FILTER_CUTOFF,80},{A26_FILTER_RES,55},{A26_GLIDE_ON,EI(1,2)},{A26_GLIDE_TIME,18} };
static const PresetDelta A26_P_RESOLEAD[] = { {A26_OSC1_LEVEL,110},{A26_FILTER_CUTOFF,90},{A26_FILTER_RES,115},{A26_FILTER_ENV_AMT,60},{A26_FENV_A,0},{A26_FENV_D,45} };
static const PresetDelta A26_P_FIFTHLEAD[]= { {A26_OSC1_LEVEL,100},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,127},{A26_OSC2_LEVEL,80},{A26_FILTER_CUTOFF,75},{A26_FILTER_RES,50} };

// ── SFX ──
static const PresetDelta A26_P_SCIFI_SH[] = { {A26_OSC1_LEVEL,0},{A26_NOISE,30},{A26_SH_RATE,70},{A26_SH_TO_PITCH,90},{A26_FILTER_CUTOFF,80},{A26_FILTER_RES,70},{A26_FENV_A,0},{A26_FENV_D,127},{A26_FENV_S,127} };
static const PresetDelta A26_P_SELFOSC_SCREAM[] = { {A26_OSC1_LEVEL,20},{A26_FILTER_CUTOFF,70},{A26_FILTER_RES,127},{A26_FILTER_MODE,EI(1,3)} };
static const PresetDelta A26_P_RINGFX[]   = { {A26_OSC1_WAVE,0},{A26_OSC1_LEVEL,100},{A26_OSC2_WAVE,EI(3,4)},{A26_OSC2_TRACK,0},{A26_OSC2_FINE,90},{A26_OSC2_LEVEL,0},{A26_RING_ON,EI(1,2)},{A26_RING_MIX,90} };
static const PresetDelta A26_P_NOISESWEEP[]={ {A26_OSC1_LEVEL,0},{A26_NOISE,110},{A26_FILTER_CUTOFF,30},{A26_FILTER_RES,100},{A26_FILTER_ENV_AMT,120},{A26_FENV_A,0},{A26_FENV_D,127},{A26_AENV_A,0},{A26_AENV_D,110},{A26_AENV_S,110} };
static const PresetDelta A26_P_ALIENVOICE[]={ {A26_OSC1_LEVEL,90},{A26_SH_RATE,50},{A26_SH_TO_PITCH,70},{A26_SH_TO_FILTER,40},{A26_FILTER_CUTOFF,60},{A26_FILTER_RES,80},{A26_RING_ON,EI(1,2)},{A26_RING_MIX,50},{A26_OSC2_WAVE,EI(2,4)},{A26_OSC2_LEVEL,50} };
static const PresetDelta A26_P_LASERZAP[] = { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,110},{A26_FILTER_CUTOFF,100},{A26_FILTER_RES,90},{A26_FILTER_ENV_AMT,127},{A26_FENV_A,0},{A26_FENV_D,15},{A26_FENV_S,0},{A26_AENV_A,0},{A26_AENV_D,20},{A26_AENV_S,0} };

// ── Filter/drone ──
static const PresetDelta A26_P_SUBDRONE[] = { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,100},{A26_OSC2_WAVE,EI(3,4)},{A26_OSC2_FINE,60},{A26_OSC2_LEVEL,60},{A26_FILTER_CUTOFF,30},{A26_AENV_A,110},{A26_AENV_S,110},{A26_REV_MIX,50} };
static const PresetDelta A26_P_FILTSWEEP[]= { {A26_OSC1_LEVEL,100},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,70},{A26_OSC2_LEVEL,80},{A26_FILTER_CUTOFF,20},{A26_FILTER_RES,100},{A26_FILTER_ENV_AMT,127},{A26_FENV_A,90},{A26_FENV_D,100},{A26_AENV_A,10},{A26_AENV_S,110} };
static const PresetDelta A26_P_BPHONK[]   = { {A26_OSC1_WAVE,EI(2,4)},{A26_OSC1_LEVEL,110},{A26_FILTER_MODE,EI(1,3)},{A26_FILTER_CUTOFF,55},{A26_FILTER_RES,90},{A26_FILTER_ENV_AMT,40} };

// ── Pads ──
static const PresetDelta A26_P_WARMPAD[]  = { {A26_OSC1_LEVEL,90},{A26_OSC2_WAVE,EI(3,4)},{A26_OSC2_FINE,70},{A26_OSC2_LEVEL,70},{A26_FILTER_CUTOFF,45},{A26_FILTER_RES,20},{A26_FENV_A,70},{A26_AENV_A,70},{A26_AENV_D,50},{A26_AENV_S,100} };
static const PresetDelta A26_P_SPACEPAD[] = { {A26_OSC1_LEVEL,80},{A26_OSC2_WAVE,0},{A26_OSC2_FINE,75},{A26_OSC2_LEVEL,70},{A26_FILTER_CUTOFF,50},{A26_AENV_A,120},{A26_AENV_S,110},{A26_DLY_MIX,30},{A26_DLY_TIME,60},{A26_REV_MIX,65},{A26_REV_SIZE,120} };
static const PresetDelta A26_P_EVOLVEPAD[]= { {A26_OSC1_LEVEL,80},{A26_OSC2_LEVEL,0},{A26_SH_RATE,8},{A26_SH_TO_FILTER,40},{A26_FILTER_CUTOFF,45},{A26_AENV_A,110},{A26_AENV_S,105},{A26_REV_MIX,50} };

// ── Bells/plucks ──
static const PresetDelta A26_P_BELLPLUCK[]= { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,90},{A26_OSC2_WAVE,EI(3,4)},{A26_OSC2_FINE,95},{A26_OSC2_LEVEL,60},{A26_RING_ON,EI(1,2)},{A26_RING_MIX,45},{A26_FILTER_CUTOFF,90},{A26_FENV_D,45},{A26_AENV_D,50},{A26_AENV_S,0} };
static const PresetDelta A26_P_METALPLUCK[]={ {A26_OSC1_LEVEL,90},{A26_OSC2_WAVE,EI(2,4)},{A26_OSC2_FINE,110},{A26_OSC2_LEVEL,80},{A26_RING_ON,EI(1,2)},{A26_RING_MIX,80},{A26_FILTER_CUTOFF,80},{A26_FENV_D,30},{A26_AENV_D,30},{A26_AENV_S,0} };
static const PresetDelta A26_P_PING[]     = { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,100},{A26_FILTER_CUTOFF,95},{A26_FILTER_RES,80},{A26_FENV_D,12},{A26_AENV_D,14},{A26_AENV_S,0} };

// ── Misc ──
static const PresetDelta A26_P_FLUTE[]      = { {A26_OSC1_WAVE,EI(3,4)},{A26_OSC1_LEVEL,100},{A26_OSC2_LEVEL,0},{A26_FILTER_CUTOFF,55},{A26_AENV_A,20},{A26_AENV_S,90} };
static const PresetDelta A26_P_CLASSICLEAD[]= { {A26_OSC1_LEVEL,110},{A26_FILTER_CUTOFF,70},{A26_FILTER_RES,50},{A26_FILTER_ENV_AMT,40},{A26_FENV_D,30} };

// ── Arp ──
static const PresetDelta A26_P_ARPCLASSIC[]= { {A26_OSC1_WAVE,EI(2,4)},{A26_OSC1_LEVEL,100},{A26_FILTER_CUTOFF,60},{A26_FILTER_RES,50},{A26_FILTER_ENV_AMT,50},{A26_FENV_D,20},{A26_AENV_D,18},{A26_AENV_S,30},{A26_ARP_ENABLE,EI(1,2)},{A26_ARP_RATE,80},{A26_ARP_GATE,70} };
static const PresetDelta A26_P_ARPOCTAVES[]= { {A26_OSC1_LEVEL,100},{A26_FILTER_CUTOFF,70},{A26_FILTER_RES,40},{A26_ARP_ENABLE,EI(1,2)},{A26_ARP_RATE,90},{A26_ARP_GATE,80},{A26_ARP_OCTAVES,80},{A26_ARP_DIR,EI(2,6)} };

static const Preset A26_PRESETS[] = {
    A26_PRESET("ARP BASS",       A26_P_BASS),
    A26_PRESET("DEEP BASS",      A26_P_DEEPBASS),
    A26_PRESET("SQUARE BASS",    A26_P_SQUAREBASS),
    A26_PRESET("RESO BASS",      A26_P_RESOBASS),
    A26_PRESET("DUO BASS",       A26_P_DUOBASS),
    A26_PRESET("SAW LEAD",       A26_P_SAWLEAD),
    A26_PRESET("PULSE LEAD",     A26_P_PULSELEAD),
    A26_PRESET("GLIDE LEAD",     A26_P_GLIDELEAD),
    A26_PRESET("RESO LEAD",      A26_P_RESOLEAD),
    A26_PRESET("FIFTH LEAD",     A26_P_FIFTHLEAD),
    A26_PRESET("SCI-FI S&H",     A26_P_SCIFI_SH),
    A26_PRESET("SELF-OSC SCREAM",A26_P_SELFOSC_SCREAM),
    A26_PRESET("RING FX",        A26_P_RINGFX),
    A26_PRESET("NOISE SWEEP",    A26_P_NOISESWEEP),
    A26_PRESET("ALIEN VOICE",    A26_P_ALIENVOICE),
    A26_PRESET("LASER ZAP",      A26_P_LASERZAP),
    A26_PRESET("SUB DRONE",      A26_P_SUBDRONE),
    A26_PRESET("FILTER SWEEP",   A26_P_FILTSWEEP),
    A26_PRESET("BANDPASS HONK",  A26_P_BPHONK),
    A26_PRESET("WARM PAD",       A26_P_WARMPAD),
    A26_PRESET("SPACE PAD",      A26_P_SPACEPAD),
    A26_PRESET("EVOLVING PAD",   A26_P_EVOLVEPAD),
    A26_PRESET("BELL PLUCK",     A26_P_BELLPLUCK),
    A26_PRESET("METAL PLUCK",    A26_P_METALPLUCK),
    A26_PRESET("PING",           A26_P_PING),
    A26_PRESET("FLUTE",          A26_P_FLUTE),
    A26_PRESET("CLASSIC LEAD",   A26_P_CLASSICLEAD),
    A26_PRESET("ARP CLASSIC",    A26_P_ARPCLASSIC),
    A26_PRESET("ARP OCTAVES",    A26_P_ARPOCTAVES),
};
#define A26_NUM_PRESETS (sizeof(A26_PRESETS) / sizeof(A26_PRESETS[0]))

static void a26ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < A26_NUM_PARAMS; i++) a26Raw[i] = (float)A26_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) a26Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    a26Decode();
}

static const EnginePageDef A26_PAGES[] = {
    { "OSC 1/2/3",  A26_OSC1_WAVE,     9 },
    { "S&H/RING",   A26_NOISE,         5 },
    { "FILTER",     A26_FILTER_CUTOFF, 6 },
    { "ENVELOPES",  A26_FENV_A,        8 },
    { "VOICING",    A26_GLIDE_ON,      4 },
    { "OUTPUT",     A26_TREM_RATE,     8 },
    { "ARP",        A26_ARP_ENABLE,    8 },
};
#define A26_NUM_PAGES (sizeof(A26_PAGES) / sizeof(A26_PAGES[0]))

uint8_t a26MasterParamIdE() { return A26_MASTER; }
uint8_t a26FilterCutoffParamIdE()    { return A26_FILTER_CUTOFF; }
uint8_t a26FilterResonanceParamIdE() { return A26_FILTER_RES; }
uint8_t a26NumPagesE() { return (uint8_t)A26_NUM_PAGES; }
const EnginePageDef* a26PageE(uint8_t idx) { return (idx < A26_NUM_PAGES) ? &A26_PAGES[idx] : &A26_PAGES[0]; }

// ─── Voice(s) ───────────────────────────────────────────────────────────
// A26_VOICES (2) is the array size; how many are actually in play is a
// runtime count set by a26SetPolyphonyE() — 1 for the stock 2600, 2 for the
// derived, genuinely-duophonic Odyssey engine (odyssey.cpp).
static A26Voice a26v[A26_VOICES];
static uint8_t  a26ActivePoly = 1;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void a26SetPolyphonyE(uint8_t n) { a26ActivePoly = (n > A26_VOICES) ? A26_VOICES : n; }

void a26EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void a26EngineActivate() {
    for (int i = 0; i < A26_VOICES; i++) a26v[i] = A26Voice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void a26ParamsInitE() { a26ApplyDeltas(nullptr, 0); }
void a26SetParameterE(uint8_t id, float v) { if (id < A26_NUM_PARAMS) { a26Raw[id] = v; a26Decode(); } }
float a26GetParameterE(uint8_t id) { return (id < A26_NUM_PARAMS) ? a26Raw[id] : 0.0f; }
void a26FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= A26_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = A26_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(a26Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(a26Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool a26ParamIsLockableE(uint8_t id) { return id < A26_NUM_PARAMS; }
uint8_t a26NumParamsE() { return A26_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo A26_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* a26ParamInfoE(uint8_t id) {
    return (id < A26_NUM_PARAMS) ? &A26_PARAM_INFO[id] : &A26_INVALID_PARAM;
}
uint8_t a26NumPresetsE() { return (uint8_t)A26_NUM_PRESETS; }
const char* a26PresetNameE(uint8_t idx) { return A26_PRESETS[idx].name; }
void a26LoadPresetE(uint8_t idx) { a26ApplyDeltas(A26_PRESETS[idx].deltas, A26_PRESETS[idx].n); }
void a26LoadInitPatchE() { a26ApplyDeltas(nullptr, 0); }
void a26SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < A26_NUM_PARAMS; i++) out[i] = (uint8_t)(a26Raw[i] * 255.0f + 0.5f);
}
void a26RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < A26_NUM_PARAMS; i++) a26Raw[i] = (float)in[i] * (1.0f / 255.0f);
    a26Decode();
}

uint8_t a26NumVoicesE() { return a26ActivePoly; }
void    a26VoiceInitE() { a26EngineActivate(); }

void a26NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= a26ActivePoly) return;
    A26Voice& v = a26v[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (a26.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (a26.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(a26.fA, a26.fD, a26.fS, a26.fR);
        v.ampEnv.trigger(a26.aA, a26.aD, a26.aS, a26.aR);
    }
}
void a26NoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= a26ActivePoly) return;
    A26Voice& v = a26v[voiceIdx];
    v.gate = false;
    v.filtEnv.release();
    v.ampEnv.release();
}
void a26AllOffE() { for (int i = 0; i < a26ActivePoly; i++) a26NoteOffE(i); }
void a26SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < a26ActivePoly) a26v[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int a26RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch; (void)modWheel;
    const float bendMul = fastPow2(pitchBend * a26.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = a26.tremMix > 0.004f;
    const bool fxDelay  = a26.dlyMix  > 0.004f;
    const bool fxReverb = a26.revMix  > 0.004f;
    g_fxChorus.setRate(a26.tremRate);
    g_fxReverb.setRoom(a26.revSize, 0.4f);
    g_fxDelay.setTimeMs(a26.dlyTimeMs, false);

    int active = 0;
    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        float mix = 0.0f;
        int sounding = 0;

        for (int vi = 0; vi < a26ActivePoly; vi++) {
            A26Voice& v = a26v[vi];
            const bool alive = v.ampEnv.stage != A26E_IDLE;
            if (!alive) { va.busy[vi] = 0; continue; }

            // Free-running sample & hold clock — independent of note-on,
            // exactly like the real module's own clock oscillator. Each
            // voice (in duophonic Odyssey mode) gets its own S&H tap.
            uint32_t prevSh = v.shPhase;
            v.shPhase += (uint32_t)(a26.shRateHz * PHASE_PER_HZ);
            if (v.shPhase < prevSh) v.shValue = v.noise.white();

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                float shPitchSemis = a26.shToPitch * v.shValue * 24.0f;
                float base = v.currentFreq * bendMul * fastPow2(shPitchSemis / 12.0f);

                v.osc1.setFreq(base);
                v.osc2.setFreq(a26.o2Track
                    ? base * fastPow2(((a26.o2Fine * 2.0f - 1.0f) * 7.0f) / 12.0f)
                    : 0.05f + a26.o2Fine * 30.0f);
                v.osc3.setFreq(base * fastPow2(((a26.o3Fine * 2.0f - 1.0f) * 7.0f) / 12.0f));

                const float envF = v.filtEnv.process();
                float kbdSemis = a26.kbdAmt * (float)(v.note - 60);
                float shFiltSemis = a26.shToFilter * v.shValue * 24.0f;
                float cutoff = safeClamp(
                    a26.cutoffHz * fastPow2((kbdSemis + a26.envAmt * envF * 48.0f + shFiltSemis) / 12.0f),
                    20.0f, 18000.0f);
                if (a26.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, a26.resonance);
                else                             v.varFilt.setCutoff(cutoff, a26.resonance, a26.filterType);
            }

            float o1 = v.osc1.next(a26.o1Wave);
            float o2 = v.osc2.next(a26.o2Wave);
            float o3 = v.osc3.next(a26.o3Wave) * a26.o3Level;
            float noiseOut = v.noise.white() * a26.noiseLvl;

            float o1Out = o1 * a26.o1Level;
            float o2Out = o2 * a26.o2Level;
            if (a26.ringOn) {
                float ring = o1 * o2;
                o1Out += (ring * a26.o1Level - o1Out) * a26.ringMix;
            }

            float mixIn = (o1Out + o2Out + o3 + noiseOut) * 0.5f;
            float filtered;
            if (a26.filterType == VF_STOCK) {
                float lp, bp, hp;
                v.filter.process(mixIn, &lp, &bp, &hp);
                filtered = (a26.filterMode == A26F_LP) ? lp : (a26.filterMode == A26F_BP) ? bp : hp;
            } else {
                filtered = v.varFilt.process(mixIn, a26.filterType, a26.resonance);
            }

            float amp = v.ampEnv.process();
            mix += filtered * amp;
            va.busy[vi] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 1) mix *= INV_SQRT_N[sounding];

        // MASTER used to be multiplied straight into the saturator below, so
        // turning the volume up also drove more distortion. A26_MIX_DRIVE is
        // a fixed pre-saturator drive instead, tuned from a probe over every
        // stock preset at both voice counts this code serves (ARP 2600's 1
        // and Odyssey's 2 — Odyssey runs hotter, peaking 0.18-0.95, so
        // that's the case this constant is sized for). MASTER itself is now
        // applied as pure linear gain after the saturator and the whole
        // effect chain.
        const float A26_MIX_DRIVE = 1.35f;
        float out = fastTanh(mix * A26_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, a26.tremDepth, a26.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, a26.dlyFb, a26.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * a26.revMix; r += rr * a26.revMix;
        }
        l *= a26.master; r *= a26.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

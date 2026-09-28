#include "system100.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  system100.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum Sy1ParamId : uint8_t {
    SY1_VCO_TRI = 0, SY1_VCO_SAW, SY1_VCO_PULSE, SY1_VCO_PW,
    SY1_RING_LEVEL, SY1_RING_RATIO,
    SY1_NOISE,
    SY1_SH_RATE, SY1_SH_TO_PITCH, SY1_SH_TO_FILTER,
    SY1_FILTER_CUTOFF, SY1_FILTER_TYPE, SY1_FILTER_RES, SY1_FILTER_ENV_AMT, SY1_FILTER_KBD,
    SY1_HPF_CUTOFF,
    SY1_ENV_A, SY1_ENV_D, SY1_ENV_S, SY1_ENV_R,
    SY1_GLIDE_ON, SY1_GLIDE_TIME, SY1_MASTER, SY1_BEND_RANGE,

    SY1_TREM_RATE, SY1_TREM_DEPTH, SY1_TREM_MIX,
    SY1_DLY_TIME, SY1_DLY_FB, SY1_DLY_MIX, SY1_REV_SIZE, SY1_REV_MIX,

    SY1_ARP_ENABLE, SY1_ARP_PATTERN, SY1_ARP_RATE, SY1_ARP_GATE,
    SY1_ARP_OCTAVES, SY1_ARP_DIR, SY1_ARP_LATCH, SY1_ARP_SYNC,

    SY1_NUM_PARAMS
};

static const char* const SY1_ONOFF[2]       = { "OFF", "ON" };
static const char* const SY1_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define SY1ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [SY1_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo SY1_PARAM_INFO[] = {
    SY1ROW("VCO TRI", PK_CONT,   nullptr, 0,   0, 127,  20),
    SY1ROW("VCO SAW", PK_CONT,   nullptr, 0,   0, 127,  21),
    SY1ROW("VCO PULSE",PK_CONT,  nullptr, 0,   0, 127,  22),
    SY1ROW("VCO PW",  PK_CONT,   nullptr, 0,   0, 127,  23),
    SY1ROW("RING LVL",PK_CONT,   nullptr, 0,   0, 127,  24),
    SY1ROW("RING RATIO",PK_CONT, nullptr, 0,   0, 127,  25),
    SY1ROW("NOISE",   PK_CONT,   nullptr, 0,   0, 127,  26),
    SY1ROW("S&H RATE",PK_HZ,     nullptr, 0,   0, 40,   27),
    SY1ROW("S&H>PITCH",PK_CONT,  nullptr, 0,   0, 127,  28),
    SY1ROW("S&H>VCF", PK_CONT,   nullptr, 0,   0, 127,  29),
    SY1ROW("CUTOFF",  PK_HZ,     nullptr, 0,  20, 18000, 30),
    SY1ROW("FILT TYPE",PK_ENUM,  VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 31),
    SY1ROW("RESONANCE",PK_CONT,  nullptr, 0,   0, 127,  33),
    SY1ROW("VCF ENV AMT",PK_CONT,nullptr, 0,   0, 127,  34),
    SY1ROW("KBD AMT", PK_CONT,   nullptr, 0,   0, 127,  35),
    SY1ROW("HPF TONE",PK_HZ,     nullptr, 0,  20, 2000,  36),
    SY1ROW("ENV ATK", PK_MS,     nullptr, 0,   1, 8000,  37),
    SY1ROW("ENV DEC", PK_MS,     nullptr, 0,   3, 12000, 39),
    SY1ROW("ENV SUS", PK_CONT,   nullptr, 0,   0, 127,   40),
    SY1ROW("ENV REL", PK_MS,     nullptr, 0,   3, 12000, 41),
    SY1ROW("GLIDE",   PK_TOGGLE, SY1_ONOFF, 2, 0, 1,     42),
    SY1ROW("GLIDE TIME",PK_MS,   nullptr, 0,   0, 5000,  43),
    SY1ROW("MASTER",  PK_CONT,   nullptr, 0,   0, 127,   44),
    SY1ROW("BEND RNG",PK_INT,    nullptr, 0,   1, 24,    45),
    SY1ROW("TREM RATE",PK_HZ,    nullptr, 0,   0, 20,    46),
    SY1ROW("TREM DEPTH",PK_CONT, nullptr, 0,   0, 127,   47),
    SY1ROW("TREM MIX",PK_CONT,   nullptr, 0,   0, 127,   48),
    SY1ROW("DLY TIME",PK_MS,     nullptr, 0,  20, 1200,  49),
    SY1ROW("DLY FDBK",PK_CONT,   nullptr, 0,   0, 127,   50),
    SY1ROW("DLY MIX", PK_CONT,   nullptr, 0,   0, 127,   51),
    SY1ROW("REV SIZE",PK_CONT,   nullptr, 0,   0, 127,   52),
    SY1ROW("REV MIX", PK_CONT,   nullptr, 0,   0, 127,   53),
    SY1ROW("ARP ON",  PK_TOGGLE, SY1_ONOFF, 2, 0, 1,     54),
    SY1ROW("ARP PATT",PK_INT,    nullptr, 0,   0, 7,     55),
    SY1ROW("ARP RATE",PK_CONT,   nullptr, 0,   0, 127,   56),
    SY1ROW("ARP GATE",PK_CONT,   nullptr, 0,   0, 127,   57),
    SY1ROW("ARP OCT", PK_INT,    nullptr, 0,   1, 4,     58),
    SY1ROW("ARP DIR", PK_ENUM,   SY1_ARP_DIR_NAMES, 6, 0, 5, 59),
    SY1ROW("ARP LATCH",PK_TOGGLE,SY1_ONOFF, 2, 0, 1,     60),
    SY1ROW("ARP SYNC",PK_TOGGLE, SY1_ONOFF, 2, 0, 1,     61),
};
static_assert(sizeof(SY1_PARAM_INFO) / sizeof(SY1_PARAM_INFO[0]) == SY1_NUM_PARAMS,
              "SY1_PARAM_INFO must have exactly SY1_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float sy1Raw[SY1_NUM_PARAMS];

struct Sy1Decoded {
    float vcoTri, vcoSaw, vcoPulse, vcoPw;
    float ringLevel, ringRatio;
    float noiseLvl;
    float shRateHz, shToPitch, shToFilter;
    float cutoffHz; uint8_t filterType; float resonance, envAmt, kbdAmt;
    float hpfG;
    float envA, envD, envS, envR;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static Sy1Decoded sy1;

static void sy1Decode() {
    sy1.vcoTri = sy1Raw[SY1_VCO_TRI]; sy1.vcoSaw = sy1Raw[SY1_VCO_SAW];
    sy1.vcoPulse = sy1Raw[SY1_VCO_PULSE];
    sy1.vcoPw = 0.05f + sy1Raw[SY1_VCO_PW] * 0.45f;

    sy1.ringLevel = sy1Raw[SY1_RING_LEVEL];
    sy1.ringRatio = 0.25f * fastPow2(sy1Raw[SY1_RING_RATIO] * 5.0f);   // 0.25x .. 8x

    sy1.noiseLvl = sy1Raw[SY1_NOISE];

    sy1.shRateHz = 0.2f + sy1Raw[SY1_SH_RATE] * 39.8f;
    sy1.shToPitch = sy1Raw[SY1_SH_TO_PITCH];
    sy1.shToFilter = sy1Raw[SY1_SH_TO_FILTER];

    sy1.cutoffHz = 20.0f * fastPow2(sy1Raw[SY1_FILTER_CUTOFF] * 9.8f);
    sy1.filterType = (uint8_t)(sy1Raw[SY1_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    sy1.resonance = sy1Raw[SY1_FILTER_RES];
    sy1.envAmt = sy1Raw[SY1_FILTER_ENV_AMT];
    sy1.kbdAmt = sy1Raw[SY1_FILTER_KBD];

    // Fixed, non-voltage-controlled one-pole highpass -- see system100.h.
    float hpfHz = 20.0f * fastPow2(sy1Raw[SY1_HPF_CUTOFF] * 6.6f);   // 20 Hz .. ~2 kHz
    sy1.hpfG = 1.0f - expf(-2.0f * (float)M_PI * hpfHz * INV_SAMPLE_RATE);

    sy1.envA = 1.0f / (1.0f + sy1Raw[SY1_ENV_A] * 800.0f);
    sy1.envD = 1.0f / (1.0f + sy1Raw[SY1_ENV_D] * 1600.0f);
    sy1.envS = sy1Raw[SY1_ENV_S];
    sy1.envR = 1.0f / (1.0f + sy1Raw[SY1_ENV_R] * 1600.0f);

    sy1.glideOn = sy1Raw[SY1_GLIDE_ON] > 0.5f;
    sy1.glideTimeS = 0.005f + sy1Raw[SY1_GLIDE_TIME] * 5.0f;
    sy1.master = sy1Raw[SY1_MASTER];
    sy1.bendRangeSemis = 1.0f + sy1Raw[SY1_BEND_RANGE] * 23.0f;

    sy1.tremRate = sy1Raw[SY1_TREM_RATE] * 20.0f;
    sy1.tremDepth = sy1Raw[SY1_TREM_DEPTH];
    sy1.tremMix = sy1Raw[SY1_TREM_MIX];
    sy1.dlyTimeMs = 20.0f + sy1Raw[SY1_DLY_TIME] * 1180.0f;
    sy1.dlyFb = sy1Raw[SY1_DLY_FB];
    sy1.dlyMix = sy1Raw[SY1_DLY_MIX];
    sy1.revSize = sy1Raw[SY1_REV_SIZE];
    sy1.revMix = sy1Raw[SY1_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) for why every PK_ENUM/PK_TOGGLE delta below
// that isn't option 0 goes through it.
static const uint8_t SY1_INIT[] = {
    0,110,0,64,           // vco tri/saw/pulse/pw
    0,64,                 // ring level/ratio(1x)
    0,                    // noise
    30,0,0,               // S&H rate/to-pitch/to-filter
    100,0,20,60,0,        // filter cutoff/type/res/envamt/kbd
    0,                    // hpf tone (fully open/no cut)
    5,60,90,40,           // envelope ADSR
    0,64,110,20,          // glide/glidetime/master/bend
    0,0,0, 0,0,0,0,0,     // output
    0,0,64,80,0,0,0,0,    // arp
};
static_assert(sizeof(SY1_INIT) / sizeof(SY1_INIT[0]) == SY1_NUM_PARAMS,
              "SY1_INIT must have exactly SY1_NUM_PARAMS values");

#define SY1_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta SY1_P_BASS[]     = { {SY1_VCO_SAW,110},{SY1_FILTER_CUTOFF,25},{SY1_FILTER_RES,55},{SY1_FILTER_ENV_AMT,90},{SY1_ENV_D,30} };
static const PresetDelta SY1_P_SUBBASS[]  = { {SY1_VCO_TRI,110},{SY1_VCO_SAW,0},{SY1_FILTER_CUTOFF,18},{SY1_FILTER_RES,20},{SY1_ENV_D,40},{SY1_ENV_S,90} };
static const PresetDelta SY1_P_SQUAREBASS[]={ {SY1_VCO_PULSE,110},{SY1_VCO_PW,20},{SY1_FILTER_CUTOFF,28},{SY1_FILTER_RES,50},{SY1_FILTER_ENV_AMT,70},{SY1_ENV_D,25} };
static const PresetDelta SY1_P_RESOBASS[] = { {SY1_VCO_SAW,110},{SY1_FILTER_CUTOFF,25},{SY1_FILTER_RES,120},{SY1_FILTER_ENV_AMT,80},{SY1_ENV_A,0},{SY1_ENV_D,30},{SY1_ENV_S,0} };
static const PresetDelta SY1_P_THINBASS[] = { {SY1_VCO_SAW,100},{SY1_HPF_CUTOFF,45},{SY1_FILTER_CUTOFF,40},{SY1_FILTER_RES,40},{SY1_ENV_D,25} };

// ── Leads ──
static const PresetDelta SY1_P_LEAD[]     = { {SY1_VCO_SAW,110},{SY1_FILTER_CUTOFF,80},{SY1_FILTER_RES,55},{SY1_FILTER_ENV_AMT,35},{SY1_ENV_A,0} };
static const PresetDelta SY1_P_RESOLEAD[] = { {SY1_VCO_SAW,110},{SY1_FILTER_CUTOFF,88},{SY1_FILTER_RES,110},{SY1_FILTER_ENV_AMT,50},{SY1_ENV_D,40} };
static const PresetDelta SY1_P_GLIDELEAD[]= { {SY1_VCO_SAW,110},{SY1_FILTER_CUTOFF,75},{SY1_FILTER_RES,55},{SY1_GLIDE_ON,EI(1,2)},{SY1_GLIDE_TIME,18} };
static const PresetDelta SY1_P_PWMLEAD[]  = { {SY1_VCO_PULSE,110},{SY1_VCO_PW,50},{SY1_FILTER_CUTOFF,70},{SY1_FILTER_RES,45} };
static const PresetDelta SY1_P_RINGLEAD[] = { {SY1_VCO_SAW,100},{SY1_RING_LEVEL,35},{SY1_RING_RATIO,74},{SY1_FILTER_CUTOFF,75},{SY1_FILTER_RES,50} };

// ── SFX ──
static const PresetDelta SY1_P_SCIFISH[]  = { {SY1_VCO_SAW,0},{SY1_NOISE,30},{SY1_SH_RATE,70},{SY1_SH_TO_PITCH,90},{SY1_FILTER_CUTOFF,80},{SY1_FILTER_RES,70},{SY1_ENV_A,0},{SY1_ENV_D,127},{SY1_ENV_S,127} };
static const PresetDelta SY1_P_RINGFX[]   = { {SY1_VCO_SAW,90},{SY1_RING_LEVEL,90},{SY1_RING_RATIO,95},{SY1_FILTER_CUTOFF,70},{SY1_FILTER_RES,60} };
static const PresetDelta SY1_P_METALRING[]= { {SY1_VCO_PULSE,90},{SY1_VCO_PW,35},{SY1_RING_LEVEL,110},{SY1_RING_RATIO,110},{SY1_FILTER_CUTOFF,85},{SY1_FILTER_RES,50} };
static const PresetDelta SY1_P_NOISESWEEP[]={ {SY1_VCO_SAW,0},{SY1_NOISE,110},{SY1_FILTER_CUTOFF,25},{SY1_FILTER_RES,100},{SY1_FILTER_ENV_AMT,120},{SY1_ENV_A,90},{SY1_ENV_D,100},{SY1_ENV_S,110} };
static const PresetDelta SY1_P_SELFOSC[]  = { {SY1_VCO_SAW,20},{SY1_FILTER_CUTOFF,70},{SY1_FILTER_RES,127} };
static const PresetDelta SY1_P_ALIENVOICE[]={ {SY1_VCO_SAW,90},{SY1_SH_RATE,50},{SY1_SH_TO_PITCH,70},{SY1_SH_TO_FILTER,40},{SY1_RING_LEVEL,50},{SY1_RING_RATIO,80},{SY1_FILTER_CUTOFF,60},{SY1_FILTER_RES,80} };

// ── Pads ──
static const PresetDelta SY1_P_WARMPAD[]  = { {SY1_VCO_SAW,80},{SY1_VCO_TRI,40},{SY1_FILTER_CUTOFF,45},{SY1_FILTER_RES,15},{SY1_ENV_A,80},{SY1_ENV_D,50},{SY1_ENV_S,100} };
static const PresetDelta SY1_P_HPFSHIMMER[]={ {SY1_VCO_SAW,80},{SY1_HPF_CUTOFF,55},{SY1_FILTER_CUTOFF,75},{SY1_ENV_A,70},{SY1_ENV_S,100},{SY1_REV_MIX,45} };
static const PresetDelta SY1_P_SPACEPAD[] = { {SY1_VCO_SAW,80},{SY1_FILTER_CUTOFF,50},{SY1_ENV_A,110},{SY1_ENV_S,100},{SY1_DLY_MIX,30},{SY1_DLY_TIME,60},{SY1_REV_MIX,60},{SY1_REV_SIZE,120} };
static const PresetDelta SY1_P_EVOLVEPAD[]= { {SY1_VCO_SAW,80},{SY1_SH_RATE,8},{SY1_SH_TO_FILTER,40},{SY1_FILTER_CUTOFF,45},{SY1_ENV_A,100},{SY1_ENV_S,105},{SY1_REV_MIX,45} };

// ── Bells/plucks ──
static const PresetDelta SY1_P_BELLPLUCK[]= { {SY1_VCO_SAW,90},{SY1_RING_LEVEL,45},{SY1_RING_RATIO,85},{SY1_FILTER_CUTOFF,90},{SY1_FILTER_ENV_AMT,60},{SY1_ENV_D,45},{SY1_ENV_S,0} };
static const PresetDelta SY1_P_PING[]     = { {SY1_VCO_TRI,100},{SY1_FILTER_CUTOFF,95},{SY1_FILTER_RES,80},{SY1_ENV_D,12},{SY1_ENV_S,0} };
static const PresetDelta SY1_P_MARIMBA[]  = { {SY1_VCO_PULSE,90},{SY1_VCO_PW,40},{SY1_FILTER_CUTOFF,55},{SY1_FILTER_ENV_AMT,50},{SY1_ENV_D,16},{SY1_ENV_S,0} };
static const PresetDelta SY1_P_GLASSPLUCK[]={ {SY1_VCO_SAW,80},{SY1_HPF_CUTOFF,50},{SY1_RING_LEVEL,30},{SY1_RING_RATIO,100},{SY1_FILTER_CUTOFF,95},{SY1_ENV_D,30},{SY1_ENV_S,0} };

// ── Misc ──
static const PresetDelta SY1_P_DRONE[]    = { {SY1_VCO_SAW,80},{SY1_VCO_TRI,50},{SY1_RING_LEVEL,20},{SY1_RING_RATIO,50},{SY1_FILTER_CUTOFF,35},{SY1_ENV_A,127},{SY1_ENV_S,110},{SY1_REV_MIX,55} };
static const PresetDelta SY1_P_FILTSWEEP[]= { {SY1_VCO_SAW,100},{SY1_FILTER_CUTOFF,20},{SY1_FILTER_RES,100},{SY1_FILTER_ENV_AMT,127},{SY1_ENV_A,90},{SY1_ENV_D,100},{SY1_ENV_S,110} };
static const PresetDelta SY1_P_WINDFX[]   = { {SY1_VCO_SAW,0},{SY1_NOISE,90},{SY1_FILTER_CUTOFF,35},{SY1_FILTER_RES,20},{SY1_ENV_A,100},{SY1_ENV_S,110},{SY1_REV_MIX,40} };

// ── Arp ──
static const PresetDelta SY1_P_ARPCLASSIC[]={ {SY1_VCO_PULSE,90},{SY1_VCO_PW,40},{SY1_FILTER_CUTOFF,60},{SY1_FILTER_RES,50},{SY1_FILTER_ENV_AMT,50},{SY1_ENV_D,20},{SY1_ENV_S,30},{SY1_ARP_ENABLE,EI(1,2)},{SY1_ARP_RATE,80},{SY1_ARP_GATE,70} };
static const PresetDelta SY1_P_ARPOCTAVES[]={ {SY1_VCO_SAW,100},{SY1_FILTER_CUTOFF,70},{SY1_FILTER_RES,40},{SY1_ARP_ENABLE,EI(1,2)},{SY1_ARP_RATE,90},{SY1_ARP_GATE,80},{SY1_ARP_OCTAVES,80},{SY1_ARP_DIR,EI(2,6)} };
static const PresetDelta SY1_P_ARPPULSE[] = { {SY1_VCO_PULSE,100},{SY1_VCO_PW,25},{SY1_FILTER_CUTOFF,65},{SY1_ARP_ENABLE,EI(1,2)},{SY1_ARP_RATE,90},{SY1_ARP_GATE,75} };

static const Preset SY1_PRESETS[] = {
    SY1_PRESET("SY1 BASS",     SY1_P_BASS),
    SY1_PRESET("SUB BASS",     SY1_P_SUBBASS),
    SY1_PRESET("SQUARE BASS",  SY1_P_SQUAREBASS),
    SY1_PRESET("RESO BASS",    SY1_P_RESOBASS),
    SY1_PRESET("THIN BASS",    SY1_P_THINBASS),
    SY1_PRESET("SY1 LEAD",     SY1_P_LEAD),
    SY1_PRESET("RESO LEAD",    SY1_P_RESOLEAD),
    SY1_PRESET("GLIDE LEAD",   SY1_P_GLIDELEAD),
    SY1_PRESET("PWM LEAD",     SY1_P_PWMLEAD),
    SY1_PRESET("RING LEAD",    SY1_P_RINGLEAD),
    SY1_PRESET("SCI-FI S&H",   SY1_P_SCIFISH),
    SY1_PRESET("RING FX",      SY1_P_RINGFX),
    SY1_PRESET("METAL RING",   SY1_P_METALRING),
    SY1_PRESET("NOISE SWEEP",  SY1_P_NOISESWEEP),
    SY1_PRESET("SELF-OSC SCREAM",SY1_P_SELFOSC),
    SY1_PRESET("ALIEN VOICE",  SY1_P_ALIENVOICE),
    SY1_PRESET("WARM PAD",     SY1_P_WARMPAD),
    SY1_PRESET("HPF SHIMMER",  SY1_P_HPFSHIMMER),
    SY1_PRESET("SPACE PAD",    SY1_P_SPACEPAD),
    SY1_PRESET("EVOLVING PAD", SY1_P_EVOLVEPAD),
    SY1_PRESET("BELL PLUCK",   SY1_P_BELLPLUCK),
    SY1_PRESET("PING",         SY1_P_PING),
    SY1_PRESET("MARIMBA",      SY1_P_MARIMBA),
    SY1_PRESET("GLASS PLUCK",  SY1_P_GLASSPLUCK),
    SY1_PRESET("DRONE",        SY1_P_DRONE),
    SY1_PRESET("FILTER SWEEP", SY1_P_FILTSWEEP),
    SY1_PRESET("WIND FX",      SY1_P_WINDFX),
    SY1_PRESET("ARP CLASSIC",  SY1_P_ARPCLASSIC),
    SY1_PRESET("ARP OCTAVES",  SY1_P_ARPOCTAVES),
    SY1_PRESET("ARP PULSE",    SY1_P_ARPPULSE),
};
#define SY1_NUM_PRESETS (sizeof(SY1_PRESETS) / sizeof(SY1_PRESETS[0]))

static void sy1ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < SY1_NUM_PARAMS; i++) sy1Raw[i] = (float)SY1_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) sy1Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    sy1Decode();
}

static const EnginePageDef SY1_PAGES[] = {
    { "VCO/RING",   SY1_VCO_TRI,      6 },
    { "NOISE/S&H",  SY1_NOISE,        4 },
    { "FILTER",     SY1_FILTER_CUTOFF,6 },
    { "ENVELOPE",   SY1_ENV_A,        4 },
    { "VOICING",    SY1_GLIDE_ON,     4 },
    { "OUTPUT",     SY1_TREM_RATE,    8 },
    { "ARP",        SY1_ARP_ENABLE,   8 },
};
#define SY1_NUM_PAGES (sizeof(SY1_PAGES) / sizeof(SY1_PAGES[0]))

uint8_t sy1MasterParamIdE() { return SY1_MASTER; }
uint8_t sy1FilterCutoffParamIdE()    { return SY1_FILTER_CUTOFF; }
uint8_t sy1FilterResonanceParamIdE() { return SY1_FILTER_RES; }
uint8_t sy1NumPagesE() { return (uint8_t)SY1_NUM_PAGES; }
const EnginePageDef* sy1PageE(uint8_t idx) { return (idx < SY1_NUM_PAGES) ? &SY1_PAGES[idx] : &SY1_PAGES[0]; }

// ─── Voice ──────────────────────────────────────────────────────────────
static Sy1Voice sy1v;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void sy1EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void sy1EngineActivate() {
    sy1v = Sy1Voice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void sy1ParamsInitE() { sy1ApplyDeltas(nullptr, 0); }
void sy1SetParameterE(uint8_t id, float v) { if (id < SY1_NUM_PARAMS) { sy1Raw[id] = v; sy1Decode(); } }
float sy1GetParameterE(uint8_t id) { return (id < SY1_NUM_PARAMS) ? sy1Raw[id] : 0.0f; }
void sy1FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= SY1_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = SY1_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(sy1Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(sy1Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool sy1ParamIsLockableE(uint8_t id) { return id < SY1_NUM_PARAMS; }
uint8_t sy1NumParamsE() { return SY1_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo SY1_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* sy1ParamInfoE(uint8_t id) {
    return (id < SY1_NUM_PARAMS) ? &SY1_PARAM_INFO[id] : &SY1_INVALID_PARAM;
}
uint8_t sy1NumPresetsE() { return (uint8_t)SY1_NUM_PRESETS; }
const char* sy1PresetNameE(uint8_t idx) { return SY1_PRESETS[idx].name; }
void sy1LoadPresetE(uint8_t idx) { sy1ApplyDeltas(SY1_PRESETS[idx].deltas, SY1_PRESETS[idx].n); }
void sy1LoadInitPatchE() { sy1ApplyDeltas(nullptr, 0); }
void sy1SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < SY1_NUM_PARAMS; i++) out[i] = (uint8_t)(sy1Raw[i] * 255.0f + 0.5f);
}
void sy1RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < SY1_NUM_PARAMS; i++) sy1Raw[i] = (float)in[i] * (1.0f / 255.0f);
    sy1Decode();
}

uint8_t sy1NumVoicesE() { return SY1_VOICES; }
void    sy1VoiceInitE() { sy1EngineActivate(); }

void sy1NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    (void)voiceIdx;
    float freq = noteToFreq(note);
    sy1v.note = note; sy1v.velocity = vel; sy1v.velNorm = (float)vel * (1.0f / 127.0f);
    sy1v.gate = true;
    sy1v.targetFreq = freq;
    if (sy1.glideOn && sy1v.currentFreq > 20.0f) {
        sy1v.gliding = true;
        sy1v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (sy1.glideTimeS * SAMPLE_RATE_F);
        if (sy1v.glideCoef > 1.0f) sy1v.glideCoef = 1.0f;
    } else {
        sy1v.currentFreq = freq;
        sy1v.gliding = false;
    }
    if (trigEnv) sy1v.env.trigger(sy1.envA, sy1.envD, sy1.envS, sy1.envR);
}
void sy1NoteOffE(uint8_t voiceIdx) {
    (void)voiceIdx;
    sy1v.gate = false;
    sy1v.env.release();
}
void sy1AllOffE() { sy1NoteOffE(0); }
void sy1SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    (void)voiceIdx; sy1v.aftertouch = (float)pressure * (1.0f / 127.0f);
}

int sy1RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)modWheel; (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * sy1.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = sy1.tremMix > 0.004f;
    const bool fxDelay  = sy1.dlyMix  > 0.004f;
    const bool fxReverb = sy1.revMix  > 0.004f;
    g_fxChorus.setRate(sy1.tremRate);
    g_fxReverb.setRoom(sy1.revSize, 0.4f);
    g_fxDelay.setTimeMs(sy1.dlyTimeMs, false);

    int active = 0;
    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        const bool alive = sy1v.env.stage != SY1_E_IDLE;

        float out = 0.0f;
        if (alive) {
            // Free-running sample & hold clock, independent of note-on --
            // exactly like the real 102 expander's own clock oscillator.
            uint32_t prevSh = sy1v.shPhase;
            sy1v.shPhase += (uint32_t)(sy1.shRateHz * PHASE_PER_HZ);
            if (sy1v.shPhase < prevSh) sy1v.shValue = sy1v.noise.white();

            // The 101/102 drives its filter and its VCA off the SAME single
            // envelope (no separate filter/amp contours on the real panel),
            // so this has to be exactly ONE process() call per sample: the
            // VCA side below needs it every sample for smooth amplitude,
            // and the cutoff side only actually reads envF at control rate
            // (same as every other modulation source in this codebase) --
            // it must not take a second call of its own, or the shared
            // envelope over-advances on every control tick.
            const float envF = sy1v.env.process();

            if (ctrl) {
                if (sy1v.gliding) {
                    float d = sy1v.targetFreq - sy1v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { sy1v.currentFreq = sy1v.targetFreq; sy1v.gliding = false; }
                    else sy1v.currentFreq += d * sy1v.glideCoef;
                }
                float shPitchSemis = sy1.shToPitch * sy1v.shValue * 24.0f;
                float base = sy1v.currentFreq * bendMul * fastPow2(shPitchSemis / 12.0f);
                sy1v.osc.setFreq(base);
                sy1v.ringOsc.setFreq(base * sy1.ringRatio);

                float kbdSemis = sy1.kbdAmt * (float)(sy1v.note - 60);
                float shFiltSemis = sy1.shToFilter * sy1v.shValue * 24.0f;
                float cutoff = safeClamp(
                    sy1.cutoffHz * fastPow2((kbdSemis + sy1.envAmt * envF * 48.0f + shFiltSemis) / 12.0f),
                    20.0f, 18000.0f);
                if (sy1.filterType == VF_STOCK) sy1v.filter.setCutoffHz(cutoff, sy1.resonance);
                else                             sy1v.varFilt.setCutoff(cutoff, sy1.resonance, sy1.filterType);
            }

            float t = sy1v.osc.advance();
            float vcoOut = sy1v.osc.tri(t) * sy1.vcoTri + sy1v.osc.saw(t) * sy1.vcoSaw
                         + sy1v.osc.pulse(t, sy1.vcoPw) * sy1.vcoPulse;
            float ringOut = sy1v.ringOsc.advance();
            float noiseOut = sy1v.noise.white() * sy1.noiseLvl;

            float ringMod = vcoOut * ringOut * sy1.ringLevel;
            float voiceIn = (vcoOut + ringMod + noiseOut) * 0.5f;

            float filtered = (sy1.filterType == VF_STOCK)
                ? sy1v.filter.process(voiceIn)
                : sy1v.varFilt.process(voiceIn, sy1.filterType, sy1.resonance);
            filtered = sy1v.hpf.process(filtered, sy1.hpfG);

            float amp = envF;
            active = 1;

            // MASTER used to be multiplied straight into the saturator
            // below, so turning the volume up also drove more distortion.
            // SY1_MIX_DRIVE is a fixed pre-saturator drive instead,
            // matching every other engine's own fixed-drive fix for the
            // same bug. MASTER itself is now applied as pure linear gain
            // after the saturator and the whole effect chain.
            const float SY1_MIX_DRIVE = 2.2f;
            out = fastTanh(filtered * SY1_MIX_DRIVE) * amp;
        }
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, sy1.tremDepth, sy1.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, sy1.dlyFb, sy1.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * sy1.revMix; r += rr * sy1.revMix;
        }
        l *= sy1.master; r *= sy1.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    va.busy[0] = active ? 1 : 0;
    return active;
}

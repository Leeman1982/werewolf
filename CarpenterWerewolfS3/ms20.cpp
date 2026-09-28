#include "ms20.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>

// ============================================================================
//  ms20.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum Ms20ParamId : uint8_t {
    MS20_VCO1_SAW = 0, MS20_VCO1_PULSE, MS20_VCO1_PWM,
    MS20_VCO2_WAVE, MS20_VCO2_FINE,
    MS20_SYNC,
    MS20_NOISE,
    MS20_HPF_CUTOFF, MS20_HPF_RES, MS20_HPF_ENV_AMT,
    MS20_LPF_CUTOFF, MS20_LPF_RES, MS20_LPF_ENV_AMT,
    MS20_FILTER_TYPE,
    MS20_KBD_TRACK,
    MS20_PENV_HOLD, MS20_PENV_A, MS20_PENV_D, MS20_PENV_S, MS20_PENV_R, MS20_PENV_AMT,
    MS20_AENV_HOLD, MS20_AENV_A, MS20_AENV_D, MS20_AENV_S, MS20_AENV_R,
    MS20_GLIDE_ON, MS20_GLIDE_TIME,
    MS20_MASTER, MS20_BEND_RANGE,

    MS20_TREM_RATE, MS20_TREM_DEPTH, MS20_TREM_MIX,
    MS20_DLY_TIME, MS20_DLY_FB, MS20_DLY_MIX, MS20_REV_SIZE, MS20_REV_MIX,

    MS20_ARP_ENABLE, MS20_ARP_PATTERN, MS20_ARP_RATE, MS20_ARP_GATE,
    MS20_ARP_OCTAVES, MS20_ARP_DIR, MS20_ARP_LATCH, MS20_ARP_SYNC,

    MS20_NUM_PARAMS
};

static const char* const MS20_ONOFF[2]      = { "OFF", "ON" };
static const char* const MS20_VCO2_NAMES[3] = { "SAW", "SQUARE", "RING MOD" };
static const char* const MS20_KBD_NAMES[3]  = { "OFF", "HALF", "FULL" };
static const char* const MS20_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define MS20ROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [MS20_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo MS20_PARAM_INFO[] = {
    MS20ROW("V1 SAW",   PK_CONT,   nullptr, 0,    0, 127,  20),
    MS20ROW("V1 PULSE", PK_CONT,   nullptr, 0,    0, 127,  21),
    MS20ROW("V1 PWM",   PK_CONT,   nullptr, 0,    0, 127,  22),
    MS20ROW("V2 WAVE",  PK_ENUM,   MS20_VCO2_NAMES, 3, 0, 2, 23),
    MS20ROW("V2 FINE",  PK_CENTS,  nullptr, 0,  -700, 700,  24),
    MS20ROW("SYNC",     PK_TOGGLE, MS20_ONOFF, 2, 0, 1,     25),
    MS20ROW("NOISE",    PK_CONT,   nullptr, 0,    0, 127,  26),
    MS20ROW("HPF CUTOFF",PK_HZ,    nullptr, 0,   20, 15000, 27),
    MS20ROW("HPF PEAK", PK_CONT,   nullptr, 0,    0, 127,  28),
    MS20ROW("HPF EG AMT",PK_CONT,  nullptr, 0,    0, 127,  29),
    MS20ROW("LPF CUTOFF",PK_HZ,    nullptr, 0,   20, 15000, 30),
    MS20ROW("LPF PEAK", PK_CONT,   nullptr, 0,    0, 127,  31),
    MS20ROW("LPF EG AMT",PK_CONT,  nullptr, 0,    0, 127,  33),
    MS20ROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 34),
    MS20ROW("KBD TRACK",PK_ENUM,   MS20_KBD_NAMES, 3,0, 2, 35),
    MS20ROW("EG1 HOLD", PK_MS,     nullptr, 0,    0, 8000, 36),
    MS20ROW("EG1 ATK",  PK_MS,     nullptr, 0,    1, 8000, 37),
    MS20ROW("EG1 DEC",  PK_MS,     nullptr, 0,    3, 12000,39),
    MS20ROW("EG1 SUS",  PK_CONT,   nullptr, 0,    0, 127,  40),
    MS20ROW("EG1 REL",  PK_MS,     nullptr, 0,    3, 12000,41),
    MS20ROW("EG1>PITCH",PK_CONT,   nullptr, 0,    0, 127,  42),
    MS20ROW("EG2 HOLD", PK_MS,     nullptr, 0,    0, 8000, 43),
    MS20ROW("EG2 ATK",  PK_MS,     nullptr, 0,    1, 8000, 44),
    MS20ROW("EG2 DEC",  PK_MS,     nullptr, 0,    3, 12000,45),
    MS20ROW("EG2 SUS",  PK_CONT,   nullptr, 0,    0, 127,  46),
    MS20ROW("EG2 REL",  PK_MS,     nullptr, 0,    3, 12000,47),
    MS20ROW("GLIDE",    PK_TOGGLE, MS20_ONOFF, 2, 0, 1,    48),
    MS20ROW("GLIDE TIME",PK_MS,    nullptr, 0,    0, 5000, 49),
    MS20ROW("MASTER",   PK_CONT,   nullptr, 0,    0, 127,  50),
    MS20ROW("BEND RNG", PK_INT,    nullptr, 0,    1, 24,   51),
    MS20ROW("TREM RATE",PK_HZ,     nullptr, 0,    0, 20,   52),
    MS20ROW("TREM DEPTH",PK_CONT,  nullptr, 0,    0, 127,  53),
    MS20ROW("TREM MIX", PK_CONT,   nullptr, 0,    0, 127,  54),
    MS20ROW("DLY TIME", PK_MS,     nullptr, 0,   20, 1200, 55),
    MS20ROW("DLY FDBK", PK_CONT,   nullptr, 0,    0, 127,  56),
    MS20ROW("DLY MIX",  PK_CONT,   nullptr, 0,    0, 127,  57),
    MS20ROW("REV SIZE", PK_CONT,   nullptr, 0,    0, 127,  58),
    MS20ROW("REV MIX",  PK_CONT,   nullptr, 0,    0, 127,  59),
    MS20ROW("ARP ON",   PK_TOGGLE, MS20_ONOFF, 2, 0, 1,    60),
    MS20ROW("ARP PATT", PK_INT,    nullptr, 0,    0, 7,    61),
    MS20ROW("ARP RATE", PK_CONT,   nullptr, 0,    0, 127,  62),
    MS20ROW("ARP GATE", PK_CONT,   nullptr, 0,    0, 127,  63),
    MS20ROW("ARP OCT",  PK_INT,    nullptr, 0,    1, 4,    65),
    MS20ROW("ARP DIR",  PK_ENUM,   MS20_ARP_DIR_NAMES,6,0,5,66),
    MS20ROW("ARP LATCH",PK_TOGGLE, MS20_ONOFF, 2, 0, 1,    67),
    MS20ROW("ARP SYNC", PK_TOGGLE, MS20_ONOFF, 2, 0, 1,    68),
};
static_assert(sizeof(MS20_PARAM_INFO) / sizeof(MS20_PARAM_INFO[0]) == MS20_NUM_PARAMS,
              "MS20_PARAM_INFO must have exactly MS20_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float ms20Raw[MS20_NUM_PARAMS];

struct Ms20Decoded {
    float v1Saw, v1Pulse, v1Pwm;
    uint8_t v2Wave; float v2FineRatio;
    bool syncOn;
    float noiseLvl;
    float hpfCutoffHz, hpfPeak, hpfEnvAmt;
    float lpfCutoffHz, lpfPeak, lpfEnvAmt;
    uint8_t filterType; float kbdAmt;
    float pHoldS, pA, pD, pS, pR, pEnvAmtSemis;
    float aHoldS, aA, aD, aS, aR;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static Ms20Decoded ms20;

static void ms20Decode() {
    ms20.v1Saw = ms20Raw[MS20_VCO1_SAW]; ms20.v1Pulse = ms20Raw[MS20_VCO1_PULSE];
    ms20.v1Pwm = 0.05f + ms20Raw[MS20_VCO1_PWM] * 0.45f;
    ms20.v2Wave = (uint8_t)(ms20Raw[MS20_VCO2_WAVE] * 2.999f);
    ms20.v2FineRatio = fastPow2(((ms20Raw[MS20_VCO2_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);
    ms20.syncOn = ms20Raw[MS20_SYNC] > 0.5f;
    ms20.noiseLvl = ms20Raw[MS20_NOISE];

    ms20.hpfCutoffHz = 20.0f * fastPow2(ms20Raw[MS20_HPF_CUTOFF] * 9.55f);   // 20 Hz .. ~15 kHz
    ms20.hpfPeak = ms20Raw[MS20_HPF_RES];
    ms20.hpfEnvAmt = ms20Raw[MS20_HPF_ENV_AMT];
    ms20.lpfCutoffHz = 20.0f * fastPow2(ms20Raw[MS20_LPF_CUTOFF] * 9.55f);
    ms20.lpfPeak = ms20Raw[MS20_LPF_RES];
    ms20.lpfEnvAmt = ms20Raw[MS20_LPF_ENV_AMT];
    ms20.filterType = (uint8_t)(ms20Raw[MS20_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    { static const float KBD_TAB[3] = { 0.0f, 0.5f, 1.0f };
      int i = (int)(ms20Raw[MS20_KBD_TRACK] * 2.999f); if (i < 0) i = 0; else if (i > 2) i = 2;
      ms20.kbdAmt = KBD_TAB[i]; }

    ms20.pHoldS = ms20Raw[MS20_PENV_HOLD] * 8.0f;
    ms20.pA = 1.0f / (1.0f + ms20Raw[MS20_PENV_A] * 800.0f);
    ms20.pD = 1.0f / (1.0f + ms20Raw[MS20_PENV_D] * 1600.0f);
    ms20.pS = ms20Raw[MS20_PENV_S];
    ms20.pR = 1.0f / (1.0f + ms20Raw[MS20_PENV_R] * 1600.0f);
    ms20.pEnvAmtSemis = (ms20Raw[MS20_PENV_AMT] * 2.0f - 1.0f) * 24.0f;   // bipolar, +/-2 octaves

    ms20.aHoldS = ms20Raw[MS20_AENV_HOLD] * 8.0f;
    ms20.aA = 1.0f / (1.0f + ms20Raw[MS20_AENV_A] * 800.0f);
    ms20.aD = 1.0f / (1.0f + ms20Raw[MS20_AENV_D] * 1600.0f);
    ms20.aS = ms20Raw[MS20_AENV_S];
    ms20.aR = 1.0f / (1.0f + ms20Raw[MS20_AENV_R] * 1600.0f);

    ms20.glideOn = ms20Raw[MS20_GLIDE_ON] > 0.5f;
    ms20.glideTimeS = 0.005f + ms20Raw[MS20_GLIDE_TIME] * 5.0f;
    ms20.master = ms20Raw[MS20_MASTER];
    ms20.bendRangeSemis = 1.0f + ms20Raw[MS20_BEND_RANGE] * 23.0f;

    ms20.tremRate = ms20Raw[MS20_TREM_RATE] * 20.0f;
    ms20.tremDepth = ms20Raw[MS20_TREM_DEPTH];
    ms20.tremMix = ms20Raw[MS20_TREM_MIX];
    ms20.dlyTimeMs = 20.0f + ms20Raw[MS20_DLY_TIME] * 1180.0f;
    ms20.dlyFb = ms20Raw[MS20_DLY_FB];
    ms20.dlyMix = ms20Raw[MS20_DLY_MIX];
    ms20.revSize = ms20Raw[MS20_REV_SIZE];
    ms20.revMix = ms20Raw[MS20_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) — every PK_ENUM/PK_TOGGLE below that isn't
// option 0 goes through it, same discipline as every other engine's table.
static const uint8_t MS20_INIT[] = {
    100, 0, 64,           // VCO1 saw/pulse/pwm
    0, 68,                 // VCO2 wave(SAW)/fine
    0,                      // sync
    0,                      // noise
    30, 30, 60,             // HPF cutoff/peak/eg-amt
    90, 40, 70,             // LPF cutoff/peak/eg-amt
    0, EI(2,3),             // filter type(STOCK)/kbd(FULL)
    0, 10, 60, 0, 40, 64,   // EG1 hold/atk/dec/sus/rel/amt(centered=off)
    0, 5, 70, 110, 40,      // EG2 hold/atk/dec/sus/rel
    0, 64,                  // glide on/time
    110, 20,                // master/bend
    0,0,0, 0,0,0,0,0,       // output
    0,0,64,80,0,0,0,0,      // arp
};
static_assert(sizeof(MS20_INIT) / sizeof(MS20_INIT[0]) == MS20_NUM_PARAMS,
              "MS20_INIT must have exactly MS20_NUM_PARAMS values");

#define MS20_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta MS20_P_ACIDBASS[] = { {MS20_VCO1_PULSE,110},{MS20_VCO1_SAW,0},{MS20_LPF_CUTOFF,35},{MS20_LPF_RES,118},{MS20_LPF_ENV_AMT,127},{MS20_AENV_A,0},{MS20_AENV_D,35},{MS20_AENV_S,0} };
static const PresetDelta MS20_P_SCREAMBASS[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(1,3)},{MS20_VCO2_FINE,60},{MS20_LPF_CUTOFF,30},{MS20_LPF_RES,120},{MS20_LPF_ENV_AMT,110},{MS20_HPF_CUTOFF,15},{MS20_AENV_D,30} };
static const PresetDelta MS20_P_SUBBASS[] = { {MS20_VCO1_SAW,90},{MS20_VCO2_WAVE,EI(0,3)},{MS20_LPF_CUTOFF,15},{MS20_LPF_RES,20},{MS20_AENV_D,55},{MS20_AENV_S,100} };
static const PresetDelta MS20_P_SYNCBASS[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(0,3)},{MS20_VCO2_FINE,90},{MS20_SYNC,EI(1,2)},{MS20_LPF_CUTOFF,35},{MS20_LPF_RES,70},{MS20_LPF_ENV_AMT,90},{MS20_AENV_D,30} };

// ── Leads ──
static const PresetDelta MS20_P_SCREAMLEAD[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(1,3)},{MS20_VCO2_FINE,90},{MS20_LPF_CUTOFF,90},{MS20_LPF_RES,120},{MS20_LPF_ENV_AMT,80},{MS20_HPF_CUTOFF,20},{MS20_AENV_A,0},{MS20_AENV_D,40} };
static const PresetDelta MS20_P_SYNCLEAD[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(0,3)},{MS20_VCO2_FINE,110},{MS20_SYNC,EI(1,2)},{MS20_LPF_CUTOFF,95},{MS20_LPF_RES,90},{MS20_GLIDE_ON,EI(1,2)},{MS20_GLIDE_TIME,10} };
static const PresetDelta MS20_P_RINGLEAD[] = { {MS20_VCO1_SAW,100},{MS20_VCO2_WAVE,EI(2,3)},{MS20_VCO2_FINE,80},{MS20_LPF_CUTOFF,75},{MS20_LPF_RES,60} };
static const PresetDelta MS20_P_PWMLEAD[] = { {MS20_VCO1_PULSE,100},{MS20_VCO1_SAW,0},{MS20_VCO1_PWM,50},{MS20_LPF_CUTOFF,70},{MS20_LPF_RES,45} };
static const PresetDelta MS20_P_VOWELLEAD[] = { {MS20_VCO1_SAW,90},{MS20_HPF_CUTOFF,45},{MS20_HPF_RES,90},{MS20_LPF_CUTOFF,60},{MS20_LPF_RES,70},{MS20_FILTER_TYPE,EI(13,VF_NUM_MODES)} };

// ── Pitch-envelope showcase (see ms20.h file banner point 1) ──
static const PresetDelta MS20_P_PITCHSWOOP[] = { {MS20_VCO1_SAW,100},{MS20_PENV_AMT,110},{MS20_PENV_A,0},{MS20_PENV_D,55},{MS20_LPF_CUTOFF,75},{MS20_LPF_RES,60},{MS20_AENV_A,0},{MS20_AENV_D,60} };
static const PresetDelta MS20_P_LASERPLUCK[] = { {MS20_VCO1_SAW,110},{MS20_PENV_AMT,30},{MS20_PENV_A,0},{MS20_PENV_D,15},{MS20_LPF_CUTOFF,85},{MS20_LPF_RES,90},{MS20_AENV_D,20},{MS20_AENV_S,0} };
static const PresetDelta MS20_P_SIRENVOICE[] = { {MS20_VCO1_SAW,100},{MS20_PENV_AMT,90},{MS20_PENV_A,80},{MS20_PENV_D,90},{MS20_PENV_S,90},{MS20_LPF_CUTOFF,70},{MS20_AENV_A,20},{MS20_AENV_S,120} };

// ── EG2 hold quirk showcase (see ms20.h file banner point 2) ──
static const PresetDelta MS20_P_HOLDPAD[] = { {MS20_VCO1_SAW,90},{MS20_VCO2_WAVE,EI(0,3)},{MS20_VCO2_FINE,72},{MS20_AENV_HOLD,80},{MS20_AENV_A,50},{MS20_AENV_D,60},{MS20_AENV_S,100},{MS20_LPF_CUTOFF,55} };
static const PresetDelta MS20_P_GHOSTTAIL[] = { {MS20_VCO1_SAW,80},{MS20_AENV_HOLD,110},{MS20_AENV_A,20},{MS20_AENV_R,90},{MS20_LPF_CUTOFF,50},{MS20_LPF_RES,50},{MS20_REV_MIX,40} };

// ── Filter-scream FX (see ms20.h file banner point 3) ──
static const PresetDelta MS20_P_SELFOSCFX[] = { {MS20_VCO1_SAW,0},{MS20_NOISE,15},{MS20_LPF_CUTOFF,55},{MS20_LPF_RES,127} };
static const PresetDelta MS20_P_HPFSCREAM[] = { {MS20_VCO1_SAW,0},{MS20_NOISE,20},{MS20_HPF_CUTOFF,60},{MS20_HPF_RES,127},{MS20_LPF_CUTOFF,120} };
static const PresetDelta MS20_P_FILTERSWEEP[] = { {MS20_VCO1_SAW,100},{MS20_LPF_CUTOFF,20},{MS20_LPF_RES,100},{MS20_LPF_ENV_AMT,127},{MS20_AENV_A,90},{MS20_AENV_D,100},{MS20_AENV_S,110} };
static const PresetDelta MS20_P_NOISESWEEP[] = { {MS20_VCO1_SAW,0},{MS20_NOISE,110},{MS20_LPF_CUTOFF,30},{MS20_LPF_RES,90},{MS20_LPF_ENV_AMT,110},{MS20_AENV_A,100},{MS20_AENV_D,110},{MS20_AENV_S,110} };

// ── Ring mod SFX (VCO2 wave switch, see file banner point 4) ──
static const PresetDelta MS20_P_BELLPLUCK[] = { {MS20_VCO1_SAW,90},{MS20_VCO2_WAVE,EI(2,3)},{MS20_VCO2_FINE,110},{MS20_LPF_CUTOFF,90},{MS20_LPF_ENV_AMT,50},{MS20_AENV_D,45},{MS20_AENV_S,0} };
static const PresetDelta MS20_P_RINGCLANG[] = { {MS20_VCO1_SAW,100},{MS20_VCO2_WAVE,EI(2,3)},{MS20_VCO2_FINE,60},{MS20_LPF_CUTOFF,100},{MS20_LPF_RES,50},{MS20_AENV_D,60},{MS20_AENV_S,20} };
static const PresetDelta MS20_P_METALHIT[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(2,3)},{MS20_VCO2_FINE,127},{MS20_LPF_CUTOFF,110},{MS20_LPF_RES,70},{MS20_AENV_A,0},{MS20_AENV_D,25},{MS20_AENV_S,0} };

// ── Brass/stabs ──
static const PresetDelta MS20_P_BRASSSTAB[] = { {MS20_VCO1_SAW,110},{MS20_VCO2_WAVE,EI(0,3)},{MS20_VCO2_FINE,72},{MS20_LPF_CUTOFF,55},{MS20_LPF_ENV_AMT,95},{MS20_AENV_A,10},{MS20_AENV_D,30},{MS20_AENV_S,50} };
static const PresetDelta MS20_P_HORNSTAB[] = { {MS20_VCO1_PULSE,90},{MS20_VCO1_SAW,0},{MS20_VCO1_PWM,40},{MS20_LPF_CUTOFF,50},{MS20_LPF_ENV_AMT,70},{MS20_AENV_A,15},{MS20_AENV_D,35},{MS20_AENV_S,60} };

// ── Pads ──
static const PresetDelta MS20_P_WARMPAD[] = { {MS20_VCO1_SAW,80},{MS20_VCO2_WAVE,EI(0,3)},{MS20_VCO2_FINE,68},{MS20_LPF_CUTOFF,45},{MS20_LPF_RES,15},{MS20_AENV_A,80},{MS20_AENV_D,50},{MS20_AENV_S,105} };
static const PresetDelta MS20_P_HAUNTPAD[] = { {MS20_VCO1_SAW,85},{MS20_HPF_CUTOFF,25},{MS20_HPF_RES,55},{MS20_LPF_CUTOFF,50},{MS20_LPF_RES,40},{MS20_AENV_A,110},{MS20_AENV_S,100},{MS20_REV_MIX,45} };

// ── Bells/plucks ──
static const PresetDelta MS20_P_MARIMBA[] = { {MS20_VCO1_PULSE,90},{MS20_VCO1_SAW,0},{MS20_LPF_CUTOFF,55},{MS20_LPF_ENV_AMT,50},{MS20_AENV_D,18},{MS20_AENV_S,0} };
static const PresetDelta MS20_P_KALIMBA[] = { {MS20_VCO1_SAW,80},{MS20_LPF_CUTOFF,80},{MS20_LPF_RES,40},{MS20_AENV_D,15},{MS20_AENV_S,0} };

// ── SFX ──
static const PresetDelta MS20_P_ESPGRIT[] = { {MS20_VCO1_SAW,60},{MS20_NOISE,70},{MS20_HPF_CUTOFF,50},{MS20_HPF_RES,80},{MS20_LPF_CUTOFF,100},{MS20_AENV_A,10},{MS20_AENV_S,100} };
static const PresetDelta MS20_P_ALARMFX[] = { {MS20_VCO1_PULSE,110},{MS20_VCO1_SAW,0},{MS20_PENV_AMT,80},{MS20_PENV_A,30},{MS20_PENV_D,30},{MS20_LPF_CUTOFF,80},{MS20_AENV_A,0},{MS20_AENV_S,127} };
static const PresetDelta MS20_P_GROWLFX[] = { {MS20_VCO1_SAW,100},{MS20_VCO2_WAVE,EI(1,3)},{MS20_VCO2_FINE,55},{MS20_LPF_CUTOFF,40},{MS20_LPF_RES,110},{MS20_LPF_ENV_AMT,60},{MS20_AENV_A,30},{MS20_AENV_S,110} };

// ── Arp ──
static const PresetDelta MS20_P_ARPCLASSIC[] = { {MS20_VCO1_PULSE,90},{MS20_VCO1_SAW,0},{MS20_LPF_CUTOFF,60},{MS20_LPF_RES,50},{MS20_LPF_ENV_AMT,60},{MS20_AENV_D,20},{MS20_AENV_S,30},{MS20_ARP_ENABLE,EI(1,2)},{MS20_ARP_RATE,80},{MS20_ARP_GATE,70} };
static const PresetDelta MS20_P_ARPSCREAM[] = { {MS20_VCO1_SAW,100},{MS20_LPF_CUTOFF,65},{MS20_LPF_RES,100},{MS20_ARP_ENABLE,EI(1,2)},{MS20_ARP_RATE,95},{MS20_ARP_GATE,80},{MS20_ARP_OCTAVES,48},{MS20_ARP_DIR,EI(2,6)} };

static const Preset MS20_PRESETS[] = {
    MS20_PRESET("ACID BASS",    MS20_P_ACIDBASS),
    MS20_PRESET("SCREAM BASS",  MS20_P_SCREAMBASS),
    MS20_PRESET("SUB BASS",     MS20_P_SUBBASS),
    MS20_PRESET("SYNC BASS",    MS20_P_SYNCBASS),
    MS20_PRESET("SCREAM LEAD",  MS20_P_SCREAMLEAD),
    MS20_PRESET("SYNC LEAD",    MS20_P_SYNCLEAD),
    MS20_PRESET("RING LEAD",    MS20_P_RINGLEAD),
    MS20_PRESET("PWM LEAD",     MS20_P_PWMLEAD),
    MS20_PRESET("VOWEL LEAD",   MS20_P_VOWELLEAD),
    MS20_PRESET("PITCH SWOOP",  MS20_P_PITCHSWOOP),
    MS20_PRESET("LASER PLUCK",  MS20_P_LASERPLUCK),
    MS20_PRESET("SIREN VOICE",  MS20_P_SIRENVOICE),
    MS20_PRESET("HOLD PAD",     MS20_P_HOLDPAD),
    MS20_PRESET("GHOST TAIL",   MS20_P_GHOSTTAIL),
    MS20_PRESET("SELF-OSC FX",  MS20_P_SELFOSCFX),
    MS20_PRESET("HPF SCREAM",   MS20_P_HPFSCREAM),
    MS20_PRESET("FILTER SWEEP", MS20_P_FILTERSWEEP),
    MS20_PRESET("NOISE SWEEP",  MS20_P_NOISESWEEP),
    MS20_PRESET("BELL PLUCK",   MS20_P_BELLPLUCK),
    MS20_PRESET("RING CLANG",   MS20_P_RINGCLANG),
    MS20_PRESET("METAL HIT",    MS20_P_METALHIT),
    MS20_PRESET("BRASS STAB",   MS20_P_BRASSSTAB),
    MS20_PRESET("HORN STAB",    MS20_P_HORNSTAB),
    MS20_PRESET("WARM PAD",     MS20_P_WARMPAD),
    MS20_PRESET("HAUNT PAD",    MS20_P_HAUNTPAD),
    MS20_PRESET("MARIMBA",      MS20_P_MARIMBA),
    MS20_PRESET("KALIMBA",      MS20_P_KALIMBA),
    MS20_PRESET("ESP GRIT",     MS20_P_ESPGRIT),
    MS20_PRESET("ALARM FX",     MS20_P_ALARMFX),
    MS20_PRESET("GROWL FX",     MS20_P_GROWLFX),
    MS20_PRESET("ARP CLASSIC",  MS20_P_ARPCLASSIC),
    MS20_PRESET("ARP SCREAM",   MS20_P_ARPSCREAM),
};
#define MS20_NUM_PRESETS (sizeof(MS20_PRESETS) / sizeof(MS20_PRESETS[0]))

static void ms20ApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < MS20_NUM_PARAMS; i++) ms20Raw[i] = (float)MS20_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) ms20Raw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    ms20Decode();
}

static const EnginePageDef MS20_PAGES[] = {
    { "VCO 1/2",    MS20_VCO1_SAW,    7 },   // ...through NOISE
    { "FILTERS",    MS20_HPF_CUTOFF, 8 },   // HPF/LPF x3 + type + kbd
    { "PITCH EG1",  MS20_PENV_HOLD,  6 },
    { "FILT/AMP EG2",MS20_AENV_HOLD, 5 },
    { "VOICING",    MS20_GLIDE_ON,   4 },   // glide x2 + master/bend
    { "OUTPUT",     MS20_TREM_RATE,  8 },
    { "ARP",        MS20_ARP_ENABLE, 8 },
};
#define MS20_NUM_PAGES (sizeof(MS20_PAGES) / sizeof(MS20_PAGES[0]))

uint8_t ms20MasterParamIdE() { return MS20_MASTER; }
// The front-panel pots drive the LOWPASS section, not the highpass: the MS-20
// has two independently resonant filters in series (ms20.h's file banner), and
// it is the LPF whose PEAK is the sound the instrument is known for.
uint8_t ms20FilterCutoffParamIdE()    { return MS20_LPF_CUTOFF; }
uint8_t ms20FilterResonanceParamIdE() { return MS20_LPF_RES; }
uint8_t ms20NumPagesE() { return (uint8_t)MS20_NUM_PAGES; }
const EnginePageDef* ms20PageE(uint8_t idx) { return (idx < MS20_NUM_PAGES) ? &MS20_PAGES[idx] : &MS20_PAGES[0]; }

// ─── Voice ──────────────────────────────────────────────────────────────
static Ms20Voice ms20v;
extern VoiceAlloc va;

void ms20EngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void ms20EngineActivate() {
    ms20v = Ms20Voice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void ms20ParamsInitE() { ms20ApplyDeltas(nullptr, 0); }
void ms20SetParameterE(uint8_t id, float v) { if (id < MS20_NUM_PARAMS) { ms20Raw[id] = v; ms20Decode(); } }
float ms20GetParameterE(uint8_t id) { return (id < MS20_NUM_PARAMS) ? ms20Raw[id] : 0.0f; }
void ms20FormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= MS20_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = MS20_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(ms20Raw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(ms20Raw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool ms20ParamIsLockableE(uint8_t id) { return id < MS20_NUM_PARAMS; }
uint8_t ms20NumParamsE() { return MS20_NUM_PARAMS; }
static const ParamInfo MS20_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* ms20ParamInfoE(uint8_t id) {
    return (id < MS20_NUM_PARAMS) ? &MS20_PARAM_INFO[id] : &MS20_INVALID_PARAM;
}
uint8_t ms20NumPresetsE() { return (uint8_t)MS20_NUM_PRESETS; }
const char* ms20PresetNameE(uint8_t idx) { return MS20_PRESETS[idx].name; }
void ms20LoadPresetE(uint8_t idx) { ms20ApplyDeltas(MS20_PRESETS[idx].deltas, MS20_PRESETS[idx].n); }
void ms20LoadInitPatchE() { ms20ApplyDeltas(nullptr, 0); }
void ms20SnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < MS20_NUM_PARAMS; i++) out[i] = (uint8_t)(ms20Raw[i] * 255.0f + 0.5f);
}
void ms20RestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < MS20_NUM_PARAMS; i++) ms20Raw[i] = (float)in[i] * (1.0f / 255.0f);
    ms20Decode();
}

uint8_t ms20NumVoicesE() { return MS20_VOICES; }
void    ms20VoiceInitE() { ms20EngineActivate(); }

void ms20NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    (void)voiceIdx;
    float freq = noteToFreq(note);
    ms20v.note = note; ms20v.velocity = vel; ms20v.velNorm = (float)vel * (1.0f / 127.0f);
    ms20v.gate = true;
    ms20v.targetFreq = freq;
    if (ms20.glideOn && ms20v.currentFreq > 20.0f) {
        ms20v.gliding = true;
        ms20v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (ms20.glideTimeS * SAMPLE_RATE_F);
        if (ms20v.glideCoef > 1.0f) ms20v.glideCoef = 1.0f;
    } else {
        ms20v.currentFreq = freq;
        ms20v.gliding = false;
    }
    if (trigEnv) {
        ms20v.pitchEnv.trigger(ms20.pA, ms20.pD, ms20.pS, ms20.pR, (uint32_t)(ms20.pHoldS * SAMPLE_RATE_F));
        ms20v.filtAmpEnv.trigger(ms20.aA, ms20.aD, ms20.aS, ms20.aR, (uint32_t)(ms20.aHoldS * SAMPLE_RATE_F));
    }
}
void ms20NoteOffE(uint8_t voiceIdx) {
    (void)voiceIdx;
    ms20v.gate = false;
    ms20v.pitchEnv.release();
    ms20v.filtAmpEnv.release();
}
void ms20AllOffE() { ms20NoteOffE(0); }
void ms20SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    (void)voiceIdx; ms20v.aftertouch = (float)pressure * (1.0f / 127.0f);
}

int ms20RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch; (void)modWheel;
    const float bendMul = fastPow2(pitchBend * ms20.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = ms20.tremMix > 0.004f;
    const bool fxDelay  = ms20.dlyMix  > 0.004f;
    const bool fxReverb = ms20.revMix  > 0.004f;
    g_fxChorus.setRate(ms20.tremRate);
    g_fxReverb.setRoom(ms20.revSize, 0.4f);
    g_fxDelay.setTimeMs(ms20.dlyTimeMs, false);

    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        const bool alive = ms20v.filtAmpEnv.stage != MS20_E_IDLE;

        float out = 0.0f;
        if (alive) {
            // EG2 -> both filter cutoffs AND the VCA -- see file banner
            // point 1. This has to be exactly ONE process() call per
            // sample: EG2 is a single stateful envelope shared by both
            // destinations, and the VCA side below needs it every sample
            // for a smooth amplitude. The cutoff side only actually reads
            // envF at control rate (below), the same as every other
            // modulation source in this codebase -- it does not need (and
            // must not take) a second call of its own.
            const float envF = ms20v.filtAmpEnv.process();

            if (ctrl) {
                if (ms20v.gliding) {
                    float d = ms20v.targetFreq - ms20v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { ms20v.currentFreq = ms20v.targetFreq; ms20v.gliding = false; }
                    else ms20v.currentFreq += d * ms20v.glideCoef;
                }
                // EG1 -> PITCH (both VCOs) -- see ms20.h file banner point 1.
                const float pEnvF = ms20v.pitchEnv.process();
                float base = ms20v.currentFreq * bendMul * fastPow2((pEnvF * ms20.pEnvAmtSemis) / 12.0f);
                ms20v.osc1.setFreq(base);
                ms20v.osc2.setFreq(base * ms20.v2FineRatio);

                float kbdSemis = ms20.kbdAmt * (float)(ms20v.note - 60);
                float hpfCutoff = ms20.hpfCutoffHz * fastPow2((kbdSemis + ms20.hpfEnvAmt * envF * 48.0f) / 12.0f);
                float lpfCutoff = ms20.lpfCutoffHz * fastPow2((kbdSemis + ms20.lpfEnvAmt * envF * 48.0f) / 12.0f);
                if (hpfCutoff < 20.0f) hpfCutoff = 20.0f; else if (hpfCutoff > 15000.0f) hpfCutoff = 15000.0f;
                if (lpfCutoff < 20.0f) lpfCutoff = 20.0f; else if (lpfCutoff > 15000.0f) lpfCutoff = 15000.0f;
                ms20v.hpf.setCutoffHz(hpfCutoff, ms20.hpfPeak);
                ms20v.lpf.setCutoffHz(lpfCutoff, ms20.lpfPeak);
                if (ms20.filterType != VF_STOCK) ms20v.varFilt.setCutoff(lpfCutoff, ms20.lpfPeak, ms20.filterType);
            }

            // VCO1 hard-syncs VCO2 -- note the direction, flipped from
            // every other sync-equipped engine in this codebase. See file
            // banner point 4.
            bool wrapped1 = false;
            float t1 = ms20v.osc1.advance(&wrapped1);
            float o1raw = ms20v.osc1.saw(t1) * ms20.v1Saw + ms20v.osc1.pulse(t1, ms20.v1Pwm) * ms20.v1Pulse;
            if (ms20.syncOn && wrapped1) ms20v.osc2.resetPhase();
            float t2 = ms20v.osc2.advance();
            float o2saw = ms20v.osc2.saw(t2);
            float o2square = ms20v.osc2.pulse(t2, 0.5f);
            float o2raw;
            switch (ms20.v2Wave) {
            case MS20_V2_SQUARE: o2raw = o2square; break;
            case MS20_V2_RING:   o2raw = o1raw * o2saw; break;   // ring mod replaces VCO2's own waveform
            default:             o2raw = o2saw; break;
            }

            float noiseOut = ms20v.noise.white() * ms20.noiseLvl;
            float voiceIn = (o1raw + o2raw + noiseOut) * 0.5f;

            float filtered;
            if (ms20.filterType == VF_STOCK) {
                filtered = ms20v.lpf.process(ms20v.hpf.process(voiceIn));
            } else {
                float hp = ms20v.hpf.process(voiceIn);
                filtered = ms20v.varFilt.process(hp, ms20.filterType, ms20.lpfPeak);
            }
            float amp = envF;
            active = 1;

            // MASTER is pure linear gain after the saturator, not baked
            // into it — same discipline every engine in this codebase
            // follows. MS20_MIX_DRIVE is a reasoned estimate (mono,
            // 2-oscillator voice into a resonant series filter pair), not
            // a probed one — see obx.cpp's identical caveat.
            const float MS20_MIX_DRIVE = 2.4f;
            out = fastTanh(filtered * MS20_MIX_DRIVE) * amp;
        }
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, ms20.tremDepth, ms20.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, ms20.dlyFb, ms20.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * ms20.revMix; r += rr * ms20.revMix;
        }
        l *= ms20.master; r *= ms20.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    va.busy[0] = active ? 1 : 0;
    return active;
}

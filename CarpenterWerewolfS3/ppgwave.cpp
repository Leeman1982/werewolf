#include "ppgwave.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include "variable_filter.h"
#include <string.h>
#include <math.h>

// ============================================================================
//  ppgwave.cpp — the procedural wavetable ROM, panel-as-data, presets, and
//  the generic engine adapter.
// ============================================================================

// ─── The wavetable ROM — see ppgwave.h's file banner for what these are and
// why they're procedural rather than a factory-ROM dump ───────────────────
#include "bigmem.h"
#if WOLF_BIGMEM
// 48 KB of generated wavetables: PSRAM on the werewolf S3 (bigmem.h),
// allocated by ppgBuildTables() before anything reads it.
static int8_t (*PPG_TABLE)[PPG_WAVES_PER_TABLE][PPG_WAVE_LEN] = nullptr;
#else
static int8_t PPG_TABLE[PPG_NUM_TABLES][PPG_WAVES_PER_TABLE][PPG_WAVE_LEN];
#endif
static bool   ppgTablesBuilt = false;

const char* const PPG_TABLE_NAMES[PPG_NUM_TABLES] = { "HARMONIC", "FORMANT", "PWM SWEEP" };

static inline int8_t ppgQuantize(float x) {
    if (x > 1.0f) x = 1.0f; else if (x < -1.0f) x = -1.0f;
    return (int8_t)(x * 127.0f);
}

void ppgWavetablesInit() {
    if (ppgTablesBuilt) return;
#if WOLF_BIGMEM
    if (!PPG_TABLE)
        PPG_TABLE = (int8_t (*)[PPG_WAVES_PER_TABLE][PPG_WAVE_LEN])
                    bigAlloc((size_t)PPG_NUM_TABLES * PPG_WAVES_PER_TABLE * PPG_WAVE_LEN);
    if (!PPG_TABLE) { g_bigAllocFailed = true; return; }
#endif
    ppgTablesBuilt = true;

    // Table 0: HARMONIC — a brightness sweep, sine (wave 0) through an
    // increasingly saw-rich spectrum (wave 63): 1/h-weighted harmonics, more
    // of them added as wave position rises.
    for (int w = 0; w < PPG_WAVES_PER_TABLE; w++) {
        float bright = (float)w / (float)(PPG_WAVES_PER_TABLE - 1);
        int numHarm = 1 + (int)(bright * 15.0f + 0.5f);
        float buf[PPG_WAVE_LEN];
        float peak = 0.001f;
        for (int s = 0; s < PPG_WAVE_LEN; s++) {
            float th = (float)s / (float)PPG_WAVE_LEN;
            float acc = 0.0f;
            for (int h = 1; h <= numHarm; h++)
                acc += (1.0f / (float)h) * sinf(2.0f * (float)M_PI * (float)h * th);
            buf[s] = acc;
            float a = fabsf(acc); if (a > peak) peak = a;
        }
        float norm = 0.92f / peak;
        for (int s = 0; s < PPG_WAVE_LEN; s++) PPG_TABLE[0][w][s] = ppgQuantize(buf[s] * norm);
    }

    // Table 1: FORMANT — two harmonic-weighted "formant" peaks whose center
    // harmonic number drifts with wave position, a vowel-ish sweep. Only
    // integer harmonics are used — required for a strictly periodic 256-
    // sample cycle, since a fractional-frequency partial would click at the
    // loop point — so the "formants" are weighted clusters of nearby
    // harmonics rather than freely-tunable peak frequencies.
    for (int w = 0; w < PPG_WAVES_PER_TABLE; w++) {
        float t01 = (float)w / (float)(PPG_WAVES_PER_TABLE - 1);
        float f1 = 2.0f + t01 * 6.0f;
        float f2 = 8.0f + t01 * 10.0f;
        float buf[PPG_WAVE_LEN];
        float peak = 0.001f;
        for (int s = 0; s < PPG_WAVE_LEN; s++) {
            float th = (float)s / (float)PPG_WAVE_LEN;
            float acc = 0.0f;
            for (int h = 1; h <= 24; h++) {
                float d1 = ((float)h - f1) / 1.5f;
                float d2 = ((float)h - f2) / 2.5f;
                float amp = (expf(-0.5f * d1 * d1) + 0.7f * expf(-0.5f * d2 * d2)) / (float)h;
                acc += amp * sinf(2.0f * (float)M_PI * (float)h * th);
            }
            buf[s] = acc;
            float a = fabsf(acc); if (a > peak) peak = a;
        }
        float norm = 0.92f / peak;
        for (int s = 0; s < PPG_WAVE_LEN; s++) PPG_TABLE[1][w][s] = ppgQuantize(buf[s] * norm);
    }

    // Table 2: PWM SWEEP — variable duty-cycle pulse, 2% through 98%, with
    // no band-limiting at all (see file banner point 1).
    for (int w = 0; w < PPG_WAVES_PER_TABLE; w++) {
        float duty = 0.02f + ((float)w / (float)(PPG_WAVES_PER_TABLE - 1)) * 0.96f;
        float dcCorrect = 2.0f * duty - 1.0f;
        for (int s = 0; s < PPG_WAVE_LEN; s++) {
            float th = (float)s / (float)PPG_WAVE_LEN;
            float v = ((th < duty) ? 1.0f : -1.0f) - dcCorrect;
            PPG_TABLE[2][w][s] = ppgQuantize(v * 0.9f);
        }
    }
}

float PpgOsc::next() {
    phase += inc;
    uint32_t idx  = phase >> 24;                                   // 0..255
    uint32_t idx2 = (idx + 1) & 0xFFu;
    float frac = (float)(phase & 0x00FFFFFFu) * (1.0f / 16777216.0f);

    float wp = wavePos;
    if (wp < 0.0f) wp = 0.0f; else if (wp > (float)(PPG_WAVES_PER_TABLE - 1)) wp = (float)(PPG_WAVES_PER_TABLE - 1);
    int waveLo = (int)wp;
    int waveHi = (waveLo < PPG_WAVES_PER_TABLE - 1) ? waveLo + 1 : waveLo;
    float waveFrac = wp - (float)waveLo;

    uint8_t tb = table; if (tb >= PPG_NUM_TABLES) tb = PPG_NUM_TABLES - 1;
    const int8_t* rowLo = PPG_TABLE[tb][waveLo];
    const int8_t* rowHi = PPG_TABLE[tb][waveHi];
    float sLo = ((float)rowLo[idx] + ((float)rowLo[idx2] - (float)rowLo[idx]) * frac) * (1.0f / 127.0f);
    float sHi = ((float)rowHi[idx] + ((float)rowHi[idx2] - (float)rowHi[idx]) * frac) * (1.0f / 127.0f);
    return sLo + (sHi - sLo) * waveFrac;
}

enum PpgParamId : uint8_t {
    PPG_OSCA_TABLE = 0, PPG_OSCA_WAVE, PPG_OSCA_LEVEL, PPG_OSCA_OCT,
    PPG_OSCB_TABLE, PPG_OSCB_WAVE, PPG_OSCB_LEVEL, PPG_OSCB_OCT, PPG_OSCB_FINE,
    PPG_WAVEENV_A, PPG_WAVEENV_D, PPG_WAVEENV_AMT,
    PPG_WAVE_KBD_TRACK,
    PPG_LFO_RATE, PPG_LFO_WAVE, PPG_LFO_DELAY, PPG_LFO_TO_PITCH, PPG_LFO_TO_WAVE, PPG_LFO_TO_FILTER,
    PPG_FILTER_CUTOFF, PPG_FILTER_TYPE, PPG_FILTER_RES, PPG_FILTER_ENV_AMT, PPG_FILTER_KBD,
    PPG_FENV_A, PPG_FENV_D, PPG_FENV_S, PPG_FENV_R,
    PPG_AENV_A, PPG_AENV_D, PPG_AENV_S, PPG_AENV_R,
    PPG_GLIDE_ON, PPG_GLIDE_TIME,
    PPG_MASTER, PPG_BEND_RANGE,

    PPG_TREM_RATE, PPG_TREM_DEPTH, PPG_TREM_MIX,
    PPG_DLY_TIME, PPG_DLY_FB, PPG_DLY_MIX, PPG_REV_SIZE, PPG_REV_MIX,

    PPG_ARP_ENABLE, PPG_ARP_PATTERN, PPG_ARP_RATE, PPG_ARP_GATE,
    PPG_ARP_OCTAVES, PPG_ARP_DIR, PPG_ARP_LATCH, PPG_ARP_SYNC,

    PPG_NUM_PARAMS
};

static const char* const PPG_ONOFF[2]     = { "OFF", "ON" };
static const char* const PPG_OCT_NAMES[5] = { "32'", "16'", "8'", "4'", "2'" };
static const char* const PPG_LFO_WAVE_NAMES[3] = { "TRIANGLE", "SQUARE", "S&H" };
static const char* const PPG_KBD_NAMES[3] = { "OFF", "HALF", "FULL" };
static const char* const PPG_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define PPGROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [PPG_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo PPG_PARAM_INFO[] = {
    PPGROW("A TABLE", PK_ENUM,   PPG_TABLE_NAMES, PPG_NUM_TABLES, 0, PPG_NUM_TABLES - 1, 20),
    PPGROW("A WAVE",  PK_CONT,   nullptr, 0,    0, 63,    21),
    PPGROW("A LEVEL", PK_CONT,   nullptr, 0,    0, 127,   22),
    PPGROW("A OCT",   PK_ENUM,   PPG_OCT_NAMES, 5, 0, 4,  23),
    PPGROW("B TABLE", PK_ENUM,   PPG_TABLE_NAMES, PPG_NUM_TABLES, 0, PPG_NUM_TABLES - 1, 24),
    PPGROW("B WAVE",  PK_CONT,   nullptr, 0,    0, 63,    25),
    PPGROW("B LEVEL", PK_CONT,   nullptr, 0,    0, 127,   26),
    PPGROW("B OCT",   PK_ENUM,   PPG_OCT_NAMES, 5, 0, 4,  27),
    PPGROW("B FINE",  PK_CENTS,  nullptr, 0,  -700, 700,  28),
    PPGROW("WAVE ATK",PK_MS,     nullptr, 0,    1, 8000,  29),
    PPGROW("WAVE DEC",PK_MS,     nullptr, 0,    3, 12000, 30),
    PPGROW("WAVE AMT",PK_CONT,   nullptr, 0,    0, 127,   31),
    PPGROW("WAVE KBD",PK_TOGGLE, PPG_ONOFF, 2,  0, 1,     33),
    PPGROW("LFO RATE",PK_HZ,     nullptr, 0,    0, 20,    34),
    PPGROW("LFO WAVE",PK_ENUM,   PPG_LFO_WAVE_NAMES, 3, 0, 2, 35),
    PPGROW("LFO DELAY",PK_MS,    nullptr, 0,    0, 4000,  36),
    PPGROW("LFO>PITCH",PK_CONT,  nullptr, 0,    0, 127,   37),
    PPGROW("LFO>WAVE", PK_CONT,  nullptr, 0,    0, 127,   39),
    PPGROW("LFO>VCF",  PK_CONT,  nullptr, 0,    0, 127,   40),
    PPGROW("CUTOFF",   PK_HZ,    nullptr, 0,   20, 18000, 41),
    PPGROW("FILT TYPE",PK_ENUM,  VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 42),
    PPGROW("RESONANCE",PK_CONT,  nullptr, 0,    0, 127,   43),
    PPGROW("VCF ENV AMT",PK_CONT,nullptr, 0,    0, 127,   44),
    PPGROW("KBD AMT",  PK_ENUM,  PPG_KBD_NAMES, 3,0, 2,   45),
    PPGROW("VCF ATK",  PK_MS,    nullptr, 0,    1, 8000,  46),
    PPGROW("VCF DEC",  PK_MS,    nullptr, 0,    3, 12000, 47),
    PPGROW("VCF SUS",  PK_CONT,  nullptr, 0,    0, 127,   48),
    PPGROW("VCF REL",  PK_MS,    nullptr, 0,    3, 12000, 49),
    PPGROW("VCA ATK",  PK_MS,    nullptr, 0,    1, 8000,  50),
    PPGROW("VCA DEC",  PK_MS,    nullptr, 0,    3, 12000, 51),
    PPGROW("VCA SUS",  PK_CONT,  nullptr, 0,    0, 127,   52),
    PPGROW("VCA REL",  PK_MS,    nullptr, 0,    3, 12000, 53),
    PPGROW("GLIDE",    PK_TOGGLE,PPG_ONOFF, 2,  0, 1,     54),
    PPGROW("GLIDE TIME",PK_MS,   nullptr, 0,    0, 5000,  55),
    PPGROW("MASTER",   PK_CONT,  nullptr, 0,    0, 127,   56),
    PPGROW("BEND RNG", PK_INT,   nullptr, 0,    1, 24,    57),
    PPGROW("TREM RATE",PK_HZ,    nullptr, 0,    0, 20,    58),
    PPGROW("TREM DEPTH",PK_CONT, nullptr, 0,    0, 127,   59),
    PPGROW("TREM MIX", PK_CONT,  nullptr, 0,    0, 127,   60),
    PPGROW("DLY TIME", PK_MS,    nullptr, 0,   20, 1200,  61),
    PPGROW("DLY FDBK", PK_CONT,  nullptr, 0,    0, 127,   62),
    PPGROW("DLY MIX",  PK_CONT,  nullptr, 0,    0, 127,   63),
    PPGROW("REV SIZE", PK_CONT,  nullptr, 0,    0, 127,   65),
    PPGROW("REV MIX",  PK_CONT,  nullptr, 0,    0, 127,   66),
    PPGROW("ARP ON",   PK_TOGGLE,PPG_ONOFF, 2,  0, 1,     67),
    PPGROW("ARP PATT", PK_INT,   nullptr, 0,    0, 7,     68),
    PPGROW("ARP RATE", PK_CONT,  nullptr, 0,    0, 127,   69),
    PPGROW("ARP GATE", PK_CONT,  nullptr, 0,    0, 127,   70),
    PPGROW("ARP OCT",  PK_INT,   nullptr, 0,    1, 4,     71),
    PPGROW("ARP DIR",  PK_ENUM,  PPG_ARP_DIR_NAMES,6,0,5, 72),
    PPGROW("ARP LATCH",PK_TOGGLE,PPG_ONOFF, 2,  0, 1,     73),
    PPGROW("ARP SYNC", PK_TOGGLE,PPG_ONOFF, 2,  0, 1,     74),
};
static_assert(sizeof(PPG_PARAM_INFO) / sizeof(PPG_PARAM_INFO[0]) == PPG_NUM_PARAMS,
              "PPG_PARAM_INFO must have exactly PPG_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float ppgRaw[PPG_NUM_PARAMS];

struct PpgDecoded {
    uint8_t aTable; float aWave, aLevel; float aOctMul;
    uint8_t bTable; float bWave, bLevel; float bOctMul, bFineRatio;
    float waveA, waveD, waveAmt;
    bool waveKbdTrack;
    float lfoRateHz; uint8_t lfoWave; float lfoDelayS, lfoToPitch, lfoToWave, lfoToFilter;
    float cutoffHz; uint8_t filterType; float resonance, envAmt, kbdAmt;
    float fA, fD, fS, fR, aA, aD, aS, aR;
    bool glideOn; float glideTimeS;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static PpgDecoded ppg;

static inline float ppgOctMulFromEnum(float v) {
    static const float TAB[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };
    int i = (int)(v * 4.999f); if (i < 0) i = 0; else if (i > 4) i = 4;
    return TAB[i];
}

static void ppgDecode() {
    ppg.aTable = (uint8_t)(ppgRaw[PPG_OSCA_TABLE] * ((float)PPG_NUM_TABLES - 0.001f));
    ppg.aWave  = ppgRaw[PPG_OSCA_WAVE] * 63.0f;
    ppg.aLevel = ppgRaw[PPG_OSCA_LEVEL];
    ppg.aOctMul = ppgOctMulFromEnum(ppgRaw[PPG_OSCA_OCT]);

    ppg.bTable = (uint8_t)(ppgRaw[PPG_OSCB_TABLE] * ((float)PPG_NUM_TABLES - 0.001f));
    ppg.bWave  = ppgRaw[PPG_OSCB_WAVE] * 63.0f;
    ppg.bLevel = ppgRaw[PPG_OSCB_LEVEL];
    ppg.bOctMul = ppgOctMulFromEnum(ppgRaw[PPG_OSCB_OCT]);
    ppg.bFineRatio = fastPow2(((ppgRaw[PPG_OSCB_FINE] * 2.0f - 1.0f) * 7.0f) / 12.0f);

    ppg.waveA = 1.0f / (1.0f + ppgRaw[PPG_WAVEENV_A] * 800.0f);
    ppg.waveD = 1.0f / (1.0f + ppgRaw[PPG_WAVEENV_D] * 1600.0f);
    ppg.waveAmt = ppgRaw[PPG_WAVEENV_AMT];
    ppg.waveKbdTrack = ppgRaw[PPG_WAVE_KBD_TRACK] > 0.5f;

    ppg.lfoRateHz = 0.1f + ppgRaw[PPG_LFO_RATE] * 19.9f;
    ppg.lfoWave = (uint8_t)(ppgRaw[PPG_LFO_WAVE] * 2.999f);
    ppg.lfoDelayS = ppgRaw[PPG_LFO_DELAY] * 4.0f;
    ppg.lfoToPitch = ppgRaw[PPG_LFO_TO_PITCH];
    ppg.lfoToWave = ppgRaw[PPG_LFO_TO_WAVE];
    ppg.lfoToFilter = ppgRaw[PPG_LFO_TO_FILTER];

    ppg.cutoffHz = 20.0f * fastPow2(ppgRaw[PPG_FILTER_CUTOFF] * 9.8f);
    ppg.filterType = (uint8_t)(ppgRaw[PPG_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    ppg.resonance = ppgRaw[PPG_FILTER_RES];
    ppg.envAmt = ppgRaw[PPG_FILTER_ENV_AMT];
    { static const float KBD_TAB[3] = { 0.0f, 0.5f, 1.0f };
      int i = (int)(ppgRaw[PPG_FILTER_KBD] * 2.999f); if (i < 0) i = 0; else if (i > 2) i = 2;
      ppg.kbdAmt = KBD_TAB[i]; }

    ppg.fA = 1.0f / (1.0f + ppgRaw[PPG_FENV_A] * 800.0f);
    ppg.fD = 1.0f / (1.0f + ppgRaw[PPG_FENV_D] * 1600.0f);
    ppg.fS = ppgRaw[PPG_FENV_S];
    ppg.fR = 1.0f / (1.0f + ppgRaw[PPG_FENV_R] * 1600.0f);
    ppg.aA = 1.0f / (1.0f + ppgRaw[PPG_AENV_A] * 800.0f);
    ppg.aD = 1.0f / (1.0f + ppgRaw[PPG_AENV_D] * 1600.0f);
    ppg.aS = ppgRaw[PPG_AENV_S];
    ppg.aR = 1.0f / (1.0f + ppgRaw[PPG_AENV_R] * 1600.0f);

    ppg.glideOn = ppgRaw[PPG_GLIDE_ON] > 0.5f;
    ppg.glideTimeS = 0.005f + ppgRaw[PPG_GLIDE_TIME] * 5.0f;
    ppg.master = ppgRaw[PPG_MASTER];
    ppg.bendRangeSemis = 1.0f + ppgRaw[PPG_BEND_RANGE] * 23.0f;

    ppg.tremRate = ppgRaw[PPG_TREM_RATE] * 20.0f;
    ppg.tremDepth = ppgRaw[PPG_TREM_DEPTH];
    ppg.tremMix = ppgRaw[PPG_TREM_MIX];
    ppg.dlyTimeMs = 20.0f + ppgRaw[PPG_DLY_TIME] * 1180.0f;
    ppg.dlyFb = ppgRaw[PPG_DLY_FB];
    ppg.dlyMix = ppgRaw[PPG_DLY_MIX];
    ppg.revSize = ppgRaw[PPG_REV_SIZE];
    ppg.revMix = ppgRaw[PPG_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) — every PK_ENUM/PK_TOGGLE below that isn't
// option 0 goes through it, same discipline as every other engine's table.
static const uint8_t PPG_INIT[] = {
    0, 20, 110, EI(2,5),           // OSC A: HARMONIC table, wave~10, level, 8'
    0, 40, 90, EI(2,5), 66,         // OSC B: HARMONIC, wave~20, level, 8', a few cents sharp
    30, 60, 0,                      // wave env A/D/amt (off by default)
    0,                               // wave kbd track
    20, 0, 0, 0, 0, 0,               // LFO rate/wave/delay/topitch/towave/tofilter
    90, 0, 20, 60, EI(2,3),          // filter cutoff/type/res/envamt/kbd(FULL)
    15, 55, 90, 30,                  // filter env ADSR
    5, 65, 110, 40,                  // amp env ADSR
    0, 64,                           // glide on/time
    110, 20,                         // master/bend
    0,0,0, 0,0,0,0,0,                // output
    0,0,64,80,0,0,0,0,               // arp
};
static_assert(sizeof(PPG_INIT) / sizeof(PPG_INIT[0]) == PPG_NUM_PARAMS,
              "PPG_INIT must have exactly PPG_NUM_PARAMS values");

#define PPG_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses ──
static const PresetDelta PPG_P_DIGIBASS[] = { {PPG_OSCA_TABLE,EI(2,3)},{PPG_OSCA_WAVE,10},{PPG_OSCB_LEVEL,0},{PPG_FILTER_CUTOFF,30},{PPG_FILTER_RES,35},{PPG_FILTER_ENV_AMT,90},{PPG_FENV_A,0},{PPG_FENV_D,25},{PPG_FENV_S,0},{PPG_AENV_D,25},{PPG_AENV_S,20} };
static const PresetDelta PPG_P_WAVEBASS[] = { {PPG_OSCA_WAVE,5},{PPG_WAVEENV_AMT,60},{PPG_WAVEENV_A,0},{PPG_WAVEENV_D,20},{PPG_OSCB_LEVEL,0},{PPG_FILTER_CUTOFF,35},{PPG_FILTER_RES,40},{PPG_AENV_D,30},{PPG_AENV_S,30} };
static const PresetDelta PPG_P_SUBBASS[] = { {PPG_OSCA_TABLE,0},{PPG_OSCA_WAVE,0},{PPG_OSCB_LEVEL,0},{PPG_FILTER_CUTOFF,16},{PPG_FILTER_RES,15},{PPG_AENV_D,55},{PPG_AENV_S,100} };
static const PresetDelta PPG_P_GRAINBASS[] = { {PPG_OSCA_TABLE,EI(2,3)},{PPG_OSCA_WAVE,60},{PPG_OSCB_TABLE,EI(2,3)},{PPG_OSCB_WAVE,20},{PPG_OSCB_FINE,80},{PPG_FILTER_CUTOFF,30},{PPG_FILTER_RES,55},{PPG_FILTER_ENV_AMT,80},{PPG_FENV_D,30} };
static const PresetDelta PPG_P_FMYBASS[] = { {PPG_OSCA_WAVE,45},{PPG_OSCB_WAVE,15},{PPG_OSCB_FINE,90},{PPG_FILTER_CUTOFF,25},{PPG_FILTER_RES,60},{PPG_FILTER_ENV_AMT,100},{PPG_FENV_D,20} };

// ── Leads ──
static const PresetDelta PPG_P_DIGILEAD[] = { {PPG_OSCA_WAVE,50},{PPG_OSCB_WAVE,50},{PPG_OSCB_FINE,80},{PPG_FILTER_CUTOFF,95},{PPG_FILTER_RES,50} };
static const PresetDelta PPG_P_WAVESWEEPLEAD[] = { {PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,110},{PPG_WAVEENV_A,20},{PPG_WAVEENV_D,80},{PPG_FILTER_CUTOFF,85},{PPG_FILTER_RES,60} };
static const PresetDelta PPG_P_FORMANTLEAD[] = { {PPG_OSCA_TABLE,EI(1,3)},{PPG_OSCA_WAVE,20},{PPG_OSCB_TABLE,EI(1,3)},{PPG_OSCB_WAVE,45},{PPG_OSCB_FINE,72},{PPG_FILTER_CUTOFF,70},{PPG_FILTER_RES,40} };
static const PresetDelta PPG_P_PWMLEAD[] = { {PPG_OSCA_TABLE,EI(2,3)},{PPG_OSCA_WAVE,32},{PPG_LFO_TO_WAVE,25},{PPG_LFO_RATE,20},{PPG_FILTER_CUTOFF,75} };
static const PresetDelta PPG_P_GLASSLEAD[] = { {PPG_OSCA_WAVE,8},{PPG_OSCB_WAVE,55},{PPG_OSCB_FINE,110},{PPG_FILTER_CUTOFF,100},{PPG_FILTER_RES,70} };
static const PresetDelta PPG_P_SCREAMLEAD[] = { {PPG_OSCA_WAVE,63},{PPG_OSCB_WAVE,60},{PPG_OSCB_FINE,90},{PPG_FILTER_CUTOFF,105},{PPG_FILTER_RES,110},{PPG_FILTER_ENV_AMT,90},{PPG_FENV_A,0},{PPG_FENV_D,45} };

// ── Pads/strings ──
static const PresetDelta PPG_P_DIGIPAD[] = { {PPG_OSCA_WAVE,15},{PPG_OSCB_WAVE,25},{PPG_OSCB_FINE,72},{PPG_FILTER_CUTOFF,55},{PPG_FENV_A,60},{PPG_FENV_D,90},{PPG_AENV_A,70},{PPG_AENV_D,100},{PPG_AENV_S,95},{PPG_AENV_R,90} };
static const PresetDelta PPG_P_FORMANTPAD[] = { {PPG_OSCA_TABLE,EI(1,3)},{PPG_OSCA_WAVE,10},{PPG_OSCB_TABLE,EI(1,3)},{PPG_OSCB_WAVE,50},{PPG_OSCB_FINE,68},{PPG_FILTER_CUTOFF,50},{PPG_AENV_A,90},{PPG_AENV_S,105} };
static const PresetDelta PPG_P_EVOLVEPAD[] = { {PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,90},{PPG_WAVEENV_A,110},{PPG_WAVEENV_D,127},{PPG_LFO_TO_WAVE,20},{PPG_LFO_RATE,8},{PPG_FILTER_CUTOFF,45},{PPG_AENV_A,110},{PPG_AENV_S,105} };
static const PresetDelta PPG_P_KBDSWEEPPAD[] = { {PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,100},{PPG_WAVEENV_A,10},{PPG_WAVEENV_D,90},{PPG_WAVE_KBD_TRACK,EI(1,2)},{PPG_FILTER_CUTOFF,55},{PPG_AENV_A,60},{PPG_AENV_S,100} };
static const PresetDelta PPG_P_SPACEPAD[] = { {PPG_OSCA_WAVE,30},{PPG_OSCB_WAVE,10},{PPG_OSCB_FINE,78},{PPG_LFO_TO_PITCH,15},{PPG_LFO_RATE,10},{PPG_FILTER_CUTOFF,50},{PPG_AENV_A,120},{PPG_AENV_S,110},{PPG_DLY_MIX,30},{PPG_DLY_TIME,60},{PPG_REV_MIX,60},{PPG_REV_SIZE,120} };

// ── Bells/plucks ──
static const PresetDelta PPG_P_DIGIBELL[] = { {PPG_OSCA_WAVE,55},{PPG_OSCB_WAVE,20},{PPG_OSCB_FINE,110},{PPG_FILTER_CUTOFF,95},{PPG_FILTER_ENV_AMT,50},{PPG_FENV_D,45},{PPG_AENV_D,50},{PPG_AENV_S,0} };
static const PresetDelta PPG_P_WAVEPLUCK[] = { {PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,127},{PPG_WAVEENV_A,0},{PPG_WAVEENV_D,25},{PPG_FILTER_CUTOFF,90},{PPG_AENV_D,30},{PPG_AENV_S,0} };
static const PresetDelta PPG_P_FORMANTPLUCK[] = { {PPG_OSCA_TABLE,EI(1,3)},{PPG_OSCA_WAVE,35},{PPG_FILTER_CUTOFF,80},{PPG_FILTER_ENV_AMT,60},{PPG_FENV_D,20},{PPG_AENV_D,22},{PPG_AENV_S,0} };

// ── SFX / wildly digital ──
static const PresetDelta PPG_P_ALIASFX[] = { {PPG_OSCA_TABLE,0},{PPG_OSCA_WAVE,63},{PPG_OSCA_OCT,EI(4,5)},{PPG_OSCB_LEVEL,0},{PPG_FILTER_CUTOFF,127},{PPG_AENV_A,0},{PPG_AENV_S,127} };
static const PresetDelta PPG_P_SHWAVEFX[] = { {PPG_OSCA_WAVE,0},{PPG_LFO_WAVE,EI(2,3)},{PPG_LFO_TO_WAVE,110},{PPG_LFO_RATE,110},{PPG_FILTER_CUTOFF,80},{PPG_AENV_A,0},{PPG_AENV_S,127} };
static const PresetDelta PPG_P_FORMANTVOICE[] = { {PPG_OSCA_TABLE,EI(1,3)},{PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,127},{PPG_WAVEENV_A,90},{PPG_WAVEENV_D,110},{PPG_FILTER_CUTOFF,70},{PPG_AENV_A,60},{PPG_AENV_S,100} };
static const PresetDelta PPG_P_SIRENFX[] = { {PPG_OSCA_WAVE,50},{PPG_OSCB_WAVE,50},{PPG_OSCB_FINE,127},{PPG_LFO_TO_PITCH,90},{PPG_LFO_RATE,15},{PPG_FILTER_CUTOFF,80},{PPG_AENV_A,20},{PPG_AENV_S,127} };
static const PresetDelta PPG_P_NOISEWAVE[] = { {PPG_OSCA_TABLE,EI(2,3)},{PPG_OSCA_WAVE,63},{PPG_OSCB_TABLE,EI(2,3)},{PPG_OSCB_WAVE,0},{PPG_OSCB_FINE,127},{PPG_FILTER_CUTOFF,60},{PPG_FILTER_RES,30},{PPG_AENV_A,40},{PPG_AENV_S,80} };

// ── Vibrato / LFO-delay showcase (see ppgwave.h file banner point 4) ──
static const PresetDelta PPG_P_DELAYVIB[] = { {PPG_OSCA_WAVE,20},{PPG_OSCB_WAVE,20},{PPG_OSCB_FINE,68},{PPG_LFO_TO_PITCH,60},{PPG_LFO_RATE,25},{PPG_LFO_DELAY,60},{PPG_FILTER_CUTOFF,65},{PPG_AENV_A,20},{PPG_AENV_S,100} };
static const PresetDelta PPG_P_HORRORVOX[] = { {PPG_OSCA_TABLE,EI(1,3)},{PPG_OSCA_WAVE,5},{PPG_WAVEENV_AMT,80},{PPG_WAVEENV_A,50},{PPG_WAVEENV_D,100},{PPG_LFO_TO_PITCH,25},{PPG_LFO_DELAY,90},{PPG_LFO_RATE,18},{PPG_FILTER_CUTOFF,55},{PPG_AENV_A,70},{PPG_AENV_S,95},{PPG_REV_MIX,45} };

// ── Arp ──
static const PresetDelta PPG_P_ARPCLASSIC[] = { {PPG_OSCA_WAVE,25},{PPG_FILTER_CUTOFF,60},{PPG_FILTER_RES,40},{PPG_ARP_ENABLE,EI(1,2)},{PPG_ARP_RATE,80},{PPG_ARP_GATE,70} };
static const PresetDelta PPG_P_ARPWAVE[] = { {PPG_OSCA_WAVE,0},{PPG_WAVEENV_AMT,100},{PPG_WAVEENV_A,0},{PPG_WAVEENV_D,15},{PPG_FILTER_CUTOFF,70},{PPG_ARP_ENABLE,EI(1,2)},{PPG_ARP_RATE,95},{PPG_ARP_GATE,85},{PPG_ARP_OCTAVES,48},{PPG_ARP_DIR,EI(2,6)} };

static const Preset PPG_PRESETS[] = {
    PPG_PRESET("DIGI BASS",     PPG_P_DIGIBASS),
    PPG_PRESET("WAVE BASS",     PPG_P_WAVEBASS),
    PPG_PRESET("SUB BASS",      PPG_P_SUBBASS),
    PPG_PRESET("GRAIN BASS",    PPG_P_GRAINBASS),
    PPG_PRESET("FMY BASS",      PPG_P_FMYBASS),
    PPG_PRESET("DIGI LEAD",     PPG_P_DIGILEAD),
    PPG_PRESET("WAVE SWEEP LEAD",PPG_P_WAVESWEEPLEAD),
    PPG_PRESET("FORMANT LEAD",  PPG_P_FORMANTLEAD),
    PPG_PRESET("PWM LEAD",      PPG_P_PWMLEAD),
    PPG_PRESET("GLASS LEAD",    PPG_P_GLASSLEAD),
    PPG_PRESET("SCREAM LEAD",   PPG_P_SCREAMLEAD),
    PPG_PRESET("DIGI PAD",      PPG_P_DIGIPAD),
    PPG_PRESET("FORMANT PAD",   PPG_P_FORMANTPAD),
    PPG_PRESET("EVOLVE PAD",    PPG_P_EVOLVEPAD),
    PPG_PRESET("KBD SWEEP PAD", PPG_P_KBDSWEEPPAD),
    PPG_PRESET("SPACE PAD",     PPG_P_SPACEPAD),
    PPG_PRESET("DIGI BELL",     PPG_P_DIGIBELL),
    PPG_PRESET("WAVE PLUCK",    PPG_P_WAVEPLUCK),
    PPG_PRESET("FORMANT PLUCK", PPG_P_FORMANTPLUCK),
    PPG_PRESET("ALIAS FX",      PPG_P_ALIASFX),
    PPG_PRESET("S&H WAVE FX",   PPG_P_SHWAVEFX),
    PPG_PRESET("FORMANT VOICE", PPG_P_FORMANTVOICE),
    PPG_PRESET("SIREN FX",      PPG_P_SIRENFX),
    PPG_PRESET("NOISE WAVE",    PPG_P_NOISEWAVE),
    PPG_PRESET("DELAY VIBRATO", PPG_P_DELAYVIB),
    PPG_PRESET("HORROR VOX",    PPG_P_HORRORVOX),
    PPG_PRESET("ARP CLASSIC",   PPG_P_ARPCLASSIC),
    PPG_PRESET("ARP WAVE",      PPG_P_ARPWAVE),
};
#define PPG_NUM_PRESETS (sizeof(PPG_PRESETS) / sizeof(PPG_PRESETS[0]))

static void ppgApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < PPG_NUM_PARAMS; i++) ppgRaw[i] = (float)PPG_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) ppgRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    ppgDecode();
}

static const EnginePageDef PPG_PAGES[] = {
    { "OSC A/B",    PPG_OSCA_TABLE,   9 },
    { "WAVE ENV",   PPG_WAVEENV_A,    4 },
    { "LFO",        PPG_LFO_RATE,     6 },
    { "FILTER",     PPG_FILTER_CUTOFF,5 },
    { "ENVELOPES",  PPG_FENV_A,       8 },
    { "VOICING",    PPG_GLIDE_ON,     4 },
    { "OUTPUT",     PPG_TREM_RATE,    8 },
    { "ARP",        PPG_ARP_ENABLE,   8 },
};
#define PPG_NUM_PAGES (sizeof(PPG_PAGES) / sizeof(PPG_PAGES[0]))

uint8_t ppgMasterParamIdE() { return PPG_MASTER; }
uint8_t ppgFilterCutoffParamIdE()    { return PPG_FILTER_CUTOFF; }
uint8_t ppgFilterResonanceParamIdE() { return PPG_FILTER_RES; }
uint8_t ppgNumPagesE() { return (uint8_t)PPG_NUM_PAGES; }
const EnginePageDef* ppgPageE(uint8_t idx) { return (idx < PPG_NUM_PAGES) ? &PPG_PAGES[idx] : &PPG_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static PpgVoice ppgv[PPG_VOICES];
extern VoiceAlloc va;

void ppgEngineInit() {
    ppgWavetablesInit();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}
void ppgEngineActivate() {
    ppgWavetablesInit();
    for (int i = 0; i < PPG_VOICES; i++) ppgv[i] = PpgVoice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void ppgParamsInitE() { ppgApplyDeltas(nullptr, 0); }
void ppgSetParameterE(uint8_t id, float v) { if (id < PPG_NUM_PARAMS) { ppgRaw[id] = v; ppgDecode(); } }
float ppgGetParameterE(uint8_t id) { return (id < PPG_NUM_PARAMS) ? ppgRaw[id] : 0.0f; }
void ppgFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= PPG_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = PPG_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(ppgRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(ppgRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool ppgParamIsLockableE(uint8_t id) { return id < PPG_NUM_PARAMS; }
uint8_t ppgNumParamsE() { return PPG_NUM_PARAMS; }
static const ParamInfo PPG_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* ppgParamInfoE(uint8_t id) {
    return (id < PPG_NUM_PARAMS) ? &PPG_PARAM_INFO[id] : &PPG_INVALID_PARAM;
}
uint8_t ppgNumPresetsE() { return (uint8_t)PPG_NUM_PRESETS; }
const char* ppgPresetNameE(uint8_t idx) { return PPG_PRESETS[idx].name; }
void ppgLoadPresetE(uint8_t idx) { ppgApplyDeltas(PPG_PRESETS[idx].deltas, PPG_PRESETS[idx].n); }
void ppgLoadInitPatchE() { ppgApplyDeltas(nullptr, 0); }
void ppgSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < PPG_NUM_PARAMS; i++) out[i] = (uint8_t)(ppgRaw[i] * 255.0f + 0.5f);
}
void ppgRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < PPG_NUM_PARAMS; i++) ppgRaw[i] = (float)in[i] * (1.0f / 255.0f);
    ppgDecode();
}

uint8_t ppgNumVoicesE() { return PPG_VOICES; }
void    ppgVoiceInitE() { ppgEngineActivate(); }

void ppgNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= PPG_VOICES) return;
    PpgVoice& v = ppgv[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (ppg.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (ppg.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(ppg.fA, ppg.fD, ppg.fS, ppg.fR);
        v.ampEnv.trigger(ppg.aA, ppg.aD, ppg.aS, ppg.aR);
        // Higher notes sweep proportionally faster when WAVE KBD is on —
        // same keyboard-tracked-timing convention this codebase already
        // applies to cutoff and envelope pitch offsets elsewhere, just
        // applied to a RATE (atkRate/decCoef are per-tick rates, larger =
        // faster) rather than an octave-domain offset, hence multiplying
        // rather than adding.
        float kbdWaveScale = ppg.waveKbdTrack ? fastPow2((float)(note - 60) / 12.0f) : 1.0f;
        v.waveEnv.trigger(ppg.waveA * kbdWaveScale, ppg.waveD * kbdWaveScale);
        v.lfoDelayGain = 0.0f;
    }
}
void ppgNoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= PPG_VOICES) return;
    PpgVoice& v = ppgv[voiceIdx];
    v.gate = false;
    v.filtEnv.release();
    v.ampEnv.release();
}
void ppgAllOffE() { for (int i = 0; i < PPG_VOICES; i++) ppgNoteOffE(i); }
void ppgSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < PPG_VOICES) ppgv[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int ppgRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * ppg.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = ppg.tremMix > 0.004f;
    const bool fxDelay  = ppg.dlyMix  > 0.004f;
    const bool fxReverb = ppg.revMix  > 0.004f;
    g_fxChorus.setRate(ppg.tremRate);
    g_fxReverb.setRoom(ppg.revSize, 0.4f);
    g_fxDelay.setTimeMs(ppg.dlyTimeMs, false);

    static uint32_t lfoPhase = 0;
    const float lfoDelayInc = (ppg.lfoDelayS > 0.001f)
        ? ((float)CONTROL_RATE_DIV / (ppg.lfoDelayS * SAMPLE_RATE_F)) : 1.0f;

    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        lfoPhase += (uint32_t)(ppg.lfoRateHz * PHASE_PER_HZ);
        float lfo;
        switch (ppg.lfoWave) {
        case 1:  lfo = (lfoPhase & 0x80000000u) ? -1.0f : 1.0f; break;
        case 2:  { static float lastLfoSh = 0.0f; static uint32_t prevLfoPh = 0;
                   if (lfoPhase < prevLfoPh) lastLfoSh = 2.0f * phaseToFloat(lfoPhase) - 1.0f;
                   prevLfoPh = lfoPhase; lfo = lastLfoSh; break; }
        default: lfo = (lfoPhase < 0x80000000u) ? (4.0f * phaseToFloat(lfoPhase) - 1.0f)
                                                 : (3.0f - 4.0f * phaseToFloat(lfoPhase)); break;
        }
        // LFO->PITCH and LFO->VCF are wheel-scaled, matching every other
        // engine's own Wheel-Mod convention. LFO->WAVE is deliberately NOT
        // wheel-scaled — see ppgwave.h file banner point 4 (standing in for
        // the real unit's second, independent modulation wheel).
        const float lfoPitchSemis = lfo * ppg.lfoToPitch * modWheel * 2.0f;
        const float lfoFiltSemis  = lfo * ppg.lfoToFilter * modWheel * 24.0f;
        const float lfoWaveAmt    = lfo * ppg.lfoToWave * 63.0f;

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < PPG_VOICES; i++) {
            PpgVoice& v = ppgv[i];
            const bool alive = v.ampEnv.stage != PPG_E_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (ctrl) {
                // lfoDelayInc is sized as "how much to add once per control
                // tick" (it's scaled by CONTROL_RATE_DIV), so the increment
                // itself has to live inside this same ctrl gate -- applying
                // it every sample instead ramped the delay-then-fade-in
                // 8x faster than LFO DELAY's own panel value said it would.
                if (v.lfoDelayGain < 1.0f) { v.lfoDelayGain += lfoDelayInc; if (v.lfoDelayGain > 1.0f) v.lfoDelayGain = 1.0f; }

                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                float driftMul = fastPow2(PPG_VOICE_DETUNE[i] / 1200.0f);
                float pitchSemis = lfoPitchSemis * v.lfoDelayGain;
                float base = v.currentFreq * bendMul * driftMul * fastPow2(pitchSemis / 12.0f);

                v.oscA.setFreq(base * ppg.aOctMul);
                v.oscB.setFreq(base * ppg.bOctMul * ppg.bFineRatio);
                v.oscA.table = ppg.aTable;
                v.oscB.table = ppg.bTable;

                const float waveEnvF = v.waveEnv.process();
                float wp = ppg.waveAmt * waveEnvF * 63.0f + lfoWaveAmt * v.lfoDelayGain;
                v.oscA.wavePos = ppg.aWave + wp; if (v.oscA.wavePos < 0.0f) v.oscA.wavePos = 0.0f; else if (v.oscA.wavePos > 63.0f) v.oscA.wavePos = 63.0f;
                v.oscB.wavePos = ppg.bWave + wp; if (v.oscB.wavePos < 0.0f) v.oscB.wavePos = 0.0f; else if (v.oscB.wavePos > 63.0f) v.oscB.wavePos = 63.0f;

                const float filtEnvF = v.filtEnv.process();
                float kbdSemis = ppg.kbdAmt * (float)(v.note - 60);
                float cutoff = ppg.cutoffHz * fastPow2(PPG_VOICE_FILT[i] +
                    (kbdSemis + ppg.envAmt * filtEnvF * 48.0f + lfoFiltSemis * v.lfoDelayGain) / 12.0f);
                if (cutoff < 20.0f) cutoff = 20.0f; else if (cutoff > 18000.0f) cutoff = 18000.0f;
                if (ppg.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, ppg.resonance);
                else                             v.varFilt.setCutoff(cutoff, ppg.resonance, ppg.filterType);
            }

            float oA = v.oscA.next() * ppg.aLevel;
            float oB = v.oscB.next() * ppg.bLevel;
            float voiceIn = (oA + oB) * 0.5f;

            float filtered = (ppg.filterType == VF_STOCK)
                ? v.filter.process(voiceIn)
                : v.varFilt.process(voiceIn, ppg.filterType, ppg.resonance);
            float amp = v.ampEnv.process();

            mix += filtered * amp * PPG_VOICE_LEVEL[i];
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER is pure linear gain after the saturator, not baked into it
        // — same discipline every engine in this codebase follows.
        // PPG_MIX_DRIVE is a reasoned estimate, not a probed one — see
        // obx.cpp's identical caveat on OBX_MIX_DRIVE.
        const float PPG_MIX_DRIVE = 1.5f;
        float out = fastTanh(mix * PPG_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, ppg.tremDepth, ppg.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, ppg.dlyFb, ppg.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * ppg.revMix; r += rr * ppg.revMix;
        }
        l *= ppg.master; r *= ppg.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

// Per-voice fixed offsets — 8 independent analogue SSM2044/CEM3360 stages
// behind a shared digital front end still drift, same technique as every
// other polyphonic engine in this codebase.
const float PPG_VOICE_DETUNE[PPG_VOICES] = {
    0.0f, -1.1f, 0.8f, -0.6f, 1.3f, -0.9f, 0.5f, -0.7f
};
const float PPG_VOICE_FILT[PPG_VOICES] = {
    0.0f, 0.017f, -0.013f, 0.024f, -0.019f, 0.010f, -0.026f, 0.015f
};
const float PPG_VOICE_LEVEL[PPG_VOICES] = {
    1.000f, 0.982f, 1.018f, 0.991f, 1.011f, 0.976f, 1.022f, 0.995f
};

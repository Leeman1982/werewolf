#include "stringens.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include <string.h>
#include <math.h>

// ============================================================================
//  stringens.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

static inline float seOnePoleG(float fcHz) { return 1.0f - expf(-2.0f * (float)M_PI * fcHz * INV_SAMPLE_RATE); }

// Fixed per-voicing tone colour — see stringens.h file banner point 1. Not
// player-adjustable; every note of a given voicing always uses the same
// coefficient, computed once here from each instrument's real registered
// brightness (violin brightest, contrabass darkest).
static const float SE_VIOLIN_G    = seOnePoleG(8000.0f);
static const float SE_VIOLA_G     = seOnePoleG(5500.0f);
static const float SE_TRUMPET_G   = seOnePoleG(3500.0f);
static const float SE_HORN_G      = seOnePoleG(2800.0f);
static const float SE_CELLO_G     = seOnePoleG(1400.0f);
static const float SE_CONTRABASS_G= seOnePoleG(700.0f);

float SeEnsemble::process(float x, float rateHz, float depth) {
    buf[writePos] = x;
    lfoPhase += (uint32_t)(rateHz * PHASE_PER_HZ);
    const float baseDelaySamples = 5.0f * SAMPLE_RATE_F * 0.001f;    // ~5 ms
    const float modDepthSamples  = depth * 3.0f * SAMPLE_RATE_F * 0.001f; // up to ~3 ms swing

    float wet = 0.0f;
    for (int t = 0; t < 3; t++) {
        // Three taps 120 degrees apart off one shared LFO -- standing in
        // for the real unit's dual-LFO/three-BBD-line design, see file
        // banner point 4.
        uint32_t tapPhase = lfoPhase + (uint32_t)((double)t * (4294967296.0 / 3.0));
        float lfo = sineFromPhase(tapPhase);
        float delaySamples = baseDelaySamples + lfo * modDepthSamples;
        float readPos = (float)writePos - delaySamples;
        while (readPos < 0.0f) readPos += (float)SE_ENS_BUF_SIZE;
        int i0 = (int)readPos % SE_ENS_BUF_SIZE;
        int i1 = (i0 + 1) % SE_ENS_BUF_SIZE;
        float frac = readPos - floorf(readPos);
        wet += buf[i0] + (buf[i1] - buf[i0]) * frac;
    }
    wet *= (1.0f / 3.0f);
    writePos = (writePos + 1) % SE_ENS_BUF_SIZE;
    return x * 0.4f + wet * 0.6f;   // fixed blend -- the real unit has no dry/wet knob either
}

enum SeParamId : uint8_t {
    SE_VIOLIN_LEVEL = 0, SE_VIOLA_LEVEL, SE_TRUMPET_LEVEL, SE_HORN_LEVEL,
    SE_CELLO_LEVEL, SE_CONTRABASS_LEVEL, SE_BASS_FOOTAGE,
    SE_ATTACK, SE_RELEASE,
    SE_ENSEMBLE_RATE, SE_ENSEMBLE_DEPTH,
    SE_FILTER_CUTOFF, SE_FILTER_TYPE, SE_FILTER_RES,
    SE_MASTER, SE_BEND_RANGE,

    SE_TREM_RATE, SE_TREM_DEPTH, SE_TREM_MIX,
    SE_DLY_TIME, SE_DLY_FB, SE_DLY_MIX, SE_REV_SIZE, SE_REV_MIX,

    SE_ARP_ENABLE, SE_ARP_PATTERN, SE_ARP_RATE, SE_ARP_GATE,
    SE_ARP_OCTAVES, SE_ARP_DIR, SE_ARP_LATCH, SE_ARP_SYNC,

    SE_NUM_PARAMS
};

static const char* const SE_ONOFF[2]       = { "OFF", "ON" };
static const char* const SE_FOOTAGE_NAMES[2] = { "8'", "16'" };
static const char* const SE_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define SEROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [SE_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo SE_PARAM_INFO[] = {
    SEROW("VIOLIN",    PK_CONT,   nullptr, 0,   0, 127,  20),
    SEROW("VIOLA",     PK_CONT,   nullptr, 0,   0, 127,  21),
    SEROW("TRUMPET",   PK_CONT,   nullptr, 0,   0, 127,  22),
    SEROW("HORN",      PK_CONT,   nullptr, 0,   0, 127,  23),
    SEROW("CELLO",     PK_CONT,   nullptr, 0,   0, 127,  24),
    SEROW("CONTRABASS",PK_CONT,   nullptr, 0,   0, 127,  25),
    SEROW("BASS FOOT", PK_TOGGLE, SE_FOOTAGE_NAMES, 2, 0, 1, 26),
    SEROW("ATTACK",    PK_MS,     nullptr, 0,   3, 8000,  27),
    SEROW("RELEASE",   PK_MS,     nullptr, 0,   3, 12000, 28),
    SEROW("ENS RATE",  PK_HZ,     nullptr, 0,   0, 10,    29),
    SEROW("ENS DEPTH", PK_CONT,   nullptr, 0,   0, 127,   30),
    SEROW("CUTOFF",    PK_HZ,     nullptr, 0,  20, 18000, 31),
    SEROW("FILT TYPE", PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 33),
    SEROW("RESONANCE", PK_CONT,   nullptr, 0,   0, 127,   34),
    SEROW("MASTER",    PK_CONT,   nullptr, 0,   0, 127,   35),
    SEROW("BEND RNG",  PK_INT,    nullptr, 0,   1, 24,    36),
    SEROW("TREM RATE", PK_HZ,     nullptr, 0,   0, 20,    37),
    SEROW("TREM DEPTH",PK_CONT,   nullptr, 0,   0, 127,   39),
    SEROW("TREM MIX",  PK_CONT,   nullptr, 0,   0, 127,   40),
    SEROW("DLY TIME",  PK_MS,     nullptr, 0,  20, 1200,  41),
    SEROW("DLY FDBK",  PK_CONT,   nullptr, 0,   0, 127,   42),
    SEROW("DLY MIX",   PK_CONT,   nullptr, 0,   0, 127,   43),
    SEROW("REV SIZE",  PK_CONT,   nullptr, 0,   0, 127,   44),
    SEROW("REV MIX",   PK_CONT,   nullptr, 0,   0, 127,   45),
    SEROW("ARP ON",    PK_TOGGLE, SE_ONOFF, 2,  0, 1,     46),
    SEROW("ARP PATT",  PK_INT,    nullptr, 0,   0, 7,     47),
    SEROW("ARP RATE",  PK_CONT,   nullptr, 0,   0, 127,   48),
    SEROW("ARP GATE",  PK_CONT,   nullptr, 0,   0, 127,   49),
    SEROW("ARP OCT",   PK_INT,    nullptr, 0,   1, 4,     50),
    SEROW("ARP DIR",   PK_ENUM,   SE_ARP_DIR_NAMES,6,0,5, 51),
    SEROW("ARP LATCH", PK_TOGGLE, SE_ONOFF, 2,  0, 1,     52),
    SEROW("ARP SYNC",  PK_TOGGLE, SE_ONOFF, 2,  0, 1,     53),
};
static_assert(sizeof(SE_PARAM_INFO) / sizeof(SE_PARAM_INFO[0]) == SE_NUM_PARAMS,
              "SE_PARAM_INFO must have exactly SE_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float seRaw[SE_NUM_PARAMS];

struct SeDecoded {
    float violinLvl, violaLvl, trumpetLvl, hornLvl, celloLvl, contraLvl;
    bool bassFootage16;
    float atkRate, relRate;
    float ensRateHz, ensDepth;
    float cutoffHz; uint8_t filterType; float resonance;
    float master, bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static SeDecoded se;

static void seDecode() {
    se.violinLvl = seRaw[SE_VIOLIN_LEVEL];
    se.violaLvl = seRaw[SE_VIOLA_LEVEL];
    se.trumpetLvl = seRaw[SE_TRUMPET_LEVEL];
    se.hornLvl = seRaw[SE_HORN_LEVEL];
    se.celloLvl = seRaw[SE_CELLO_LEVEL];
    se.contraLvl = seRaw[SE_CONTRABASS_LEVEL];
    se.bassFootage16 = seRaw[SE_BASS_FOOTAGE] > 0.5f;

    se.atkRate = 1.0f / ((0.003f + seRaw[SE_ATTACK] * 8.0f) * SAMPLE_RATE_F);
    se.relRate = 1.0f / ((0.003f + seRaw[SE_RELEASE] * 12.0f) * SAMPLE_RATE_F);

    se.ensRateHz = 0.1f + seRaw[SE_ENSEMBLE_RATE] * 9.9f;
    se.ensDepth = seRaw[SE_ENSEMBLE_DEPTH];

    se.cutoffHz = 20.0f * fastPow2(seRaw[SE_FILTER_CUTOFF] * 9.8f);
    se.filterType = (uint8_t)(seRaw[SE_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    se.resonance = seRaw[SE_FILTER_RES];

    se.master = seRaw[SE_MASTER];
    se.bendRangeSemis = 1.0f + seRaw[SE_BEND_RANGE] * 23.0f;

    se.tremRate = seRaw[SE_TREM_RATE] * 20.0f;
    se.tremDepth = seRaw[SE_TREM_DEPTH];
    se.tremMix = seRaw[SE_TREM_MIX];
    se.dlyTimeMs = 20.0f + seRaw[SE_DLY_TIME] * 1180.0f;
    se.dlyFb = seRaw[SE_DLY_FB];
    se.dlyMix = seRaw[SE_DLY_MIX];
    se.revSize = seRaw[SE_REV_SIZE];
    se.revMix = seRaw[SE_REV_MIX];
}

// ─── Presets ─────────────────────────────────────────────────────────────
// See param_types.h's EI(idx, n) — every PK_ENUM/PK_TOGGLE below that isn't
// option 0 goes through it, same discipline as every other engine's table.
static const uint8_t SE_INIT[] = {
    100, 90, 0, 0,          // violin/viola/trumpet/horn levels
    0, 0, 0,                 // cello/contrabass/footage(8')
    60, 90,                  // attack/release
    30, 70,                  // ensemble rate/depth
    100, 0, 0,                // filter cutoff/type(STOCK)/res
    110, 20,                  // master/bend
    0,0,0, 0,0,0,0,0,         // output
    0,0,64,80,0,0,0,0,        // arp
};
static_assert(sizeof(SE_INIT) / sizeof(SE_INIT[0]) == SE_NUM_PARAMS,
              "SE_INIT must have exactly SE_NUM_PARAMS values");

#define SE_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Classic ensembles ──
static const PresetDelta SE_P_FULLENSEMBLE[] = { {SE_VIOLIN_LEVEL,100},{SE_VIOLA_LEVEL,90},{SE_TRUMPET_LEVEL,50},{SE_HORN_LEVEL,60},{SE_ATTACK,50},{SE_RELEASE,90},{SE_ENSEMBLE_DEPTH,90} };
static const PresetDelta SE_P_VIOLINSSOLO[] = { {SE_VIOLIN_LEVEL,120},{SE_ATTACK,40},{SE_RELEASE,70},{SE_ENSEMBLE_DEPTH,60} };
static const PresetDelta SE_P_VIOLASECTION[] = { {SE_VIOLA_LEVEL,110},{SE_VIOLIN_LEVEL,30},{SE_ATTACK,55},{SE_RELEASE,80},{SE_ENSEMBLE_DEPTH,70} };
static const PresetDelta SE_P_BRASSENSEMBLE[] = { {SE_TRUMPET_LEVEL,110},{SE_HORN_LEVEL,90},{SE_ATTACK,25},{SE_RELEASE,55},{SE_ENSEMBLE_DEPTH,50} };
static const PresetDelta SE_P_HORNSECTION[] = { {SE_HORN_LEVEL,120},{SE_TRUMPET_LEVEL,40},{SE_ATTACK,35},{SE_RELEASE,60},{SE_ENSEMBLE_DEPTH,55} };
static const PresetDelta SE_P_STRINGSANDBRASS[] = { {SE_VIOLIN_LEVEL,80},{SE_VIOLA_LEVEL,70},{SE_TRUMPET_LEVEL,60},{SE_HORN_LEVEL,70},{SE_ATTACK,45},{SE_RELEASE,80},{SE_ENSEMBLE_DEPTH,80} };

// ── With the mono bass coupler (see stringens.h file banner point 2) ──
static const PresetDelta SE_P_STRINGSWBASS[] = { {SE_VIOLIN_LEVEL,90},{SE_VIOLA_LEVEL,80},{SE_CELLO_LEVEL,100},{SE_ATTACK,45},{SE_RELEASE,85},{SE_ENSEMBLE_DEPTH,75} };
static const PresetDelta SE_P_CONTRAWALK[] = { {SE_VIOLIN_LEVEL,0},{SE_VIOLA_LEVEL,0},{SE_CONTRABASS_LEVEL,110},{SE_BASS_FOOTAGE,EI(1,2)},{SE_ATTACK,10},{SE_RELEASE,40},{SE_ENSEMBLE_DEPTH,40} };
static const PresetDelta SE_P_CELLOSOLO[] = { {SE_VIOLIN_LEVEL,0},{SE_VIOLA_LEVEL,0},{SE_CELLO_LEVEL,120},{SE_ATTACK,50},{SE_RELEASE,90},{SE_ENSEMBLE_DEPTH,60} };
static const PresetDelta SE_P_ORCHESTRALPAD[] = { {SE_VIOLIN_LEVEL,85},{SE_VIOLA_LEVEL,75},{SE_TRUMPET_LEVEL,30},{SE_HORN_LEVEL,50},{SE_CELLO_LEVEL,80},{SE_ATTACK,70},{SE_RELEASE,110},{SE_ENSEMBLE_DEPTH,85} };

// ── Horror/atmospheric ──
static const PresetDelta SE_P_HAUNTEDSTRINGS[] = { {SE_VIOLIN_LEVEL,70},{SE_VIOLA_LEVEL,90},{SE_ATTACK,110},{SE_RELEASE,120},{SE_ENSEMBLE_DEPTH,100},{SE_REV_MIX,45},{SE_REV_SIZE,110} };
static const PresetDelta SE_P_DREADHORNS[] = { {SE_HORN_LEVEL,110},{SE_TRUMPET_LEVEL,20},{SE_CELLO_LEVEL,60},{SE_ATTACK,90},{SE_RELEASE,100},{SE_ENSEMBLE_DEPTH,80},{SE_REV_MIX,40} };
static const PresetDelta SE_P_COLDSTRINGS[] = { {SE_VIOLIN_LEVEL,95},{SE_VIOLA_LEVEL,60},{SE_ATTACK,100},{SE_RELEASE,127},{SE_ENSEMBLE_DEPTH,110},{SE_FILTER_CUTOFF,55},{SE_FILTER_TYPE,EI(11,VF_NUM_MODES)} };
static const PresetDelta SE_P_DRONESTRINGS[] = { {SE_VIOLA_LEVEL,100},{SE_HORN_LEVEL,50},{SE_CONTRABASS_LEVEL,70},{SE_ATTACK,127},{SE_RELEASE,127},{SE_ENSEMBLE_DEPTH,90},{SE_REV_MIX,55},{SE_REV_SIZE,127} };
static const PresetDelta SE_P_TENSIONSTAB[] = { {SE_VIOLIN_LEVEL,110},{SE_TRUMPET_LEVEL,70},{SE_ATTACK,0},{SE_RELEASE,30},{SE_ENSEMBLE_DEPTH,50} };

// ── Filtered enhancements (non-STOCK, see file banner point 5) ──
static const PresetDelta SE_P_DARKSTRINGS[] = { {SE_VIOLIN_LEVEL,90},{SE_VIOLA_LEVEL,90},{SE_ATTACK,60},{SE_RELEASE,90},{SE_FILTER_CUTOFF,40},{SE_FILTER_TYPE,EI(1,VF_NUM_MODES)},{SE_FILTER_RES,40} };
static const PresetDelta SE_P_RESOSTRINGS[] = { {SE_VIOLIN_LEVEL,100},{SE_VIOLA_LEVEL,70},{SE_ATTACK,40},{SE_RELEASE,70},{SE_FILTER_CUTOFF,65},{SE_FILTER_TYPE,EI(7,VF_NUM_MODES)},{SE_FILTER_RES,70} };
static const PresetDelta SE_P_FORMANTCHOIR[] = { {SE_VIOLIN_LEVEL,70},{SE_VIOLA_LEVEL,80},{SE_HORN_LEVEL,50},{SE_ATTACK,90},{SE_RELEASE,100},{SE_FILTER_CUTOFF,60},{SE_FILTER_TYPE,EI(13,VF_NUM_MODES)} };

// ── Slow evolving pads ──
static const PresetDelta SE_P_SLOWSWELL[] = { {SE_VIOLIN_LEVEL,90},{SE_VIOLA_LEVEL,80},{SE_HORN_LEVEL,40},{SE_ATTACK,127},{SE_RELEASE,127},{SE_ENSEMBLE_DEPTH,95} };
static const PresetDelta SE_P_GENTLESWELL[] = { {SE_VIOLIN_LEVEL,80},{SE_VIOLA_LEVEL,70},{SE_ATTACK,90},{SE_RELEASE,100},{SE_ENSEMBLE_DEPTH,65} };
static const PresetDelta SE_P_FASTSTRINGS[] = { {SE_VIOLIN_LEVEL,110},{SE_VIOLA_LEVEL,80},{SE_ATTACK,5},{SE_RELEASE,30},{SE_ENSEMBLE_DEPTH,55} };
static const PresetDelta SE_P_STACCATOSTRINGS[] = { {SE_VIOLIN_LEVEL,120},{SE_ATTACK,0},{SE_RELEASE,15},{SE_ENSEMBLE_DEPTH,45} };

// ── Combos ──
static const PresetDelta SE_P_VIOLINVIOLA[] = { {SE_VIOLIN_LEVEL,100},{SE_VIOLA_LEVEL,100},{SE_ATTACK,50},{SE_RELEASE,80},{SE_ENSEMBLE_DEPTH,75} };
static const PresetDelta SE_P_TRUMPETHORN[] = { {SE_TRUMPET_LEVEL,100},{SE_HORN_LEVEL,100},{SE_ATTACK,30},{SE_RELEASE,60},{SE_ENSEMBLE_DEPTH,60} };
static const PresetDelta SE_P_ALLSIX[] = { {SE_VIOLIN_LEVEL,70},{SE_VIOLA_LEVEL,65},{SE_TRUMPET_LEVEL,55},{SE_HORN_LEVEL,55},{SE_CELLO_LEVEL,60},{SE_ATTACK,60},{SE_RELEASE,95},{SE_ENSEMBLE_DEPTH,90} };
static const PresetDelta SE_P_LIGHTVIOLIN[] = { {SE_VIOLIN_LEVEL,60},{SE_ATTACK,70},{SE_RELEASE,90},{SE_ENSEMBLE_DEPTH,50} };
static const PresetDelta SE_P_WARMHORN[] = { {SE_HORN_LEVEL,100},{SE_VIOLA_LEVEL,40},{SE_ATTACK,80},{SE_RELEASE,100},{SE_ENSEMBLE_DEPTH,70} };

// ── Chorus-forward showcase (ensemble effect, see file banner point 4) ──
static const PresetDelta SE_P_DEEPENSEMBLE[] = { {SE_VIOLIN_LEVEL,90},{SE_VIOLA_LEVEL,85},{SE_ATTACK,60},{SE_RELEASE,90},{SE_ENSEMBLE_DEPTH,127},{SE_ENSEMBLE_RATE,20} };
static const PresetDelta SE_P_SUBTLECHORUS[] = { {SE_VIOLIN_LEVEL,100},{SE_ATTACK,40},{SE_RELEASE,70},{SE_ENSEMBLE_DEPTH,25},{SE_ENSEMBLE_RATE,40} };

// ── Arp ──
static const PresetDelta SE_P_ARPCLASSIC[] = { {SE_VIOLIN_LEVEL,100},{SE_ATTACK,10},{SE_RELEASE,30},{SE_ENSEMBLE_DEPTH,40},{SE_ARP_ENABLE,EI(1,2)},{SE_ARP_RATE,80},{SE_ARP_GATE,70} };
static const PresetDelta SE_P_ARPSTRINGS[] = { {SE_VIOLIN_LEVEL,90},{SE_VIOLA_LEVEL,60},{SE_ATTACK,15},{SE_RELEASE,40},{SE_ARP_ENABLE,EI(1,2)},{SE_ARP_RATE,95},{SE_ARP_GATE,80},{SE_ARP_OCTAVES,48},{SE_ARP_DIR,EI(2,6)} };

static const Preset SE_PRESETS[] = {
    SE_PRESET("FULL ENSEMBLE",   SE_P_FULLENSEMBLE),
    SE_PRESET("VIOLINS SOLO",    SE_P_VIOLINSSOLO),
    SE_PRESET("VIOLA SECTION",   SE_P_VIOLASECTION),
    SE_PRESET("BRASS ENSEMBLE",  SE_P_BRASSENSEMBLE),
    SE_PRESET("HORN SECTION",    SE_P_HORNSECTION),
    SE_PRESET("STRINGS+BRASS",   SE_P_STRINGSANDBRASS),
    SE_PRESET("STRINGS+BASS",    SE_P_STRINGSWBASS),
    SE_PRESET("CONTRA WALK",     SE_P_CONTRAWALK),
    SE_PRESET("CELLO SOLO",      SE_P_CELLOSOLO),
    SE_PRESET("ORCHESTRAL PAD",  SE_P_ORCHESTRALPAD),
    SE_PRESET("HAUNTED STRINGS", SE_P_HAUNTEDSTRINGS),
    SE_PRESET("DREAD HORNS",     SE_P_DREADHORNS),
    SE_PRESET("COLD STRINGS",    SE_P_COLDSTRINGS),
    SE_PRESET("DRONE STRINGS",   SE_P_DRONESTRINGS),
    SE_PRESET("TENSION STAB",    SE_P_TENSIONSTAB),
    SE_PRESET("DARK STRINGS",    SE_P_DARKSTRINGS),
    SE_PRESET("RESO STRINGS",    SE_P_RESOSTRINGS),
    SE_PRESET("FORMANT CHOIR",   SE_P_FORMANTCHOIR),
    SE_PRESET("SLOW SWELL",      SE_P_SLOWSWELL),
    SE_PRESET("GENTLE SWELL",    SE_P_GENTLESWELL),
    SE_PRESET("FAST STRINGS",    SE_P_FASTSTRINGS),
    SE_PRESET("STACCATO STRINGS",SE_P_STACCATOSTRINGS),
    SE_PRESET("VIOLIN+VIOLA",    SE_P_VIOLINVIOLA),
    SE_PRESET("TRUMPET+HORN",    SE_P_TRUMPETHORN),
    SE_PRESET("ALL SIX",         SE_P_ALLSIX),
    SE_PRESET("LIGHT VIOLIN",    SE_P_LIGHTVIOLIN),
    SE_PRESET("WARM HORN",       SE_P_WARMHORN),
    SE_PRESET("DEEP ENSEMBLE",   SE_P_DEEPENSEMBLE),
    SE_PRESET("SUBTLE CHORUS",   SE_P_SUBTLECHORUS),
    SE_PRESET("ARP CLASSIC",     SE_P_ARPCLASSIC),
    SE_PRESET("ARP STRINGS",     SE_P_ARPSTRINGS),
};
#define SE_NUM_PRESETS (sizeof(SE_PRESETS) / sizeof(SE_PRESETS[0]))

static void seApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < SE_NUM_PARAMS; i++) seRaw[i] = (float)SE_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) seRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    seDecode();
}

static const EnginePageDef SE_PAGES[] = {
    { "VOICINGS",   SE_VIOLIN_LEVEL,   7 },   // 6 voicings + bass footage
    { "ENVELOPE",   SE_ATTACK,         2 },
    { "ENSEMBLE",   SE_ENSEMBLE_RATE,  2 },
    { "FILTER",     SE_FILTER_CUTOFF,  3 },
    { "VOICING",    SE_MASTER,         2 },
    { "OUTPUT",     SE_TREM_RATE,      8 },
    { "ARP",        SE_ARP_ENABLE,     8 },
};
#define SE_NUM_PAGES (sizeof(SE_PAGES) / sizeof(SE_PAGES[0]))

uint8_t seMasterParamIdE() { return SE_MASTER; }
uint8_t seFilterCutoffParamIdE()    { return SE_FILTER_CUTOFF; }
uint8_t seFilterResonanceParamIdE() { return SE_FILTER_RES; }
uint8_t seNumPagesE() { return (uint8_t)SE_NUM_PAGES; }
const EnginePageDef* sePageE(uint8_t idx) { return (idx < SE_NUM_PAGES) ? &SE_PAGES[idx] : &SE_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static SeVoice sev[SE_VOICES];
static SeBassVoice seBass;
static SeEnsemble seEns;
// One shared filter applied to the final mix, not per-voice — the real
// instrument has no filter at all (see file banner point 5), so this is a
// bonus post-mix enhancement only, cheaper than 10 independent instances.
static VarFilter seSharedFilt;
extern VoiceAlloc va;

// Mono bass coupler's own held-note stack — see stringens.h file banner
// point 2. Same technique as this codebase's Prophet-5 UNISON/OB-X HOLD:
// built entirely at the engine level since the shared voice allocator has
// no concept of "lowest held note only, restricted to a key range."
#define SE_BASS_MAX_HELD SE_VOICES
static int     seBassHeld[SE_BASS_MAX_HELD];
static uint8_t seBassHeldCount = 0;

static void seBassReset() { seBassHeldCount = 0; }
static void seBassPush(int note) {
    if (note > SE_BASS_SPLIT_NOTE) return;
    for (uint8_t i = 0; i < seBassHeldCount; i++) if (seBassHeld[i] == note) return;
    if (seBassHeldCount < SE_BASS_MAX_HELD) seBassHeld[seBassHeldCount++] = note;
}
static void seBassPop(int note) {
    for (uint8_t i = 0; i < seBassHeldCount; i++) {
        if (seBassHeld[i] == note) {
            for (uint8_t j = i; j < seBassHeldCount - 1; j++) seBassHeld[j] = seBassHeld[j + 1];
            seBassHeldCount--;
            return;
        }
    }
}
static int seBassLowest() {
    if (seBassHeldCount == 0) return -1;
    int lo = seBassHeld[0];
    for (uint8_t i = 1; i < seBassHeldCount; i++) if (seBassHeld[i] < lo) lo = seBassHeld[i];
    return lo;
}

void seEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void seEngineActivate() {
    for (int i = 0; i < SE_VOICES; i++) sev[i] = SeVoice();
    seBass = SeBassVoice();
    seEns = SeEnsemble();
    seSharedFilt.reset();
    seBassReset();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void seParamsInitE() { seApplyDeltas(nullptr, 0); }
void seSetParameterE(uint8_t id, float v) { if (id < SE_NUM_PARAMS) { seRaw[id] = v; seDecode(); } }
float seGetParameterE(uint8_t id) { return (id < SE_NUM_PARAMS) ? seRaw[id] : 0.0f; }
void seFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= SE_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = SE_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(seRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(seRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool seParamIsLockableE(uint8_t id) { return id < SE_NUM_PARAMS; }
uint8_t seNumParamsE() { return SE_NUM_PARAMS; }
static const ParamInfo SE_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* seParamInfoE(uint8_t id) {
    return (id < SE_NUM_PARAMS) ? &SE_PARAM_INFO[id] : &SE_INVALID_PARAM;
}
uint8_t seNumPresetsE() { return (uint8_t)SE_NUM_PRESETS; }
const char* sePresetNameE(uint8_t idx) { return SE_PRESETS[idx].name; }
void seLoadPresetE(uint8_t idx) { seApplyDeltas(SE_PRESETS[idx].deltas, SE_PRESETS[idx].n); }
void seLoadInitPatchE() { seApplyDeltas(nullptr, 0); }
void seSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < SE_NUM_PARAMS; i++) out[i] = (uint8_t)(seRaw[i] * 255.0f + 0.5f);
}
void seRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < SE_NUM_PARAMS; i++) seRaw[i] = (float)in[i] * (1.0f / 255.0f);
    seDecode();
}

uint8_t seNumVoicesE() { return SE_VOICES; }
void    seVoiceInitE() { seEngineActivate(); }

static inline float seBassFreq(int note) {
    return noteToFreq(note) * (se.bassFootage16 ? 0.5f : 1.0f);
}

void seNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx < SE_VOICES) {
        SeVoice& v = sev[voiceIdx];
        v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
        v.gate = true;
        v.noteFreq = noteToFreq(note);   // pitch bend is re-applied every control tick in the render loop
        if (trigEnv) v.env.trigger(se.atkRate, se.relRate);
    }

    // Mono bass coupler: independent of voiceIdx, tracks whichever held
    // note (restricted to the low split) is currently lowest — see file
    // banner point 2.
    seBassPush(note);
    int lowest = seBassLowest();
    if (lowest >= 0 && lowest != seBass.note) {
        seBass.note = lowest;
        seBass.noteFreq = seBassFreq(lowest);
        if (trigEnv) seBass.env.trigger(se.atkRate, se.relRate);
    }
}
void seNoteOffE(uint8_t voiceIdx, int note) {
    if (voiceIdx < SE_VOICES) {
        sev[voiceIdx].gate = false;
        sev[voiceIdx].env.release();
    }

    if (note >= 0) seBassPop(note); else seBassReset();
    int lowest = seBassLowest();
    if (lowest < 0) {
        seBass.env.release();
        seBass.note = -1;
    } else if (lowest != seBass.note) {
        seBass.note = lowest;
        seBass.noteFreq = seBassFreq(lowest);
        seBass.env.trigger(se.atkRate, se.relRate);
    }
}
void seAllOffE() {
    seBassReset();
    for (int i = 0; i < SE_VOICES; i++) { sev[i].gate = false; sev[i].env.release(); }
    seBass.env.release();
    seBass.note = -1;
}
void seSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < SE_VOICES) sev[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int seRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch; (void)modWheel;
    const float bendMul = fastPow2(pitchBend * se.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = se.tremMix > 0.004f;
    const bool fxDelay  = se.dlyMix  > 0.004f;
    const bool fxReverb = se.revMix  > 0.004f;
    g_fxChorus.setRate(se.tremRate);
    g_fxReverb.setRoom(se.revSize, 0.4f);
    g_fxDelay.setTimeMs(se.dlyTimeMs, false);

    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);
        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < SE_VOICES; i++) {
            SeVoice& v = sev[i];
            if (v.env.stage == SE_ENV_IDLE) { va.busy[i] = 0; continue; }

            if (ctrl) v.osc.setFreq(v.noteFreq * bendMul);

            float raw = v.osc.next();
            float voiceOut =
                v.violinF.process(raw, SE_VIOLIN_G) * se.violinLvl +
                v.violaF.process(raw, SE_VIOLA_G) * se.violaLvl +
                v.trumpetF.process(raw, SE_TRUMPET_G) * se.trumpetLvl +
                v.hornF.process(raw, SE_HORN_G) * se.hornLvl;
            float amp = v.env.process();

            mix += voiceOut * amp;
            va.busy[i] = 1;
            sounding++;
        }

        // Mono bass coupler — rendered once per sample, added to the same
        // mix, entirely separate from the SE_VOICES pool above.
        if (seBass.env.stage != SE_ENV_IDLE) {
            if (ctrl) seBass.osc.setFreq(seBass.noteFreq * bendMul);
            float raw = seBass.osc.next();
            float bassOut = seBass.celloF.process(raw, SE_CELLO_G) * se.celloLvl +
                            seBass.contraF.process(raw, SE_CONTRABASS_G) * se.contraLvl;
            float amp = seBass.env.process();
            mix += bassOut * amp;
            if (sounding == 0) sounding = 1;
        }

        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // The ensemble/chorus is core to this instrument's identity, not
        // an optional post-effect — see stringens.h file banner point 4.
        mix = seEns.process(mix, se.ensRateHz, se.ensDepth);

        float filtered = mix;
        if (se.filterType != VF_STOCK) {
            if (ctrl) seSharedFilt.setCutoff(se.cutoffHz, se.resonance, se.filterType);
            filtered = seSharedFilt.process(mix, se.filterType, se.resonance);
        }

        // MASTER is pure linear gain after the saturator, not baked into
        // it — same discipline every engine in this codebase follows.
        // SE_MIX_DRIVE is a reasoned estimate, not a probed one.
        const float SE_MIX_DRIVE = 1.6f;
        float out = fastTanh(filtered * SE_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, se.tremDepth, se.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, se.dlyFb, se.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * se.revMix; r += rr * se.revMix;
        }
        l *= se.master; r *= se.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

#include "prophetvs.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include <string.h>

// ============================================================================
//  prophetvs.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum VsParamId : uint8_t {
    VS_OSCA_WAVE = 0, VS_OSCA_OCTAVE, VS_OSCA_FINE,
    VS_OSCB_WAVE, VS_OSCB_OCTAVE, VS_OSCB_FINE,
    VS_OSCC_WAVE, VS_OSCC_OCTAVE, VS_OSCC_FINE,
    VS_OSCD_WAVE, VS_OSCD_OCTAVE, VS_OSCD_FINE,

    VS_VECTOR_X, VS_VECTOR_Y,
    VS_VENV_X1, VS_VENV_Y1, VS_VENV_TIME1,
    VS_VENV_X2, VS_VENV_Y2, VS_VENV_TIME2, VS_VENV_LOOP,

    VS_LFO_RATE, VS_LFO_TO_PITCH, VS_LFO_TO_VECTOR,

    VS_FILTER_CUTOFF, VS_FILTER_TYPE, VS_FILTER_RES, VS_FILTER_ENV_AMT, VS_FILTER_KBD,
    VS_FENV_A, VS_FENV_D, VS_FENV_S, VS_FENV_R,
    VS_AENV_A, VS_AENV_D, VS_AENV_S, VS_AENV_R,
    VS_GLIDE_ON, VS_GLIDE_TIME, VS_MASTER, VS_BEND_RANGE,

    VS_TREM_RATE, VS_TREM_DEPTH, VS_TREM_MIX,
    VS_DLY_TIME, VS_DLY_FB, VS_DLY_MIX, VS_REV_SIZE, VS_REV_MIX,

    VS_ARP_ENABLE, VS_ARP_PATTERN, VS_ARP_RATE, VS_ARP_GATE,
    VS_ARP_OCTAVES, VS_ARP_DIR, VS_ARP_LATCH, VS_ARP_SYNC,

    VS_NUM_PARAMS
};

static const char* const VS_WAVE_NAMES[VS_W_COUNT] = { "SINE", "TRIANGLE", "SAW", "SQUARE", "NOISE" };
static const char* const VS_OCTAVE_NAMES[5] = { "32'", "16'", "8'", "4'", "2'" };
static const char* const VS_ONOFF[2]        = { "OFF", "ON" };
static const char* const VS_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define VSROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }
// Every oscillator gets the same 3-row WAVE/OCTAVE/FINE shape -- see
// VsOsc/VsWave in prophetvs.h.
#define VS_OSC_ROWS(letter, cc0) \
    VSROW(letter " WAVE", PK_ENUM, VS_WAVE_NAMES, VS_W_COUNT, 0, VS_W_COUNT - 1, cc0), \
    VSROW(letter " OCTAVE", PK_ENUM, VS_OCTAVE_NAMES, 5, 0, 4, cc0 + 1), \
    VSROW(letter " FINE", PK_CENTS, nullptr, 0, -700, 700, cc0 + 2)

// Deduced size, not [VS_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
// CC blocks skip this codebase's reserved 32/38/64 slots -- see the
// VS_VECTOR_X/VENV_X2/DLY_FB rows below, each bumped up by one past a skip.
static const ParamInfo VS_PARAM_INFO[] = {
    VS_OSC_ROWS("A", 20),
    VS_OSC_ROWS("B", 23),
    VS_OSC_ROWS("C", 26),
    VS_OSC_ROWS("D", 29),

    VSROW("VECTOR X",  PK_BIPOLAR, nullptr, 0, -100, 100, 33),
    VSROW("VECTOR Y",  PK_BIPOLAR, nullptr, 0, -100, 100, 34),
    VSROW("VENV X1",   PK_BIPOLAR, nullptr, 0, -100, 100, 35),
    VSROW("VENV Y1",   PK_BIPOLAR, nullptr, 0, -100, 100, 36),
    VSROW("VENV TIME1",PK_MS,      nullptr, 0,   10, 8000, 37),
    VSROW("VENV X2",   PK_BIPOLAR, nullptr, 0, -100, 100, 39),
    VSROW("VENV Y2",   PK_BIPOLAR, nullptr, 0, -100, 100, 40),
    VSROW("VENV TIME2",PK_MS,      nullptr, 0,   10, 8000, 41),
    VSROW("VENV LOOP", PK_TOGGLE,  VS_ONOFF, 2,  0, 1,     42),

    VSROW("LFO RATE",  PK_HZ,      nullptr, 0,   0, 20,   43),
    VSROW("LFO>PITCH", PK_CONT,    nullptr, 0,   0, 127,  44),
    VSROW("LFO>VECTOR",PK_CONT,    nullptr, 0,   0, 127,  45),

    VSROW("CUTOFF",    PK_HZ,      nullptr, 0,  20, 18000, 46),
    VSROW("FILT TYPE", PK_ENUM,    VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 47),
    VSROW("RESONANCE", PK_CONT,    nullptr, 0,   0, 127,   48),
    VSROW("VCF ENV AMT",PK_CONT,   nullptr, 0,   0, 127,   49),
    VSROW("KBD AMT",   PK_CONT,    nullptr, 0,   0, 127,   50),
    VSROW("VCF ATK",   PK_MS,      nullptr, 0,   1, 8000,  51),
    VSROW("VCF DEC",   PK_MS,      nullptr, 0,   3, 12000, 52),
    VSROW("VCF SUS",   PK_CONT,    nullptr, 0,   0, 127,   53),
    VSROW("VCF REL",   PK_MS,      nullptr, 0,   3, 12000, 54),
    VSROW("VCA ATK",   PK_MS,      nullptr, 0,   1, 8000,  55),
    VSROW("VCA DEC",   PK_MS,      nullptr, 0,   3, 12000, 56),
    VSROW("VCA SUS",   PK_CONT,    nullptr, 0,   0, 127,   57),
    VSROW("VCA REL",   PK_MS,      nullptr, 0,   3, 12000, 58),
    VSROW("GLIDE",     PK_TOGGLE,  VS_ONOFF, 2,  0, 1,     59),
    VSROW("GLIDE TIME",PK_MS,      nullptr, 0,   0, 5000,  60),
    VSROW("MASTER",    PK_CONT,    nullptr, 0,   0, 127,   61),
    VSROW("BEND RNG",  PK_INT,     nullptr, 0,   1, 24,    62),

    VSROW("TREM RATE", PK_HZ,      nullptr, 0,   0, 20,   63),
    VSROW("TREM DEPTH",PK_CONT,    nullptr, 0,   0, 127,  65),
    VSROW("TREM MIX",  PK_CONT,    nullptr, 0,   0, 127,  66),
    VSROW("DLY TIME",  PK_MS,      nullptr, 0,  20, 1200, 67),
    VSROW("DLY FDBK",  PK_CONT,    nullptr, 0,   0, 127,  68),
    VSROW("DLY MIX",   PK_CONT,    nullptr, 0,   0, 127,  69),
    VSROW("REV SIZE",  PK_CONT,    nullptr, 0,   0, 127,  70),
    VSROW("REV MIX",   PK_CONT,    nullptr, 0,   0, 127,  71),

    VSROW("ARP ON",    PK_TOGGLE,  VS_ONOFF, 2,  0, 1,    72),
    VSROW("ARP PATT",  PK_INT,     nullptr, 0,   0, 7,    73),
    VSROW("ARP RATE",  PK_CONT,    nullptr, 0,   0, 127,  74),
    VSROW("ARP GATE",  PK_CONT,    nullptr, 0,   0, 127,  75),
    VSROW("ARP OCT",   PK_INT,     nullptr, 0,   1, 4,    76),
    VSROW("ARP DIR",   PK_ENUM,    VS_ARP_DIR_NAMES, 6, 0, 5, 77),
    VSROW("ARP LATCH", PK_TOGGLE,  VS_ONOFF, 2,  0, 1,    78),
    VSROW("ARP SYNC",  PK_TOGGLE,  VS_ONOFF, 2,  0, 1,    79),
};
static_assert(sizeof(VS_PARAM_INFO) / sizeof(VS_PARAM_INFO[0]) == VS_NUM_PARAMS,
              "VS_PARAM_INFO must have exactly VS_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float vsRaw[VS_NUM_PARAMS];

struct VsDecoded {
    uint8_t waveA, waveB, waveC, waveD;
    float   octMulA, octMulB, octMulC, octMulD;
    float   fineA, fineB, fineC, fineD;

    float   vectorX, vectorY;
    float   venvX1, venvY1, venvRate1;
    float   venvX2, venvY2, venvRate2;
    bool    venvLoop;

    float   lfoRateHz, lfoToPitch, lfoToVector;

    float   cutoffHz; uint8_t filterType; float resonance, envAmt, kbdAmt;
    float   fA, fD, fS, fR, aA, aD, aS, aR;
    bool    glideOn; float glideTimeS;
    float   master, bendRangeSemis;
    float   tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static VsDecoded vs;

static const float VS_OCTAVE_MUL[5] = { 0.25f, 0.5f, 1.0f, 2.0f, 4.0f };

static inline uint8_t octIndexFromRaw(float raw01) {
    int i = (int)(raw01 * 4.999f);
    if (i < 0) i = 0; else if (i > 4) i = 4;
    return (uint8_t)i;
}
static inline uint8_t waveIndexFromRaw(float raw01) {
    int i = (int)(raw01 * (VS_W_COUNT - 0.001f));
    if (i < 0) i = 0; else if (i >= VS_W_COUNT) i = VS_W_COUNT - 1;
    return (uint8_t)i;
}
static inline float vsFineFromRaw(float raw01) {
    return fastPow2(((raw01 * 2.0f - 1.0f) * 7.0f) / 12.0f);
}
// VENV_TIME1/2's PK_MS 10..8000 range is the time to cross the FULL -1..+1
// span at that rate -- vecEnv.process() is only ever called at control rate
// (see the render loop), so the per-tick step is pre-scaled by
// CONTROL_RATE_DIV here rather than recomputed every sample.
static inline float vsTimeToRate(float raw01, float minMs, float maxMs) {
    float timeMs = minMs + raw01 * (maxMs - minMs);
    float rate = (2.0f / (timeMs * 0.001f * SAMPLE_RATE_F)) * (float)CONTROL_RATE_DIV;
    if (rate > 2.0f) rate = 2.0f;
    return rate;
}

static void vsDecode() {
    vs.waveA = waveIndexFromRaw(vsRaw[VS_OSCA_WAVE]);
    vs.octMulA = VS_OCTAVE_MUL[octIndexFromRaw(vsRaw[VS_OSCA_OCTAVE])];
    vs.fineA = vsFineFromRaw(vsRaw[VS_OSCA_FINE]);
    vs.waveB = waveIndexFromRaw(vsRaw[VS_OSCB_WAVE]);
    vs.octMulB = VS_OCTAVE_MUL[octIndexFromRaw(vsRaw[VS_OSCB_OCTAVE])];
    vs.fineB = vsFineFromRaw(vsRaw[VS_OSCB_FINE]);
    vs.waveC = waveIndexFromRaw(vsRaw[VS_OSCC_WAVE]);
    vs.octMulC = VS_OCTAVE_MUL[octIndexFromRaw(vsRaw[VS_OSCC_OCTAVE])];
    vs.fineC = vsFineFromRaw(vsRaw[VS_OSCC_FINE]);
    vs.waveD = waveIndexFromRaw(vsRaw[VS_OSCD_WAVE]);
    vs.octMulD = VS_OCTAVE_MUL[octIndexFromRaw(vsRaw[VS_OSCD_OCTAVE])];
    vs.fineD = vsFineFromRaw(vsRaw[VS_OSCD_FINE]);

    vs.vectorX = vsRaw[VS_VECTOR_X] * 2.0f - 1.0f;
    vs.vectorY = vsRaw[VS_VECTOR_Y] * 2.0f - 1.0f;
    vs.venvX1 = vsRaw[VS_VENV_X1] * 2.0f - 1.0f;
    vs.venvY1 = vsRaw[VS_VENV_Y1] * 2.0f - 1.0f;
    vs.venvRate1 = vsTimeToRate(vsRaw[VS_VENV_TIME1], 10.0f, 8000.0f);
    vs.venvX2 = vsRaw[VS_VENV_X2] * 2.0f - 1.0f;
    vs.venvY2 = vsRaw[VS_VENV_Y2] * 2.0f - 1.0f;
    vs.venvRate2 = vsTimeToRate(vsRaw[VS_VENV_TIME2], 10.0f, 8000.0f);
    vs.venvLoop = vsRaw[VS_VENV_LOOP] > 0.5f;

    vs.lfoRateHz = vsRaw[VS_LFO_RATE] * 20.0f;
    vs.lfoToPitch = vsRaw[VS_LFO_TO_PITCH];
    vs.lfoToVector = vsRaw[VS_LFO_TO_VECTOR];

    vs.cutoffHz = 20.0f * fastPow2(vsRaw[VS_FILTER_CUTOFF] * 9.8f);
    vs.filterType = (uint8_t)(vsRaw[VS_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    vs.resonance = vsRaw[VS_FILTER_RES];
    vs.envAmt = vsRaw[VS_FILTER_ENV_AMT];
    vs.kbdAmt = vsRaw[VS_FILTER_KBD];

    vs.fA = 1.0f / (1.0f + vsRaw[VS_FENV_A] * 800.0f);
    vs.fD = 1.0f / (1.0f + vsRaw[VS_FENV_D] * 1600.0f);
    vs.fS = vsRaw[VS_FENV_S];
    vs.fR = 1.0f / (1.0f + vsRaw[VS_FENV_R] * 1600.0f);
    vs.aA = 1.0f / (1.0f + vsRaw[VS_AENV_A] * 800.0f);
    vs.aD = 1.0f / (1.0f + vsRaw[VS_AENV_D] * 1600.0f);
    vs.aS = vsRaw[VS_AENV_S];
    vs.aR = 1.0f / (1.0f + vsRaw[VS_AENV_R] * 1600.0f);

    vs.glideOn = vsRaw[VS_GLIDE_ON] > 0.5f;
    vs.glideTimeS = 0.005f + vsRaw[VS_GLIDE_TIME] * 5.0f;
    vs.master = vsRaw[VS_MASTER];
    vs.bendRangeSemis = 1.0f + vsRaw[VS_BEND_RANGE] * 23.0f;

    vs.tremRate = vsRaw[VS_TREM_RATE] * 20.0f;
    vs.tremDepth = vsRaw[VS_TREM_DEPTH];
    vs.tremMix = vsRaw[VS_TREM_MIX];
    vs.dlyTimeMs = 20.0f + vsRaw[VS_DLY_TIME] * 1180.0f;
    vs.dlyFb = vsRaw[VS_DLY_FB];
    vs.dlyMix = vsRaw[VS_DLY_MIX];
    vs.revSize = vsRaw[VS_REV_SIZE];
    vs.revMix = vsRaw[VS_REV_MIX];
}

// ─── Per-voice fixed offsets (see prophetvs.h) ─────────────────────────────
const float VS_VOICE_DETUNE[VS_VOICES] = { -3.2f, 2.1f, -1.4f, 3.6f, -2.8f, 1.7f, -0.6f, 2.9f };
const float VS_VOICE_FILT[VS_VOICES]   = { -0.03f, 0.02f, -0.015f, 0.025f, -0.02f, 0.015f, -0.01f, 0.03f };
const float VS_VOICE_LEVEL[VS_VOICES]  = { 0.98f, 1.02f, 0.99f, 1.01f, 0.97f, 1.03f, 1.0f, 0.995f };

// ─── Presets ─────────────────────────────────────────────────────────────
// The default patch: A/B saw, C square, D sine an octave down, vector
// parked dead centre (equal 4-way blend) with a static (non-moving) vector
// envelope -- X1/Y1/X2/Y2 all equal to the centre start position, so
// approach() reports "arrived" on sample one and the note simply holds
// still, exactly the way a real VS patch with a zero-time envelope 3 does.
static const uint8_t VS_INIT[] = {
    EI(2,5), EI(2,5), 64,      // OSC A: SAW, 8', fine centre
    EI(2,5), EI(2,5), 64,      // OSC B: SAW, 8', fine centre
    EI(3,5), EI(2,5), 64,      // OSC C: SQUARE, 8', fine centre
    0,       EI(1,5), 64,      // OSC D: SINE, 16', fine centre
    64, 64,                    // vector base position: centre
    64, 64, 64,                // venv waypoint 1: centre, mid time
    64, 64, 64,                // venv waypoint 2: centre, mid time
    0,                         // venv loop off
    40, 0, 0,                  // LFO rate/to-pitch/to-vector
    90, 0, 20, 50, 30,         // filter cutoff/type(STOCK)/res/envamt/kbd
    10, 60, 90, 40,            // filter env ADSR
    5, 70, 110, 50,            // amp env ADSR
    0, 64,                     // glide/glidetime
    110, 20,                   // master/bend
    0,0,0, 0,0,0,0,0,          // output
    0,0,64,80,0,0,0,0,         // arp
};
static_assert(sizeof(VS_INIT) / sizeof(VS_INIT[0]) == VS_NUM_PARAMS,
              "VS_INIT must have exactly VS_NUM_PARAMS values");

#define VS_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Basses (hugging OSC D's low-octave corner) ──
static const PresetDelta VS_P_SUBBASS[]  = { {VS_VECTOR_X,0},{VS_VECTOR_Y,0},{VS_OSCD_OCTAVE,0},{VS_FILTER_CUTOFF,25},{VS_FILTER_RES,20},{VS_AENV_D,45},{VS_AENV_S,100} };
static const PresetDelta VS_P_DIGIBASS[] = { {VS_VECTOR_X,127},{VS_VECTOR_Y,0},{VS_OSCC_OCTAVE,0},{VS_FILTER_CUTOFF,30},{VS_FILTER_RES,40},{VS_FENV_D,25},{VS_AENV_D,35} };
static const PresetDelta VS_P_GRITBASS[] = { {VS_VECTOR_X,110},{VS_VECTOR_Y,20},{VS_OSCC_OCTAVE,EI(1,5)},{VS_FILTER_CUTOFF,35},{VS_FILTER_RES,90},{VS_FILTER_ENV_AMT,80},{VS_FENV_D,30} };

// ── Leads ──
static const PresetDelta VS_P_SAWLEAD[]    = { {VS_VECTOR_X,64},{VS_VECTOR_Y,127},{VS_FILTER_CUTOFF,80},{VS_FILTER_RES,50},{VS_FENV_A,0} };
static const PresetDelta VS_P_SQUARELEAD[] = { {VS_VECTOR_X,127},{VS_VECTOR_Y,64},{VS_OSCB_FINE,70},{VS_FILTER_CUTOFF,75},{VS_FILTER_RES,55} };
static const PresetDelta VS_P_VECTORLEAD[] = { {VS_FILTER_CUTOFF,85},{VS_FILTER_RES,45},{VS_FENV_A,0},{VS_FENV_D,40} };
static const PresetDelta VS_P_GLIDELEAD[]  = { {VS_VECTOR_X,64},{VS_VECTOR_Y,127},{VS_GLIDE_ON,EI(1,2)},{VS_GLIDE_TIME,20},{VS_FILTER_CUTOFF,78} };

// ── Vector showcase ──
static const PresetDelta VS_P_VECTORSWEEP[]= { {VS_VECTOR_X,0},{VS_VECTOR_Y,0},{VS_VENV_X1,127},{VS_VENV_Y1,127},{VS_VENV_TIME1,100},{VS_VENV_X2,127},{VS_VENV_Y2,127},{VS_VENV_TIME2,10},{VS_AENV_A,10},{VS_AENV_S,110},{VS_AENV_R,70} };
static const PresetDelta VS_P_VECTORLOOP[] = { {VS_VECTOR_X,0},{VS_VECTOR_Y,127},{VS_VENV_X1,127},{VS_VENV_Y1,0},{VS_VENV_TIME1,40},{VS_VENV_X2,0},{VS_VENV_Y2,127},{VS_VENV_TIME2,40},{VS_VENV_LOOP,EI(1,2)},{VS_AENV_S,110},{VS_FILTER_CUTOFF,70} };
static const PresetDelta VS_P_JOYSTICKSTAB[]={ {VS_VECTOR_X,64},{VS_VECTOR_Y,64},{VS_VENV_X1,127},{VS_VENV_Y1,0},{VS_VENV_TIME1,8},{VS_VENV_X2,127},{VS_VENV_Y2,0},{VS_VENV_TIME2,8},{VS_FILTER_CUTOFF,75},{VS_AENV_D,30},{VS_AENV_S,20} };
static const PresetDelta VS_P_AUTOXFADE[]  = { {VS_VECTOR_X,64},{VS_VECTOR_Y,64},{VS_LFO_RATE,55},{VS_LFO_TO_VECTOR,90},{VS_FILTER_CUTOFF,70},{VS_AENV_A,20},{VS_AENV_S,110} };

// ── Pads ──
static const PresetDelta VS_P_LUSHVECTORPAD[]= { {VS_VENV_X1,20},{VS_VENV_Y1,110},{VS_VENV_TIME1,120},{VS_VENV_X2,110},{VS_VENV_Y2,20},{VS_VENV_TIME2,120},{VS_VENV_LOOP,EI(1,2)},{VS_FILTER_CUTOFF,55},{VS_AENV_A,90},{VS_AENV_S,110},{VS_REV_MIX,50},{VS_REV_SIZE,80} };
static const PresetDelta VS_P_EVOLVINGPAD[]  = { {VS_VENV_X1,0},{VS_VENV_Y1,0},{VS_VENV_TIME1,127},{VS_VENV_X2,127},{VS_VENV_Y2,127},{VS_VENV_TIME2,127},{VS_FILTER_CUTOFF,50},{VS_FILTER_ENV_AMT,60},{VS_FENV_A,110},{VS_AENV_A,110},{VS_AENV_S,110} };
static const PresetDelta VS_P_GLASSPAD[]     = { {VS_VECTOR_X,64},{VS_VECTOR_Y,127},{VS_OSCA_WAVE,EI(1,5)},{VS_FILTER_CUTOFF,95},{VS_FILTER_RES,30},{VS_AENV_A,60},{VS_AENV_S,105} };
static const PresetDelta VS_P_STRINGSVS[]    = { {VS_VECTOR_X,90},{VS_VECTOR_Y,64},{VS_OSCB_FINE,72},{VS_FILTER_CUTOFF,60},{VS_AENV_A,70},{VS_AENV_S,105},{VS_TREM_MIX,40} };
static const PresetDelta VS_P_CHORUSPAD[]    = { {VS_VECTOR_X,64},{VS_VECTOR_Y,90},{VS_OSCA_FINE,58},{VS_OSCB_FINE,70},{VS_FILTER_CUTOFF,58},{VS_AENV_A,80},{VS_AENV_S,110},{VS_TREM_MIX,55},{VS_TREM_RATE,30} };

// ── Brass/stabs ──
static const PresetDelta VS_P_VSBRASS[]  = { {VS_VECTOR_X,110},{VS_VECTOR_Y,90},{VS_FILTER_CUTOFF,55},{VS_FILTER_ENV_AMT,85},{VS_FENV_A,15},{VS_FENV_D,45},{VS_AENV_A,10} };
static const PresetDelta VS_P_DIGISTAB[] = { {VS_VECTOR_X,100},{VS_VECTOR_Y,64},{VS_FILTER_CUTOFF,60},{VS_FILTER_ENV_AMT,90},{VS_FENV_A,0},{VS_FENV_D,18},{VS_AENV_D,22} };

// ── Bells/plucks ──
static const PresetDelta VS_P_DIGIBELL[]    = { {VS_VECTOR_X,0},{VS_VECTOR_Y,127},{VS_OSCA_WAVE,0},{VS_OSCA_OCTAVE,EI(4,5)},{VS_FILTER_CUTOFF,100},{VS_FILTER_ENV_AMT,60},{VS_FENV_D,30},{VS_AENV_D,55},{VS_AENV_S,20} };
static const PresetDelta VS_P_VECTORPLUCK[] = { {VS_VENV_X1,127},{VS_VENV_Y1,127},{VS_VENV_TIME1,15},{VS_VENV_X2,0},{VS_VENV_Y2,0},{VS_VENV_TIME2,60},{VS_FILTER_CUTOFF,85},{VS_AENV_D,35},{VS_AENV_S,10} };
static const PresetDelta VS_P_METALPLUCK[]  = { {VS_OSCC_WAVE,EI(4,5)},{VS_VECTOR_X,90},{VS_VECTOR_Y,20},{VS_FILTER_CUTOFF,90},{VS_FILTER_RES,60},{VS_AENV_D,25},{VS_AENV_S,0} };

// ── SFX ──
static const PresetDelta VS_P_NOISESWEEP[] = { {VS_OSCD_WAVE,EI(4,5)},{VS_VECTOR_X,0},{VS_VECTOR_Y,0},{VS_FILTER_CUTOFF,25},{VS_FILTER_RES,90},{VS_FILTER_ENV_AMT,120},{VS_FENV_A,90},{VS_FENV_D,100},{VS_AENV_A,10},{VS_AENV_S,110} };
static const PresetDelta VS_P_SIREN[]      = { {VS_LFO_RATE,15},{VS_LFO_TO_PITCH,100},{VS_AENV_A,30},{VS_AENV_S,110} };
static const PresetDelta VS_P_VECTORFX[]   = { {VS_VENV_X1,0},{VS_VENV_Y1,127},{VS_VENV_TIME1,5},{VS_VENV_X2,127},{VS_VENV_Y2,0},{VS_VENV_TIME2,5},{VS_VENV_LOOP,EI(1,2)},{VS_FILTER_RES,100},{VS_AENV_S,110} };

// ── Filter showcase ──
static const PresetDelta VS_P_RESOSWEEP[]  = { {VS_FILTER_CUTOFF,20},{VS_FILTER_RES,110},{VS_FILTER_ENV_AMT,110},{VS_FENV_A,60},{VS_FENV_D,90},{VS_AENV_S,110} };
static const PresetDelta VS_P_SCREAMLEAD[] = { {VS_VECTOR_X,100},{VS_FILTER_CUTOFF,55},{VS_FILTER_RES,124},{VS_FILTER_ENV_AMT,40},{VS_FENV_D,50},{VS_AENV_A,0} };

// ── Arp ──
static const PresetDelta VS_P_ARPDIGI[]   = { {VS_FILTER_CUTOFF,70},{VS_AENV_D,20},{VS_AENV_S,40},{VS_ARP_ENABLE,EI(1,2)},{VS_ARP_RATE,80},{VS_ARP_GATE,70} };
static const PresetDelta VS_P_ARPVECTOR[] = { {VS_VECTOR_X,0},{VS_VECTOR_Y,0},{VS_VENV_X1,127},{VS_VENV_Y1,127},{VS_VENV_TIME1,60},{VS_VENV_X2,0},{VS_VENV_Y2,0},{VS_VENV_TIME2,60},{VS_VENV_LOOP,EI(1,2)},{VS_ARP_ENABLE,EI(1,2)},{VS_ARP_RATE,85},{VS_ARP_GATE,75} };
static const PresetDelta VS_P_ARPSWEEP[]  = { {VS_VECTOR_X,90},{VS_FILTER_CUTOFF,40},{VS_FILTER_RES,60},{VS_FILTER_ENV_AMT,80},{VS_FENV_D,40},{VS_ARP_ENABLE,EI(1,2)},{VS_ARP_RATE,90},{VS_ARP_GATE,80},{VS_ARP_OCTAVES,60} };

static const Preset VS_PRESETS[] = {
    VS_PRESET("SUB BASS",     VS_P_SUBBASS),
    VS_PRESET("DIGI BASS",    VS_P_DIGIBASS),
    VS_PRESET("GRIT BASS",    VS_P_GRITBASS),
    VS_PRESET("SAW LEAD",     VS_P_SAWLEAD),
    VS_PRESET("SQUARE LEAD",  VS_P_SQUARELEAD),
    VS_PRESET("VECTOR LEAD",  VS_P_VECTORLEAD),
    VS_PRESET("GLIDE LEAD",   VS_P_GLIDELEAD),
    VS_PRESET("VECTOR SWEEP", VS_P_VECTORSWEEP),
    VS_PRESET("VECTOR LOOP",  VS_P_VECTORLOOP),
    VS_PRESET("JOYSTICK STAB",VS_P_JOYSTICKSTAB),
    VS_PRESET("AUTO XFADE",   VS_P_AUTOXFADE),
    VS_PRESET("LUSH VEC PAD", VS_P_LUSHVECTORPAD),
    VS_PRESET("EVOLVING PAD", VS_P_EVOLVINGPAD),
    VS_PRESET("GLASS PAD",    VS_P_GLASSPAD),
    VS_PRESET("STRINGS VS",   VS_P_STRINGSVS),
    VS_PRESET("CHORUS PAD",   VS_P_CHORUSPAD),
    VS_PRESET("VS BRASS",     VS_P_VSBRASS),
    VS_PRESET("DIGI STAB",    VS_P_DIGISTAB),
    VS_PRESET("DIGI BELL",    VS_P_DIGIBELL),
    VS_PRESET("VECTOR PLUCK", VS_P_VECTORPLUCK),
    VS_PRESET("METAL PLUCK",  VS_P_METALPLUCK),
    VS_PRESET("NOISE SWEEP",  VS_P_NOISESWEEP),
    VS_PRESET("SIREN",        VS_P_SIREN),
    VS_PRESET("VECTOR FX",    VS_P_VECTORFX),
    VS_PRESET("RESO SWEEP",   VS_P_RESOSWEEP),
    VS_PRESET("SCREAM LEAD",  VS_P_SCREAMLEAD),
    VS_PRESET("ARP DIGI",     VS_P_ARPDIGI),
    VS_PRESET("ARP VECTOR",   VS_P_ARPVECTOR),
    VS_PRESET("ARP SWEEP",    VS_P_ARPSWEEP),
};
#define VS_NUM_PRESETS (sizeof(VS_PRESETS) / sizeof(VS_PRESETS[0]))

static void vsApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < VS_NUM_PARAMS; i++) vsRaw[i] = (float)VS_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) vsRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    vsDecode();
}

static const EnginePageDef VS_PAGES[] = {
    { "OSC A/B",    VS_OSCA_WAVE,     6 },
    { "OSC C/D",    VS_OSCC_WAVE,     6 },
    { "VECTOR",     VS_VECTOR_X,      9 },
    { "LFO",        VS_LFO_RATE,      3 },
    { "FILTER",     VS_FILTER_CUTOFF, 5 },
    { "ENVELOPES",  VS_FENV_A,        8 },
    { "VOICING",    VS_GLIDE_ON,      4 },
    { "OUTPUT",     VS_TREM_RATE,     8 },
    { "ARP",        VS_ARP_ENABLE,    8 },
};
#define VS_NUM_PAGES (sizeof(VS_PAGES) / sizeof(VS_PAGES[0]))

uint8_t vsMasterParamIdE() { return VS_MASTER; }
uint8_t vsFilterCutoffParamIdE()    { return VS_FILTER_CUTOFF; }
uint8_t vsFilterResonanceParamIdE() { return VS_FILTER_RES; }
uint8_t vsNumPagesE() { return (uint8_t)VS_NUM_PAGES; }
const EnginePageDef* vsPageE(uint8_t idx) { return (idx < VS_NUM_PAGES) ? &VS_PAGES[idx] : &VS_PAGES[0]; }

// ─── Voices ─────────────────────────────────────────────────────────────
static VsVoice vsv[VS_VOICES];
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
extern VoiceAlloc va;

void vsEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void vsEngineActivate() {
    for (int i = 0; i < VS_VOICES; i++) vsv[i] = VsVoice();
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void vsParamsInitE() { vsApplyDeltas(nullptr, 0); }
void vsSetParameterE(uint8_t id, float v) { if (id < VS_NUM_PARAMS) { vsRaw[id] = v; vsDecode(); } }
float vsGetParameterE(uint8_t id) { return (id < VS_NUM_PARAMS) ? vsRaw[id] : 0.0f; }
void vsFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= VS_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = VS_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(vsRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(vsRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool vsParamIsLockableE(uint8_t id) { return id < VS_NUM_PARAMS; }
uint8_t vsNumParamsE() { return VS_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo VS_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* vsParamInfoE(uint8_t id) {
    return (id < VS_NUM_PARAMS) ? &VS_PARAM_INFO[id] : &VS_INVALID_PARAM;
}
uint8_t vsNumPresetsE() { return (uint8_t)VS_NUM_PRESETS; }
const char* vsPresetNameE(uint8_t idx) { return VS_PRESETS[idx].name; }
void vsLoadPresetE(uint8_t idx) { vsApplyDeltas(VS_PRESETS[idx].deltas, VS_PRESETS[idx].n); }
void vsLoadInitPatchE() { vsApplyDeltas(nullptr, 0); }
void vsSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < VS_NUM_PARAMS; i++) out[i] = (uint8_t)(vsRaw[i] * 255.0f + 0.5f);
}
void vsRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < VS_NUM_PARAMS; i++) vsRaw[i] = (float)in[i] * (1.0f / 255.0f);
    vsDecode();
}

uint8_t vsNumVoicesE() { return VS_VOICES; }
void    vsVoiceInitE() { vsEngineActivate(); }

void vsNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= VS_VOICES) return;
    VsVoice& v = vsv[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.velNorm = (float)vel * (1.0f / 127.0f);
    v.gate = true;
    v.targetFreq = freq;
    if (vs.glideOn && v.currentFreq > 20.0f) {
        v.gliding = true;
        v.glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (vs.glideTimeS * SAMPLE_RATE_F);
        if (v.glideCoef > 1.0f) v.glideCoef = 1.0f;
    } else {
        v.currentFreq = freq;
        v.gliding = false;
    }
    if (trigEnv) {
        v.filtEnv.trigger(vs.fA, vs.fD, vs.fS, vs.fR);
        v.ampEnv.trigger(vs.aA, vs.aD, vs.aS, vs.aR);
        // The vector envelope always starts from the held joystick position
        // (VECTOR_X/Y) and moves through its two waypoints -- see
        // VsVecEnv::trigger in prophetvs.h. A "static" patch just sets both
        // waypoints equal to the start position (VS_INIT does exactly this),
        // so approach() reports "arrived" on sample one and the position
        // never actually moves.
        v.vecEnv.trigger(vs.vectorX, vs.vectorY, vs.venvX1, vs.venvY1, vs.venvRate1,
                          vs.venvX2, vs.venvY2, vs.venvRate2, vs.venvLoop);
    }
}
void vsNoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= VS_VOICES) return;
    VsVoice& v = vsv[voiceIdx];
    v.gate = false;
    v.filtEnv.release();
    v.ampEnv.release();
    // The vector envelope deliberately keeps running/looping through
    // release -- real hardware's own envelope 3 free-runs independently of
    // the gate when looped, and simply holds its last position otherwise.
}
void vsAllOffE() { for (int i = 0; i < VS_VOICES; i++) vsNoteOffE(i); }
void vsSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < VS_VOICES) vsv[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int vsRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * vs.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = vs.tremMix > 0.004f;
    const bool fxDelay  = vs.dlyMix  > 0.004f;
    const bool fxReverb = vs.revMix  > 0.004f;
    g_fxChorus.setRate(vs.tremRate);
    g_fxReverb.setRoom(vs.revSize, 0.4f);
    g_fxDelay.setTimeMs(vs.dlyTimeMs, false);

    static uint32_t lfoPhase = 0;
    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        lfoPhase += (uint32_t)(vs.lfoRateHz * PHASE_PER_HZ);
        const float lfo = sineFromPhase(lfoPhase);
        const float lfoPitchSemis = lfo * vs.lfoToPitch * modWheel * 0.5f;
        const float lfoVectorOffset = lfo * vs.lfoToVector * modWheel;

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < VS_VOICES; i++) {
            VsVoice& v = vsv[i];
            const bool alive = v.ampEnv.stage != ADSR_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            if (ctrl) {
                if (v.gliding) {
                    float d = v.targetFreq - v.currentFreq;
                    if (d < 0.05f && d > -0.05f) { v.currentFreq = v.targetFreq; v.gliding = false; }
                    else v.currentFreq += d * v.glideCoef;
                }
                const float detuneMul = fastPow2(VS_VOICE_DETUNE[i] * (1.0f / 1200.0f));
                const float pitchLfoMul = fastPow2(lfoPitchSemis / 12.0f);
                const float base = v.currentFreq * bendMul * detuneMul * pitchLfoMul;
                v.oscA.setFreq(base * vs.octMulA * vs.fineA);
                v.oscB.setFreq(base * vs.octMulB * vs.fineB);
                v.oscC.setFreq(base * vs.octMulC * vs.fineC);
                v.oscD.setFreq(base * vs.octMulD * vs.fineD);

                v.vecEnv.process();

                const float envF = v.filtEnv.process();
                const float kbdSemis = vs.kbdAmt * (float)(v.note - 60);
                const float filtVoiceTrimSemis = VS_VOICE_FILT[i] * 12.0f;
                const float cutoff = safeClamp(
                    vs.cutoffHz * fastPow2((kbdSemis + vs.envAmt * envF * 48.0f + filtVoiceTrimSemis) / 12.0f),
                    20.0f, 18000.0f);
                if (vs.filterType == VF_STOCK) v.filter.setCutoffHz(cutoff, vs.resonance);
                else                           v.varFilt.setCutoff(cutoff, vs.resonance, vs.filterType);
            }

            const float sA = v.oscA.next(vs.waveA);
            const float sB = v.oscB.next(vs.waveB);
            const float sC = v.oscC.next(vs.waveC);
            const float sD = v.oscD.next(vs.waveD);

            // Bilinear crossfade across the diamond -- see prophetvs.h's
            // file banner point 1. Corner assignment: A=top-left,
            // B=top-right, C=bottom-right, D=bottom-left.
            const float vx = v.vecEnv.x;
            const float vy = safeClamp(v.vecEnv.y + lfoVectorOffset, -1.0f, 1.0f);
            const float u = (vx + 1.0f) * 0.5f, w = (vy + 1.0f) * 0.5f;
            const float wD = (1.0f - u) * (1.0f - w);
            const float wC = u * (1.0f - w);
            const float wA = (1.0f - u) * w;
            const float wB = u * w;

            const float voiceIn = sA * wA + sB * wB + sC * wC + sD * wD;
            const float filtered = (vs.filterType == VF_STOCK)
                ? v.filter.process(voiceIn)
                : v.varFilt.process(voiceIn, vs.filterType, vs.resonance);

            const float amp = v.ampEnv.process();
            const float velScale = 0.5f + 0.5f * v.velNorm;

            mix += filtered * amp * velScale * VS_VOICE_LEVEL[i];
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;

        // MASTER is pure linear gain after a fixed pre-saturator drive
        // constant, matching every other engine's own fix for the same
        // "volume knob also controls distortion" bug.
        const float VS_MIX_DRIVE = 1.4f;
        float out = fastTanh(mix * VS_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, vs.tremDepth, vs.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, vs.dlyFb, vs.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * vs.revMix; r += rr * vs.revMix;
        }
        l *= vs.master; r *= vs.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

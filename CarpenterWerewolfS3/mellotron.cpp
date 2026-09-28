#include "mellotron.h"
#include "engine.h"
#include "voicealloc.h"
#include "fx.h"
#include <string.h>

// ============================================================================
//  mellotron.cpp — panel-as-data, presets, and the generic engine adapter.
// ============================================================================

enum MelParamId : uint8_t {
    MEL_SOUND = 0,
    MEL_ATTACK, MEL_RELEASE,
    MEL_WOW_RATE, MEL_WOW_DEPTH,
    MEL_TONE, MEL_NOISE,
    MEL_FILTER_CUTOFF, MEL_FILTER_TYPE, MEL_FILTER_RES,
    MEL_MASTER, MEL_BEND_RANGE,

    MEL_TREM_RATE, MEL_TREM_DEPTH, MEL_TREM_MIX,
    MEL_DLY_TIME, MEL_DLY_FB, MEL_DLY_MIX, MEL_REV_SIZE, MEL_REV_MIX,

    MEL_ARP_ENABLE, MEL_ARP_PATTERN, MEL_ARP_RATE, MEL_ARP_GATE,
    MEL_ARP_OCTAVES, MEL_ARP_DIR, MEL_ARP_LATCH, MEL_ARP_SYNC,

    MEL_NUM_PARAMS
};

static const char* const MEL_SOUND_NAMES[3] = { "CHOIR", "STRINGS", "BOTH" };
static const char* const MEL_ONOFF[2]       = { "OFF", "ON" };
static const char* const MEL_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

#define MELROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [MEL_NUM_PARAMS]: an under-filled row list would
// otherwise silently zero-pad the remainder instead of failing to build.
static const ParamInfo MEL_PARAM_INFO[] = {
    MELROW("SOUND",    PK_ENUM,   MEL_SOUND_NAMES, 3, 0, 2,  20),
    MELROW("ATTACK",   PK_MS,     nullptr, 0,   10, 400,  21),
    MELROW("RELEASE",  PK_MS,     nullptr, 0,   10, 500,  22),
    MELROW("WOW RATE", PK_HZ,     nullptr, 0,    0, 6,    23),
    MELROW("WOW DEPTH",PK_CONT,   nullptr, 0,    0, 127,  24),
    MELROW("TONE",     PK_CONT,   nullptr, 0,    0, 127,  25),
    MELROW("TAPE HISS",PK_CONT,   nullptr, 0,    0, 127,  26),
    MELROW("CUTOFF",   PK_HZ,     nullptr, 0,   20, 18000, 45),
    MELROW("FILT TYPE",PK_ENUM,   VAR_FILTER_NAMES, VF_NUM_MODES, 0, VF_NUM_MODES - 1, 46),
    MELROW("RESONANCE",PK_CONT,   nullptr, 0,    0, 127,  47),
    MELROW("MASTER",   PK_CONT,   nullptr, 0,    0, 127,  27),
    MELROW("BEND RNG", PK_INT,    nullptr, 0,    1, 24,   28),
    MELROW("TREM RATE",PK_HZ,     nullptr, 0,    0, 20,   29),
    MELROW("TREM DEPTH",PK_CONT,  nullptr, 0,    0, 127,  30),
    MELROW("TREM MIX", PK_CONT,   nullptr, 0,    0, 127,  31),
    MELROW("DLY TIME", PK_MS,     nullptr, 0,   20, 1200, 32),
    MELROW("DLY FDBK", PK_CONT,   nullptr, 0,    0, 127,  33),
    MELROW("DLY MIX",  PK_CONT,   nullptr, 0,    0, 127,  34),
    MELROW("REV SIZE", PK_CONT,   nullptr, 0,    0, 127,  35),
    MELROW("REV MIX",  PK_CONT,   nullptr, 0,    0, 127,  36),
    MELROW("ARP ON",   PK_TOGGLE, MEL_ONOFF, 2, 0, 1,     37),
    MELROW("ARP PATT", PK_INT,    nullptr, 0,    0, 7,    38),
    MELROW("ARP RATE", PK_CONT,   nullptr, 0,    0, 127,  39),
    MELROW("ARP GATE", PK_CONT,   nullptr, 0,    0, 127,  40),
    MELROW("ARP OCT",  PK_INT,    nullptr, 0,    1, 4,    41),
    MELROW("ARP DIR",  PK_ENUM,   MEL_ARP_DIR_NAMES, 6, 0, 5, 42),
    MELROW("ARP LATCH",PK_TOGGLE, MEL_ONOFF, 2, 0, 1,     43),
    MELROW("ARP SYNC", PK_TOGGLE, MEL_ONOFF, 2, 0, 1,     44),
};
static_assert(sizeof(MEL_PARAM_INFO) / sizeof(MEL_PARAM_INFO[0]) == MEL_NUM_PARAMS,
              "MEL_PARAM_INFO must have exactly MEL_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");

static float melRaw[MEL_NUM_PARAMS];

struct MelDecoded {
    uint8_t sound;
    float atkRate, relRate;
    float wowHz, wowDepth;
    float toneG;      // one-pole lowpass coefficient
    float noiseLvl;
    float cutoffHz; uint8_t filterType; float resonance;
    float master; float bendRangeSemis;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static MelDecoded mel;

static void melDecode() {
    mel.sound = (uint8_t)(melRaw[MEL_SOUND] * 2.999f);
    // ATTACK/RELEASE knobs read directly in milliseconds: the tape motor's
    // spin-up/brake time, not a VCA envelope's shape.
    {
        float atkMs = 10.0f + melRaw[MEL_ATTACK] * 390.0f;
        float relMs = 10.0f + melRaw[MEL_RELEASE] * 490.0f;
        mel.atkRate = 1.0f / (atkMs * 0.001f * SAMPLE_RATE_F);
        mel.relRate = 1.0f / (relMs * 0.001f * SAMPLE_RATE_F);
    }
    mel.wowHz = melRaw[MEL_WOW_RATE] * 6.0f;
    mel.wowDepth = melRaw[MEL_WOW_DEPTH] * 0.02f;   // up to +/-2% speed wobble
    float toneHz = 800.0f + melRaw[MEL_TONE] * 7000.0f;
    mel.toneG = 1.0f - expf(-2.0f * (float)M_PI * toneHz * INV_SAMPLE_RATE);
    mel.noiseLvl = melRaw[MEL_NOISE] * 0.15f;
    mel.cutoffHz = 20.0f * fastPow2(melRaw[MEL_FILTER_CUTOFF] * 9.8f);   // 20 Hz .. ~18 kHz
    mel.filterType = (uint8_t)(melRaw[MEL_FILTER_TYPE] * ((float)VF_NUM_MODES - 0.001f));
    mel.resonance = melRaw[MEL_FILTER_RES];
    mel.master = melRaw[MEL_MASTER];
    mel.bendRangeSemis = 1.0f + melRaw[MEL_BEND_RANGE] * 23.0f;

    mel.tremRate = melRaw[MEL_TREM_RATE] * 20.0f;
    mel.tremDepth = melRaw[MEL_TREM_DEPTH];
    mel.tremMix = melRaw[MEL_TREM_MIX];
    mel.dlyTimeMs = 20.0f + melRaw[MEL_DLY_TIME] * 1180.0f;
    mel.dlyFb = melRaw[MEL_DLY_FB];
    mel.dlyMix = melRaw[MEL_DLY_MIX];
    mel.revSize = melRaw[MEL_REV_SIZE];
    mel.revMix = melRaw[MEL_REV_MIX];
}

// See param_types.h's EI(idx, n) -- the same raw-byte-as-proportion decode
// convention every other engine's PK_ENUM/PK_TOGGLE reads applies here too
// (melDecode()'s SOUND field, the shared arp reader in CarpenterSynth.ino).
// ATTACK/RELEASE are plain continuous knobs (not enums), but the original
// SLOW SWELL preset set them to 250 against this array's 0..127 raw range --
// the same "assumed a 0..255 scale" mistake as Minimoog/Prophet-5's detune
// knobs, just on a continuous rather than enum field: melApplyDeltas() never
// clamps, so raw 250 decoded to v=1.97, nearly double the panel's own
// documented 400/500 ms ceiling. Capped at 127 (the true max) below.
static const uint8_t MEL_INIT[] = {
    0,            // sound: choir
    25,60,        // attack/release
    30,25,        // wow rate/depth
    70,10,        // tone/hiss
    127,0,0,      // filter: wide open, STOCK (= no filter), no resonance
    110,20,       // master/bend
    0,0,0, 0,0,0,0,0,
    0,0,64,80,0,0,0,0,
};
static_assert(sizeof(MEL_INIT) / sizeof(MEL_INIT[0]) == MEL_NUM_PARAMS,
              "MEL_INIT must have exactly MEL_NUM_PARAMS values");

#define MEL_PRESET(nm, sym) { nm, sym, (uint8_t)(sizeof(sym) / sizeof(PresetDelta)) }

// ── Choir ──
static const PresetDelta MEL_P_CLASSICCHOIR[] = { {MEL_ATTACK,50},{MEL_RELEASE,90},{MEL_TONE,60} };
static const PresetDelta MEL_P_FASTCHOIR[]    = { {MEL_ATTACK,5},{MEL_RELEASE,20},{MEL_TONE,80} };
static const PresetDelta MEL_P_GOTHICCHOIR[]  = { {MEL_ATTACK,80},{MEL_RELEASE,120},{MEL_WOW_DEPTH,60},{MEL_TONE,30} };
static const PresetDelta MEL_P_ANGELICCHOIR[] = { {MEL_ATTACK,40},{MEL_RELEASE,100},{MEL_TONE,100},{MEL_REV_MIX,60},{MEL_REV_SIZE,110} };
static const PresetDelta MEL_P_SPACECHOIR[]   = { {MEL_ATTACK,110},{MEL_RELEASE,127},{MEL_TONE,70},{MEL_DLY_MIX,40},{MEL_DLY_TIME,70},{MEL_REV_MIX,80},{MEL_REV_SIZE,127} };
static const PresetDelta MEL_P_CHOIRVIBRATO[] = { {MEL_WOW_RATE,60},{MEL_WOW_DEPTH,90},{MEL_TONE,65} };
static const PresetDelta MEL_P_DRYCHOIR[]     = { {MEL_ATTACK,15},{MEL_RELEASE,40},{MEL_TONE,55},{MEL_WOW_DEPTH,10} };
static const PresetDelta MEL_P_HISSCHOIR[]    = { {MEL_NOISE,70},{MEL_TONE,40},{MEL_ATTACK,20},{MEL_RELEASE,50} };
static const PresetDelta MEL_P_CHOIRPAD[]     = { {MEL_ATTACK,127},{MEL_RELEASE,127},{MEL_TONE,60},{MEL_REV_MIX,50},{MEL_TREM_MIX,30} };
static const PresetDelta MEL_P_HAUNTEDCHOIR[] = { {MEL_ATTACK,100},{MEL_RELEASE,127},{MEL_TONE,25},{MEL_WOW_DEPTH,50},{MEL_REV_MIX,70},{MEL_REV_SIZE,127} };
static const PresetDelta MEL_P_WARMFILTER[]   = { {MEL_ATTACK,40},{MEL_RELEASE,80},{MEL_TONE,50},{MEL_FILTER_CUTOFF,70},{MEL_FILTER_TYPE,EI(6,16)},{MEL_FILTER_RES,40} };
static const PresetDelta MEL_P_FORMANTCHOIR[] = { {MEL_ATTACK,30},{MEL_RELEASE,60},{MEL_FILTER_CUTOFF,90},{MEL_FILTER_TYPE,EI(13,16)},{MEL_FILTER_RES,60} };

// ── Strings ──
static const PresetDelta MEL_P_STRINGS[]      = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,60},{MEL_RELEASE,90},{MEL_TONE,90} };
static const PresetDelta MEL_P_BRIGHTSTRINGS[]= { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,20},{MEL_RELEASE,50},{MEL_TONE,110} };
static const PresetDelta MEL_P_DARKSTRINGS[]  = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,50},{MEL_RELEASE,90},{MEL_TONE,25} };
static const PresetDelta MEL_P_FASTSTRINGS[]  = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,3},{MEL_RELEASE,15},{MEL_TONE,80} };
static const PresetDelta MEL_P_SLOWSTRINGS[]  = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,110},{MEL_RELEASE,120},{MEL_TONE,55} };
static const PresetDelta MEL_P_CATHEDRAL[]    = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,90},{MEL_RELEASE,110},{MEL_TONE,45},{MEL_REV_MIX,70},{MEL_REV_SIZE,127} };
static const PresetDelta MEL_P_STRINGSVIB[]   = { {MEL_SOUND,EI(1,3)},{MEL_WOW_RATE,50},{MEL_WOW_DEPTH,80},{MEL_TONE,60} };
static const PresetDelta MEL_P_STRINGSECTION[]= { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,35},{MEL_RELEASE,70},{MEL_TONE,65},{MEL_TREM_MIX,45},{MEL_TREM_RATE,20} };

// ── Choir + strings ──
static const PresetDelta MEL_P_BOTH[]      = { {MEL_SOUND,EI(2,3)},{MEL_ATTACK,40},{MEL_WOW_DEPTH,40} };
static const PresetDelta MEL_P_LUSHSTRINGS[]= { {MEL_SOUND,EI(2,3)},{MEL_ATTACK,60},{MEL_RELEASE,100},{MEL_TONE,70},{MEL_TREM_MIX,35},{MEL_REV_MIX,45} };
static const PresetDelta MEL_P_BOTHBRIGHT[] = { {MEL_SOUND,EI(2,3)},{MEL_ATTACK,15},{MEL_RELEASE,45},{MEL_TONE,100} };
static const PresetDelta MEL_P_WARMBOTH[]   = { {MEL_SOUND,EI(2,3)},{MEL_ATTACK,45},{MEL_RELEASE,80},{MEL_TONE,35} };
static const PresetDelta MEL_P_DRONEPAD[]   = { {MEL_SOUND,EI(2,3)},{MEL_ATTACK,127},{MEL_RELEASE,127},{MEL_TONE,40},{MEL_REV_MIX,80},{MEL_REV_SIZE,127},{MEL_DLY_MIX,35},{MEL_DLY_TIME,90} };

// ── Swells / textures ──
static const PresetDelta MEL_P_SWELL[]      = { {MEL_ATTACK,127},{MEL_RELEASE,127},{MEL_WOW_DEPTH,60},{MEL_TONE,50} };
static const PresetDelta MEL_P_UNDERWATER[] = { {MEL_WOW_RATE,50},{MEL_WOW_DEPTH,127},{MEL_TONE,20},{MEL_ATTACK,40},{MEL_RELEASE,80} };
static const PresetDelta MEL_P_TAPEECHO[]   = { {MEL_ATTACK,25},{MEL_RELEASE,60},{MEL_TONE,60},{MEL_DLY_MIX,55},{MEL_DLY_FB,60},{MEL_DLY_TIME,45} };
static const PresetDelta MEL_P_LOFI[]       = { {MEL_NOISE,100},{MEL_WOW_DEPTH,70},{MEL_TONE,35},{MEL_ATTACK,30},{MEL_RELEASE,60} };

// ── Arp ──
static const PresetDelta MEL_P_ARPCHOIR[]   = { {MEL_ATTACK,10},{MEL_RELEASE,30},{MEL_TONE,65},{MEL_ARP_ENABLE,EI(1,2)},{MEL_ARP_RATE,80},{MEL_ARP_GATE,70} };
static const PresetDelta MEL_P_ARPSTRINGS[] = { {MEL_SOUND,EI(1,3)},{MEL_ATTACK,8},{MEL_RELEASE,25},{MEL_TONE,75},{MEL_ARP_ENABLE,EI(1,2)},{MEL_ARP_RATE,90},{MEL_ARP_GATE,80},{MEL_ARP_OCTAVES,48},{MEL_ARP_DIR,EI(2,6)} };

static const Preset MEL_PRESETS[] = {
    MEL_PRESET("MALE CHOIR",    MEL_P_CLASSICCHOIR),
    MEL_PRESET("FAST CHOIR",    MEL_P_FASTCHOIR),
    MEL_PRESET("GOTHIC CHOIR",  MEL_P_GOTHICCHOIR),
    MEL_PRESET("ANGELIC CHOIR", MEL_P_ANGELICCHOIR),
    MEL_PRESET("SPACE CHOIR",   MEL_P_SPACECHOIR),
    MEL_PRESET("CHOIR VIBRATO", MEL_P_CHOIRVIBRATO),
    MEL_PRESET("DRY CHOIR",     MEL_P_DRYCHOIR),
    MEL_PRESET("TAPE HISS CHR", MEL_P_HISSCHOIR),
    MEL_PRESET("CHOIR PAD",     MEL_P_CHOIRPAD),
    MEL_PRESET("HAUNTED CHOIR", MEL_P_HAUNTEDCHOIR),
    MEL_PRESET("WARM FILTER",   MEL_P_WARMFILTER),
    MEL_PRESET("FORMANT CHOIR", MEL_P_FORMANTCHOIR),
    MEL_PRESET("TAPE STRINGS",  MEL_P_STRINGS),
    MEL_PRESET("BRIGHT STRGS",  MEL_P_BRIGHTSTRINGS),
    MEL_PRESET("DARK STRINGS",  MEL_P_DARKSTRINGS),
    MEL_PRESET("FAST STRINGS",  MEL_P_FASTSTRINGS),
    MEL_PRESET("SLOW STRINGS",  MEL_P_SLOWSTRINGS),
    MEL_PRESET("CATHEDRAL STR", MEL_P_CATHEDRAL),
    MEL_PRESET("STRINGS VIB",   MEL_P_STRINGSVIB),
    MEL_PRESET("STRING SECT",   MEL_P_STRINGSECTION),
    MEL_PRESET("CHOIR+STRINGS", MEL_P_BOTH),
    MEL_PRESET("LUSH STRINGS",  MEL_P_LUSHSTRINGS),
    MEL_PRESET("BOTH BRIGHT",   MEL_P_BOTHBRIGHT),
    MEL_PRESET("WARM BOTH",     MEL_P_WARMBOTH),
    MEL_PRESET("DRONE PAD",     MEL_P_DRONEPAD),
    MEL_PRESET("SLOW SWELL",    MEL_P_SWELL),
    MEL_PRESET("UNDERWATER",    MEL_P_UNDERWATER),
    MEL_PRESET("TAPE ECHO",     MEL_P_TAPEECHO),
    MEL_PRESET("LO-FI TAPE",    MEL_P_LOFI),
    MEL_PRESET("ARP CHOIR",     MEL_P_ARPCHOIR),
    MEL_PRESET("ARP STRINGS",   MEL_P_ARPSTRINGS),
};
#define MEL_NUM_PRESETS (sizeof(MEL_PRESETS) / sizeof(MEL_PRESETS[0]))

static void melApplyDeltas(const PresetDelta* d, uint8_t n) {
    for (uint8_t i = 0; i < MEL_NUM_PARAMS; i++) melRaw[i] = (float)MEL_INIT[i] * (1.0f / 127.0f);
    for (uint8_t i = 0; i < n; i++) melRaw[d[i].param] = (float)d[i].value * (1.0f / 127.0f);
    melDecode();
}

static const EnginePageDef MEL_PAGES[] = {
    { "TAPE",       MEL_SOUND,        7 },
    { "FILTER",     MEL_FILTER_CUTOFF,3 },
    { "VOICING",    MEL_MASTER,       2 },
    { "OUTPUT",     MEL_TREM_RATE,    8 },
    { "ARP",        MEL_ARP_ENABLE,   8 },
};
#define MEL_NUM_PAGES (sizeof(MEL_PAGES) / sizeof(MEL_PAGES[0]))

// ─── Voices ─────────────────────────────────────────────────────────────
static MelVoice melv[MEL_VOICES];
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.
static float       melTonePoleL = 0.0f;
extern VoiceAlloc va;

void melEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }
void melEngineActivate() {
    for (int i = 0; i < MEL_VOICES; i++) melv[i] = MelVoice();
    melTonePoleL = 0.0f;
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
}

void melParamsInitE() { melApplyDeltas(nullptr, 0); }
void melSetParameterE(uint8_t id, float v) { if (id < MEL_NUM_PARAMS) { melRaw[id] = v; melDecode(); } }
float melGetParameterE(uint8_t id) { return (id < MEL_NUM_PARAMS) ? melRaw[id] : 0.0f; }
void melFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= MEL_NUM_PARAMS) { buf[0] = 0; return; }
    const ParamInfo& pi = MEL_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(melRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(melRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool melParamIsLockableE(uint8_t id) { return id < MEL_NUM_PARAMS; }
uint8_t melNumParamsE() { return MEL_NUM_PARAMS; }
// See engine_cs80.cpp's cs80ParamInfoE for why this bounds-checks: a step
// lock's id can outlive an engine switch.
static const ParamInfo MEL_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* melParamInfoE(uint8_t id) {
    return (id < MEL_NUM_PARAMS) ? &MEL_PARAM_INFO[id] : &MEL_INVALID_PARAM;
}
uint8_t melNumPresetsE() { return (uint8_t)MEL_NUM_PRESETS; }
const char* melPresetNameE(uint8_t idx) { return MEL_PRESETS[idx].name; }
void melLoadPresetE(uint8_t idx) { melApplyDeltas(MEL_PRESETS[idx].deltas, MEL_PRESETS[idx].n); }
void melLoadInitPatchE() { melApplyDeltas(nullptr, 0); }
void melSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < MEL_NUM_PARAMS; i++) out[i] = (uint8_t)(melRaw[i] * 255.0f + 0.5f);
}
void melRestoreParamsE(const uint8_t* in) {
    for (uint8_t i = 0; i < MEL_NUM_PARAMS; i++) melRaw[i] = (float)in[i] * (1.0f / 255.0f);
    melDecode();
}

uint8_t melNumVoicesE() { return MEL_VOICES; }
void    melVoiceInitE() { melEngineActivate(); }

void melNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= MEL_VOICES) return;
    MelVoice& v = melv[voiceIdx];
    float freq = noteToFreq(note);
    v.note = note; v.velocity = vel; v.gate = true;
    float baseRatioChoir = freq / MEL_CHOIR_BASE_HZ;
    float baseRatioStrings = freq / MEL_STRINGS_BASE_HZ;
    v.choir.incPerSample = baseRatioChoir * ((float)MEL_CHOIR_SR / SAMPLE_RATE_F);
    v.strings.incPerSample = baseRatioStrings * ((float)MEL_STRINGS_SR / SAMPLE_RATE_F);
    if (trigEnv) {
        v.choir.posSamples = 0.0f;
        v.strings.posSamples = 0.0f;
        v.env.trigger(mel.atkRate, mel.relRate);
    }
}
void melNoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= MEL_VOICES) return;
    melv[voiceIdx].gate = false;
    melv[voiceIdx].env.release();
}
void melAllOffE() { for (int i = 0; i < MEL_VOICES; i++) melNoteOffE(i); }
void melSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < MEL_VOICES) melv[voiceIdx].aftertouch = (float)pressure * (1.0f / 127.0f);
}

int melRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    (void)modWheel; (void)chanAftertouch;
    const float bendMul = fastPow2(pitchBend * mel.bendRangeSemis * (1.0f / 12.0f));
    const bool fxChorus = mel.tremMix > 0.004f;
    const bool fxDelay  = mel.dlyMix  > 0.004f;
    const bool fxReverb = mel.revMix  > 0.004f;
    g_fxChorus.setRate(mel.tremRate);
    g_fxReverb.setRoom(mel.revSize, 0.4f);
    g_fxDelay.setTimeMs(mel.dlyTimeMs, false);

    // CUTOFF/RESONANCE are plain knob positions here (no envelope amount or
    // keyboard tracking to modulate them, unlike the subtractive engines'
    // filters) -- nothing moves them faster than a knob turn, so recomputing
    // the coefficients once per block, for every voice, is enough.
    if (mel.filterType != VF_STOCK)
        for (int i = 0; i < MEL_VOICES; i++)
            melv[i].varFilt.setCutoff(mel.cutoffHz, mel.resonance, mel.filterType);

    static uint32_t wowPhase = 0;
    static uint32_t hissSeed = 0x7A9Eu;
    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        wowPhase += (uint32_t)(mel.wowHz * PHASE_PER_HZ);
        // Wow & flutter: an extra, independent speed wobble on top of the
        // loop's own baked-in modulation — see mellotron.h's header comment.
        float speedMul = bendMul * (1.0f + sineFromPhase(wowPhase) * mel.wowDepth);

        float mix = 0.0f;
        int sounding = 0;
        for (int i = 0; i < MEL_VOICES; i++) {
            MelVoice& v = melv[i];
            const bool alive = v.env.stage != MEL_E_IDLE;
            if (!alive) { va.busy[i] = 0; continue; }

            float sample = 0.0f;
            if (mel.sound != MEL_S_STRINGS) sample += v.choir.read(MEL_CHOIR_DATA, MEL_CHOIR_LEN);
            if (mel.sound != MEL_S_CHOIR)   sample += v.strings.read(MEL_STRINGS_DATA, MEL_STRINGS_LEN);
            if (mel.sound == MEL_S_BOTH) sample *= 0.5f;

            // Both tape heads always advance (whichever isn't audible this
            // block still needs to stay in sync for an instant SOUND switch),
            // each at its own note-rate ratio times the shared wow/bend speed.
            v.choir.posSamples += v.choir.incPerSample * speedMul;
            if (v.choir.posSamples >= (float)MEL_CHOIR_LEN) v.choir.posSamples -= (float)MEL_CHOIR_LEN;
            v.strings.posSamples += v.strings.incPerSample * speedMul;
            if (v.strings.posSamples >= (float)MEL_STRINGS_LEN) v.strings.posSamples -= (float)MEL_STRINGS_LEN;

            float filtered = (mel.filterType == VF_STOCK)
                ? sample : v.varFilt.process(sample, mel.filterType, mel.resonance);

            float amp = v.env.process();
            mix += filtered * amp;
            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;
        if (sounding > 0) mix *= INV_SQRT_N[sounding];

        hissSeed = hissSeed * 1664525u + 1013904223u;
        float hiss = (float)((int32_t)hissSeed) * 4.6566128752458e-10f * mel.noiseLvl;
        mix += hiss;

        melTonePoleL += mel.toneG * (mix - melTonePoleL);
        // MASTER used to be multiplied straight into the saturator below, so
        // turning the volume up also drove more distortion. MEL_MIX_DRIVE is
        // a fixed pre-saturator drive instead, tuned from a probe over every
        // stock preset (peaks ran 0.62-0.99 — a bank of tape-loop voices
        // sits in a narrower, hotter band than Minimoog/Prophet/ARP's
        // subtractive voices, but nowhere near the CS-80's). MASTER itself
        // is now applied as pure linear gain after the saturator and the
        // whole effect chain.
        const float MEL_MIX_DRIVE = 1.3f;
        float out = fastTanh(melTonePoleL * MEL_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, mel.tremDepth, mel.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, mel.dlyFb, mel.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * mel.revMix; r += rr * mel.revMix;
        }
        l *= mel.master; r *= mel.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }
    return active;
}

uint8_t melMasterParamIdE() { return MEL_MASTER; }
uint8_t melFilterCutoffParamIdE()    { return MEL_FILTER_CUTOFF; }
uint8_t melFilterResonanceParamIdE() { return MEL_FILTER_RES; }
uint8_t melNumPagesE() { return (uint8_t)MEL_NUM_PAGES; }
const EnginePageDef* melPageE(uint8_t idx) { return (idx < MEL_NUM_PAGES) ? &MEL_PAGES[idx] : &MEL_PAGES[0]; }

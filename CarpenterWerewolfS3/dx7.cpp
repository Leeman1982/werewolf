// ============================================================================
//  dx7.cpp — Yamaha DX7 engine, backed by Dexed/MSFA.  See dx7.h.
//
//  Layout of this file:
//    1. The Dexed adapter (and the one real bug it works around)
//    2. Parameters — the panel, and how each row reaches Dexed
//    3. Voice/ROM handling
//    4. Note events, mapped from this codebase's voice-index model onto
//       Dexed's own note-based allocator
//    5. Render — Dexed's float path into this codebase's FX chain
// ============================================================================

#include "dx7.h"
#include "dx7_rom.h"
#include "engine.h"
#include "fx.h"
#include "voicealloc.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

// Synth_Dexed's controllers.h uses std::max, and some Arduino cores #define
// max as a macro, which the preprocessor rewrites even inside "std::max(".
// Drop the macro forms before pulling the library in; nothing below needs
// Arduino's.  (Same guard MedusaDexed's own synth.cpp carries, for the same
// reason.)
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

// synth_dexed.h defines its own SAMPLE_RATE (44100) unconditionally, which
// would otherwise silently replace this machine's 48000 for the rest of this
// file -- Dexed would then be constructed at the wrong rate and every note
// would play flat with mistimed envelopes.  Capture the real rate first, then
// drop the library's macro so nothing below can pick it up by accident.
static const uint16_t DX_SAMPLE_RATE = (uint16_t)SAMPLE_RATE;
#undef SAMPLE_RATE          // ...so the library's own definition is not a redefinition
#include <synth_dexed.h>
#undef SAMPLE_RATE          // ...and so its 44100 cannot leak into the code below

extern VoiceAlloc va;

// ─── 1. The Dexed adapter ───────────────────────────────────────────────────
//
// Dexed expects to be subclassed (its own Teensy AudioStream wrapper normally
// fills that role), so getSamples() and the voice state are protected.  This
// adapter re-exposes what this engine needs — the same shape as Pico2Dexed's
// CDexedAdapter and MedusaDexed's own.
//
// It also carries settle(), which is not cosmetic.  MSFA's Env::update()
// contains this, verbatim and upstream:
//
//     if (down_) {
//         // for now we simply reset ourselves at level 3
//         int32_t newlevel = levels_[2];
//         ...
//         advance(2);
//     }
//
// i.e. a parameter refresh on a held note JUMPS that note's envelope straight
// to its sustain level.  Dexed applies exactly that refresh at the top of
// getSamples() whenever refreshVoice is pending — and loading a voice, or
// touching any parameter, sets refreshVoice.  So a note started AFTER a load
// but BEFORE the next render block is caught by it and skips its attack and
// decay entirely: it begins life at its sustain level.  For any patch whose
// sustain level is zero — every piano, bass, clav, harpsichord, bell and
// pluck in the factory ROM — that means the note is simply SILENT.
//
// Measured on the host across the whole factory library: 93 of the 256 ROM
// voices produced no output at all.  With settle() called after every load
// and every parameter write, 0 of 256 are silent and mean peak level is 4x
// higher.  tests/test.cpp locks that down (testDx7EveryRomVoiceSounds).
//
// The clear is conditional on nothing playing, which is the whole point: a
// knob turned while a note is held SHOULD still refresh that note (that is
// the upstream behaviour, jump-to-sustain and all).  What must not happen is
// a stale refresh sitting armed across a note-on.
class DxAdapter : public Dexed {
public:
    DxAdapter(uint8_t maxnotes, uint16_t rate) : Dexed(maxnotes, rate) {}
    using Dexed::getSamples;

    void settle() {
        if (getNumNotesPlaying() != 0) return;
        lfo.reset(data + 137);      // the work the pending refresh owed us
        refreshVoice = false;
    }
};

// A real object at file scope, deliberately not lazily new'd on first use.
// engineSwitchTo() assigns currentEngine BEFORE calling the incoming engine's
// voiceInit, so core1 can enter dxRenderBlockE() in between -- and a lazy
// constructor would then be racing core0's, on the heap, from an audio
// callback.  Building it during static init (single-threaded, before setup()
// and before core1 starts) removes that window entirely, at the cost of ~6 KB
// held while another engine is live.
static DxAdapter dxd(DX_VOICES, DX_SAMPLE_RATE);
static inline DxAdapter& dex() { return dxd; }

// Panel OP1..OP6 -> Dexed's data-order op index.  DX7 voice data starts with
// OP6, so the mapping is a reversal.  See dx7.h.
static inline uint8_t dxOpToDexed(uint8_t panelOp0based) { return (uint8_t)(5 - panelOp0based); }

// ─── 2. Parameters ──────────────────────────────────────────────────────────
// Every row below is a REAL DX7 parameter with an exact Dexed setter and
// getter — no macros standing in for groups of them, and nothing that only
// exists in this UI.  That is what makes the read-back in dxRefreshFromVoice()
// possible: after loading a cartridge voice the panel shows that voice's
// actual values, so editing starts from the patch rather than from wherever
// the knobs happened to be left.
enum DxParamId : uint8_t {
    // Voice selection.  These two come FIRST on purpose: dxRestoreParamsE()
    // walks the array in order, so the cartridge voice is loaded before the
    // edits that sit on top of it are applied.
    DX_BANK = 0, DX_VOICE,

    // The six operator output levels — the DX7's own front-panel faders.
    DX_OP1_LEVEL, DX_OP2_LEVEL, DX_OP3_LEVEL, DX_OP4_LEVEL, DX_OP5_LEVEL, DX_OP6_LEVEL,

    DX_ALGORITHM, DX_FEEDBACK,

    DX_LFO_SPEED, DX_LFO_DELAY, DX_LFO_PMD, DX_LFO_AMD,
    DX_LFO_WAVE, DX_LFO_SYNC, DX_LFO_PMS,

    DX_PEG_R1, DX_PEG_R2, DX_PEG_R3, DX_PEG_R4,
    DX_PEG_L1, DX_PEG_L2, DX_PEG_L3, DX_PEG_L4,

    DX_TRANSPOSE, DX_TUNE, DX_MONO, DX_PORTA, DX_MASTER, DX_BEND_RANGE,

    DX_TREM_RATE, DX_TREM_DEPTH, DX_TREM_MIX,
    DX_DLY_TIME, DX_DLY_FB, DX_DLY_MIX, DX_REV_SIZE, DX_REV_MIX,

    DX_ARP_ENABLE, DX_ARP_PATTERN, DX_ARP_RATE, DX_ARP_GATE,
    DX_ARP_OCTAVES, DX_ARP_DIR, DX_ARP_LATCH, DX_ARP_SYNC,

    DX_NUM_PARAMS
};

static const char* const DX_ONOFF[2]         = { "OFF", "ON" };
static const char* const DX_LFO_WAVE_NAMES[6] = { "TRIANGLE","SAW DOWN","SAW UP","SQUARE","SINE","S&HOLD" };
static const char* const DX_ARP_DIR_NAMES[6] = { "UP","DOWN","UPDN","DNUP","AS PLAYED","RANDOM" };

// BANK's compile-time ceiling: the 8 ROM banks plus every filesystem bank
// this build will ever admit (MAX_FS_BANKS, config.h). Deliberately fixed at
// compile time, NOT the live dxTotalBankCount() below, even though the
// latter is what actually decides whether a given bank number resolves —
// see the two-part reasoning at their call sites (dxBankFromRaw/
// dxInstallVoice). In short: the front-panel encoder's PK_INT step size
// comes from this table's own dispMax (ui.cpp's generic SYNTH_EDIT handler),
// so if it tracked the LIVE count instead, plugging in a handful of
// cartridges would make the knob jump by several ROM banks per detent —
// which is the one everyday case (no cartridges yet) this must never
// regress. Extension banks beyond whatever's actually present just sit as
// dead positions at the high end of the range, same as any other panel
// selector's unused option would.
#define DX_MAX_TOTAL_BANKS (DX7_ROM_BANKS + MAX_FS_BANKS)

#define DXROW(nm, kind, opts, no, lo, hi, ccn) { nm, kind, opts, no, lo, hi, ccn }

// Deduced size, not [DX_NUM_PARAMS]: an under-filled row list would otherwise
// silently zero-pad the remainder instead of failing to build.
static const ParamInfo DX_PARAM_INFO[] = {
    // Not PK_ENUM: BANK's name list is only known at runtime (ROM names are
    // fixed, filesystem ones come from whatever's scanned), so it's a plain
    // number with a special-cased name lookup in dxFormatParameterE() below
    // instead — same shape VOICE already used for showing the patch name.
    DXROW("BANK",      PK_INT,    nullptr, 0,  0, DX_MAX_TOTAL_BANKS - 1, 20),
    DXROW("VOICE",     PK_INT,    nullptr, 0,  1, DX7_VOICES_PER_BANK, 21),
    DXROW("OP1 LEVEL", PK_INT,    nullptr, 0,  0, 99,   22),
    DXROW("OP2 LEVEL", PK_INT,    nullptr, 0,  0, 99,   23),
    DXROW("OP3 LEVEL", PK_INT,    nullptr, 0,  0, 99,   24),
    DXROW("OP4 LEVEL", PK_INT,    nullptr, 0,  0, 99,   25),
    DXROW("OP5 LEVEL", PK_INT,    nullptr, 0,  0, 99,   26),
    DXROW("OP6 LEVEL", PK_INT,    nullptr, 0,  0, 99,   27),
    DXROW("ALGORITHM", PK_INT,    nullptr, 0,  1, 32,   28),
    DXROW("FEEDBACK",  PK_INT,    nullptr, 0,  0, 7,    29),
    DXROW("LFO SPEED", PK_INT,    nullptr, 0,  0, 99,   30),
    DXROW("LFO DELAY", PK_INT,    nullptr, 0,  0, 99,   31),
    DXROW("LFO>PITCH", PK_INT,    nullptr, 0,  0, 99,   33),
    DXROW("LFO>AMP",   PK_INT,    nullptr, 0,  0, 99,   34),
    DXROW("LFO WAVE",  PK_ENUM,   DX_LFO_WAVE_NAMES, 6, 0, 5, 35),
    DXROW("LFO SYNC",  PK_TOGGLE, DX_ONOFF, 2, 0, 1,    36),
    DXROW("LFO P.SENS",PK_INT,    nullptr, 0,  0, 7,    37),
    DXROW("PEG RATE 1",PK_INT,    nullptr, 0,  0, 99,   39),
    DXROW("PEG RATE 2",PK_INT,    nullptr, 0,  0, 99,   40),
    DXROW("PEG RATE 3",PK_INT,    nullptr, 0,  0, 99,   41),
    DXROW("PEG RATE 4",PK_INT,    nullptr, 0,  0, 99,   42),
    DXROW("PEG LEVEL 1",PK_INT,   nullptr, 0,  0, 99,   43),
    DXROW("PEG LEVEL 2",PK_INT,   nullptr, 0,  0, 99,   44),
    DXROW("PEG LEVEL 3",PK_INT,   nullptr, 0,  0, 99,   45),
    DXROW("PEG LEVEL 4",PK_INT,   nullptr, 0,  0, 99,   46),
    DXROW("TRANSPOSE", PK_INT,    nullptr, 0, -24, 24,  47),
    DXROW("TUNE",      PK_CENTS,  nullptr, 0, -63, 63,  48),
    DXROW("MONO",      PK_TOGGLE, DX_ONOFF, 2, 0, 1,    49),
    DXROW("PORTA",     PK_INT,    nullptr, 0,  0, 99,   50),
    DXROW("MASTER",    PK_CONT,   nullptr, 0,  0, 127,  51),
    DXROW("BEND RNG",  PK_INT,    nullptr, 0,  0, 12,   52),
    DXROW("TREM RATE", PK_HZ,     nullptr, 0,  0, 20,   73),
    DXROW("TREM DEPTH",PK_CONT,   nullptr, 0,  0, 127,  74),
    DXROW("TREM MIX",  PK_CONT,   nullptr, 0,  0, 127,  75),
    DXROW("DLY TIME",  PK_MS,     nullptr, 0, 20, 1200, 76),
    DXROW("DLY FDBK",  PK_CONT,   nullptr, 0,  0, 127,  77),
    DXROW("DLY MIX",   PK_CONT,   nullptr, 0,  0, 127,  78),
    DXROW("REV SIZE",  PK_CONT,   nullptr, 0,  0, 127,  79),
    DXROW("REV MIX",   PK_CONT,   nullptr, 0,  0, 127,  80),
    DXROW("ARP ON",    PK_TOGGLE, DX_ONOFF, 2, 0, 1,    81),
    DXROW("ARP PATT",  PK_INT,    nullptr, 0,  0, 7,    82),
    DXROW("ARP RATE",  PK_CONT,   nullptr, 0,  0, 127,  83),
    DXROW("ARP GATE",  PK_CONT,   nullptr, 0,  0, 127,  84),
    DXROW("ARP OCT",   PK_INT,    nullptr, 0,  1, 4,    85),
    DXROW("ARP DIR",   PK_ENUM,   DX_ARP_DIR_NAMES, 6, 0, 5, 86),
    DXROW("ARP LATCH", PK_TOGGLE, DX_ONOFF, 2, 0, 1,    87),
    DXROW("ARP SYNC",  PK_TOGGLE, DX_ONOFF, 2, 0, 1,    88),
};
static_assert(sizeof(DX_PARAM_INFO) / sizeof(DX_PARAM_INFO[0]) == DX_NUM_PARAMS,
              "DX_PARAM_INFO must have exactly DX_NUM_PARAMS rows — "
              "add/remove a row in both the enum and this table together");
static_assert(DX_NUM_PARAMS <= MAX_ENGINE_PARAMS,
              "DX7 panel outgrew the preset/step-lock byte budget");

static float dxRaw[DX_NUM_PARAMS];

// The FX tail is this engine's own, exactly as before -- Dexed produces a dry
// mono voice and everything downstream of it is this codebase's.
struct DxOut {
    float master;
    float tremRate, tremDepth, tremMix, dlyTimeMs, dlyFb, dlyMix, revSize, revMix;
};
static DxOut dxo;

static char    dxVoiceName[11] = "INIT VOICE";
static uint8_t dxBank = 0, dxProg = 0;
// Set while a cartridge voice is being installed, so the parameter writes
// dxRefreshFromVoice() makes don't each try to push themselves back into
// Dexed (and, worse, re-load the voice when they touch BANK/VOICE).
static bool    dxLoading = false;

// 0..1 panel value <-> a Dexed 0..N integer.
static inline uint8_t dxRawToInt(float raw, int hi) {
    int v = (int)(raw * (float)hi + 0.5f);
    return (uint8_t)(v < 0 ? 0 : (v > hi ? hi : v));
}
static inline float dxIntToRaw(int v, int hi) { return (hi > 0) ? ((float)v / (float)hi) : 0.0f; }

// Storage for the extension hooks declared in dx7.h -- null until the sketch
// wires them to voicebank.cpp's real functions, which is also their
// permanent state in the host tests (see dx7.h's "Beyond the 8 ROM banks").
uint8_t (*dxExtBankCount)() = nullptr;
void    (*dxExtBankName)(uint8_t extBank, char* out, size_t outSize) = nullptr;
bool    (*dxExtGetVoice)(uint8_t extBank, uint8_t prog, uint8_t* out) = nullptr;

// ROM banks always exist; extension banks only count once something has
// actually been scanned in. This is the LIVE count -- how many of BANK's
// DX_MAX_TOTAL_BANKS compile-time positions currently resolve to a real
// voice -- as opposed to DX_MAX_TOTAL_BANKS itself, which is what the
// encoder's step size is deliberately still fixed against (see that
// #define's own comment).
static inline uint8_t dxTotalBankCount() {
    return (uint8_t)(DX7_ROM_BANKS + (dxExtBankCount ? dxExtBankCount() : 0));
}
static inline uint8_t dxBankFromRaw(float raw) { return dxRawToInt(raw, DX_MAX_TOTAL_BANKS - 1); }

// ─── 3. Voice / ROM handling ────────────────────────────────────────────────

static void dxDecodeOut() {
    dxo.master    = dxRaw[DX_MASTER];
    dxo.tremRate  = dxRaw[DX_TREM_RATE] * 20.0f;
    dxo.tremDepth = dxRaw[DX_TREM_DEPTH];
    dxo.tremMix   = dxRaw[DX_TREM_MIX];
    dxo.dlyTimeMs = 20.0f + dxRaw[DX_DLY_TIME] * 1180.0f;
    dxo.dlyFb     = dxRaw[DX_DLY_FB];
    dxo.dlyMix    = dxRaw[DX_DLY_MIX];
    dxo.revSize   = dxRaw[DX_REV_SIZE];
    dxo.revMix    = dxRaw[DX_REV_MIX];
}

// After a cartridge voice is installed, pull its real values back into the
// panel so the rows show the patch rather than the last thing the knobs held.
static void dxRefreshFromVoice() {
    DxAdapter& d = dex();
    dxLoading = true;
    for (int i = 0; i < 6; i++)
        dxRaw[DX_OP1_LEVEL + i] = dxIntToRaw(d.getOPOutputLevel(dxOpToDexed((uint8_t)i)), 99);
    dxRaw[DX_ALGORITHM] = dxIntToRaw(d.getAlgorithm(), 31);
    dxRaw[DX_FEEDBACK]  = dxIntToRaw(d.getFeedback(), 7);
    dxRaw[DX_LFO_SPEED] = dxIntToRaw(d.getLFOSpeed(), 99);
    dxRaw[DX_LFO_DELAY] = dxIntToRaw(d.getLFODelay(), 99);
    dxRaw[DX_LFO_PMD]   = dxIntToRaw(d.getLFOPitchModulationDepth(), 99);
    dxRaw[DX_LFO_AMD]   = dxIntToRaw(d.getLFOAmpModulationDepth(), 99);
    dxRaw[DX_LFO_WAVE]  = dxIntToRaw(d.getLFOWaveform(), 5);
    dxRaw[DX_LFO_SYNC]  = d.getLFOSync() ? 1.0f : 0.0f;
    dxRaw[DX_LFO_PMS]   = dxIntToRaw(d.getLFOPitchModulationSensitivity(), 7);
    for (int i = 0; i < 4; i++) {
        dxRaw[DX_PEG_R1 + i] = dxIntToRaw(d.getPitchRate((uint8_t)i), 99);
        dxRaw[DX_PEG_L1 + i] = dxIntToRaw(d.getPitchLevel((uint8_t)i), 99);
    }
    // Dexed stores transpose 0..48 with 24 = no shift; the panel row is
    // -24..+24, so raw 0.5 is centre.
    dxRaw[DX_TRANSPOSE] = dxIntToRaw(d.getTranspose(), 48);
    dxLoading = false;
}

// Install a voice by its FULL bank index -- 0..DX7_ROM_BANKS-1 is ROM,
// DX7_ROM_BANKS.. is a filesystem extension bank (see dxExt* in dx7.h).
// Everything that can silence a note happens here, in this order, and the
// order matters -- see settle(). If `bank` doesn't currently resolve to a
// real voice (past dxTotalBankCount() -- an unpopulated dead position at the
// high end of BANK's range, see DX_MAX_TOTAL_BANKS above -- or the extension
// isn't wired up, or the underlying file vanished/the disk is host-owned),
// the CURRENTLY LOADED voice is left exactly as it was: never replace a
// working sound with silence or garbage over an unreachable selection.
static void dxInstallVoice(uint8_t bank, uint8_t program) {
    DxAdapter& d = dex();
    program = (uint8_t)(program % DX7_VOICES_PER_BANK);
    if (bank >= dxTotalBankCount()) return;

    uint8_t packed[DX7_PACKED_VOICE];
    if (bank < DX7_ROM_BANKS) {
        // decodeVoice() takes a non-const pointer, and DX7_ROM is const (it
        // lives in flash, see dx7_rom.h), so copy the 128 packed bytes out
        // first.
        memcpy(packed, DX7_ROM[bank][program], DX7_PACKED_VOICE);
    } else {
        if (!dxExtGetVoice || !dxExtGetVoice((uint8_t)(bank - DX7_ROM_BANKS), program, packed))
            return;
    }
    dxBank = bank;
    dxProg = program;

    uint8_t unpacked[NUM_VOICE_PARAMETERS];
    memset(unpacked, 0, sizeof(unpacked));
    d.decodeVoice(unpacked, packed);
    d.loadVoiceParameters(unpacked);
    d.settle();

    for (int i = 0; i < 10; i++) {
        uint8_t c = packed[DX7_VOICE_NAME_OFF + i];
        dxVoiceName[i] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    dxVoiceName[10] = '\0';

    dxRefreshFromVoice();
}

const char* dxCurrentVoiceName() { return dxVoiceName; }
void dxSelectRomVoice(uint8_t bank, uint8_t program) {
    bank    = (uint8_t)(bank % DX7_ROM_BANKS);
    program = (uint8_t)(program % DX7_VOICES_PER_BANK);
    dxLoading = true;
    dxRaw[DX_BANK]  = dxIntToRaw(bank, DX_MAX_TOTAL_BANKS - 1);
    dxRaw[DX_VOICE] = dxIntToRaw(program, DX7_VOICES_PER_BANK - 1);
    dxLoading = false;
    dxInstallVoice(bank, program);
}

void dxRefreshCurrentBank() { dxInstallVoice(dxBank, dxProg); }

// Push one panel row into Dexed.  Rows that Dexed doesn't own (the FX tail,
// the arp tail, MASTER) fall through to dxDecodeOut().
static void dxApplyToDexed(uint8_t id) {
    DxAdapter& d = dex();
    switch (id) {
    case DX_BANK:
    case DX_VOICE:
        dxInstallVoice(dxBankFromRaw(dxRaw[DX_BANK]),
                       dxRawToInt(dxRaw[DX_VOICE], DX7_VOICES_PER_BANK - 1));
        return;                       // installs and refreshes everything else
    case DX_OP1_LEVEL: case DX_OP2_LEVEL: case DX_OP3_LEVEL:
    case DX_OP4_LEVEL: case DX_OP5_LEVEL: case DX_OP6_LEVEL:
        d.setOPOutputLevel(dxOpToDexed((uint8_t)(id - DX_OP1_LEVEL)), dxRawToInt(dxRaw[id], 99));
        break;
    case DX_ALGORITHM: d.setAlgorithm(dxRawToInt(dxRaw[id], 31)); break;
    case DX_FEEDBACK:  d.setFeedback(dxRawToInt(dxRaw[id], 7));   break;
    case DX_LFO_SPEED: d.setLFOSpeed(dxRawToInt(dxRaw[id], 99));  break;
    case DX_LFO_DELAY: d.setLFODelay(dxRawToInt(dxRaw[id], 99));  break;
    case DX_LFO_PMD:   d.setLFOPitchModulationDepth(dxRawToInt(dxRaw[id], 99)); break;
    case DX_LFO_AMD:   d.setLFOAmpModulationDepth(dxRawToInt(dxRaw[id], 99));   break;
    case DX_LFO_WAVE:  d.setLFOWaveform(dxRawToInt(dxRaw[id], 5)); break;
    case DX_LFO_SYNC:  d.setLFOSync(dxRaw[id] > 0.5f);            break;
    case DX_LFO_PMS:   d.setLFOPitchModulationSensitivity(dxRawToInt(dxRaw[id], 7)); break;
    case DX_PEG_R1: case DX_PEG_R2: case DX_PEG_R3: case DX_PEG_R4:
        d.setPitchRate((uint8_t)(id - DX_PEG_R1), dxRawToInt(dxRaw[id], 99)); break;
    case DX_PEG_L1: case DX_PEG_L2: case DX_PEG_L3: case DX_PEG_L4:
        d.setPitchLevel((uint8_t)(id - DX_PEG_L1), dxRawToInt(dxRaw[id], 99)); break;
    case DX_TRANSPOSE: d.setTranspose(dxRawToInt(dxRaw[id], 48)); break;
    case DX_TUNE:      d.setMasterTune((int8_t)(dxRawToInt(dxRaw[id], 126) - 63)); break;
    case DX_MONO:      d.setMonoMode(dxRaw[id] > 0.5f);           break;
    case DX_PORTA: {
        uint8_t t = dxRawToInt(dxRaw[id], 99);
        d.setPortamento(t > 0 ? 1 : 0, 0, t);
        break;
    }
    case DX_BEND_RANGE: d.setPitchbendRange(dxRawToInt(dxRaw[id], 12)); break;
    default: dxDecodeOut(); return;
    }
    // Every branch above wrote a voice parameter, which arms Dexed's
    // refresh -- see DxAdapter::settle().
    d.settle();
}

// ─── Presets = the factory cartridges ───────────────────────────────────────
// The first four ROM banks (ROM1A/1B/2A/2B — the two cartridges a DX7 shipped
// with) become this engine's preset list, 128 entries.  All eight banks stay
// reachable from the BANK row.  128 rather than all 256 because the engine
// interface's preset index is a uint8_t and dxNumPresetsE() returns one: 256
// does not fit in the count.
#define DX_NUM_PRESETS 128

static char dxPresetNameBuf[11];

uint8_t dxNumPresetsE() { return (uint8_t)DX_NUM_PRESETS; }
const char* dxPresetNameE(uint8_t idx) {
    uint8_t bank = (uint8_t)((idx / DX7_VOICES_PER_BANK) % DX7_ROM_BANKS);
    uint8_t prog = (uint8_t)(idx % DX7_VOICES_PER_BANK);
    for (int i = 0; i < 10; i++) {
        uint8_t c = DX7_ROM[bank][prog][DX7_VOICE_NAME_OFF + i];
        dxPresetNameBuf[i] = (c >= 32 && c < 127) ? (char)c : ' ';
    }
    dxPresetNameBuf[10] = '\0';
    // Trim the trailing pad the DX7's fixed-width names carry.
    for (int i = 9; i >= 0 && dxPresetNameBuf[i] == ' '; i--) dxPresetNameBuf[i] = '\0';
    return dxPresetNameBuf;
}

// Panel defaults for everything Dexed does NOT own.  The Dexed-owned rows are
// not defaulted here at all: they are read back from whichever cartridge voice
// is installed, which is the whole point of dxRefreshFromVoice().
static void dxResetOutputRows() {
    dxRaw[DX_MASTER]     = 100.0f / 127.0f;
    dxRaw[DX_TUNE]       = 0.5f;              // centre = no detune
    dxRaw[DX_MONO]       = 0.0f;
    dxRaw[DX_PORTA]      = 0.0f;
    dxRaw[DX_BEND_RANGE] = 2.0f / 12.0f;      // the DX7's own default
    dxRaw[DX_TREM_RATE]  = 0.25f;
    dxRaw[DX_TREM_DEPTH] = 0.0f;
    dxRaw[DX_TREM_MIX]   = 0.0f;
    dxRaw[DX_DLY_TIME]   = 0.25f;
    dxRaw[DX_DLY_FB]     = 0.3f;
    dxRaw[DX_DLY_MIX]    = 0.0f;
    dxRaw[DX_REV_SIZE]   = 0.5f;
    dxRaw[DX_REV_MIX]    = 0.0f;
    for (uint8_t i = DX_ARP_ENABLE; i < DX_NUM_PARAMS; i++) dxRaw[i] = 0.0f;
    dxRaw[DX_ARP_RATE]    = 0.5f;
    dxRaw[DX_ARP_GATE]    = 0.5f;
    dxRaw[DX_ARP_OCTAVES] = 0.0f;
    dxDecodeOut();
}

void dxLoadPresetE(uint8_t idx) {
    dxResetOutputRows();
    dxSelectRomVoice((uint8_t)((idx / DX7_VOICES_PER_BANK) % DX7_ROM_BANKS),
                     (uint8_t)(idx % DX7_VOICES_PER_BANK));
    DxAdapter& d = dex();
    d.setMasterTune(0);
    d.setMonoMode(false);
    d.setPortamento(0, 0, 0);
    d.setPitchbendRange(2);
    d.settle();
}

// ROM1A #1 is BRASS 1; the DX7 powered up on it, so this build does too.
void dxLoadInitPatchE() { dxLoadPresetE(0); }
void dxParamsInitE()    { dxLoadInitPatchE(); }

static const EnginePageDef DX_PAGES[] = {
    { "VOICE",      DX_BANK,       2 },
    { "OP LEVELS",  DX_OP1_LEVEL,  6 },
    { "ALGO",       DX_ALGORITHM,  2 },
    { "LFO",        DX_LFO_SPEED,  7 },
    { "PITCH EG",   DX_PEG_R1,     8 },
    { "VOICING",    DX_TRANSPOSE,  6 },
    { "OUTPUT",     DX_TREM_RATE,  8 },
    { "ARP",        DX_ARP_ENABLE, 8 },
};
#define DX_NUM_PAGES (sizeof(DX_PAGES) / sizeof(DX_PAGES[0]))

uint8_t dxMasterParamIdE() { return DX_MASTER; }
// The only engine here with no filter to give the front-panel pots: a DX7
// shapes its spectrum entirely by FM index, so there is no cutoff and no
// resonance to map to -- not even loosely.  Feedback is the nearest global
// brightness control, but it is an algorithm parameter with its own audible
// character, not a filter, and a knob labelled CUTOFF sweeping it would be a
// lie that also silently edits the patch.  So both pots are inert here, by
// way of the ENGINE_PARAM_NONE sentinel (param_types.h).
uint8_t dxFilterCutoffParamIdE()    { return ENGINE_PARAM_NONE; }
uint8_t dxFilterResonanceParamIdE() { return ENGINE_PARAM_NONE; }
uint8_t dxNumPagesE() { return (uint8_t)DX_NUM_PAGES; }
const EnginePageDef* dxPageE(uint8_t idx) { return (idx < DX_NUM_PAGES) ? &DX_PAGES[idx] : &DX_PAGES[0]; }

void dxSetParameterE(uint8_t id, float v) {
    if (id >= DX_NUM_PARAMS) return;
    dxRaw[id] = v;
    if (dxLoading) return;
    dxApplyToDexed(id);
    dxDecodeOut();
}
float dxGetParameterE(uint8_t id) { return (id < DX_NUM_PARAMS) ? dxRaw[id] : 0.0f; }

void dxFormatParameterE(uint8_t id, char* buf, size_t n) {
    if (id >= DX_NUM_PARAMS) { buf[0] = 0; return; }
    // The VOICE row shows the patch's own name rather than its number: on a
    // machine whose sounds ARE the factory library, "TUB BELLS" is the useful
    // readout and "18" is not.
    if (id == DX_VOICE) { snprintf(buf, n, "%s", dxVoiceName); return; }
    // BANK shows the cartridge's own name/label -- ROM name, filesystem
    // filename, or "--" for one of the unpopulated dead positions past
    // dxTotalBankCount() (see DX_MAX_TOTAL_BANKS above).
    if (id == DX_BANK) {
        uint8_t b = dxBankFromRaw(dxRaw[id]);
        if (b < DX7_ROM_BANKS) snprintf(buf, n, "%s", DX7_ROM_BANK_NAMES[b]);
        else if (b < dxTotalBankCount() && dxExtBankName) dxExtBankName((uint8_t)(b - DX7_ROM_BANKS), buf, n);
        else snprintf(buf, n, "--");
        return;
    }
    const ParamInfo& pi = DX_PARAM_INFO[id];
    if (pi.kind == PK_ENUM || pi.kind == PK_TOGGLE) {
        int i = (int)(dxRaw[id] * (pi.numOpts - 0.001f));
        if (i < 0) i = 0; else if (i >= pi.numOpts) i = pi.numOpts - 1;
        snprintf(buf, n, "%s", pi.opts[i]);
    } else {
        int val = pi.dispMin + (int)(dxRaw[id] * (pi.dispMax - pi.dispMin));
        snprintf(buf, n, "%d", val);
    }
}
bool dxParamIsLockableE(uint8_t id) { return id < DX_NUM_PARAMS; }
uint8_t dxNumParamsE() { return DX_NUM_PARAMS; }
static const ParamInfo DX_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* dxParamInfoE(uint8_t id) {
    return (id < DX_NUM_PARAMS) ? &DX_PARAM_INFO[id] : &DX_INVALID_PARAM;
}

void dxSnapshotParamsE(uint8_t* out) {
    for (uint8_t i = 0; i < DX_NUM_PARAMS; i++) out[i] = (uint8_t)(dxRaw[i] * 255.0f + 0.5f);
}
void dxRestoreParamsE(const uint8_t* in) {
    // In id order, which is why BANK/VOICE are ids 0 and 1: the cartridge
    // voice goes in first, then every edit that was saved on top of it --
    // including the edits dxRefreshFromVoice() would otherwise have just
    // overwritten with the ROM's own values.
    for (uint8_t i = 0; i < DX_NUM_PARAMS; i++)
        dxSetParameterE(i, (float)in[i] * (1.0f / 255.0f));
}

// ─── 4. Notes ───────────────────────────────────────────────────────────────
// This codebase allocates a voice INDEX and tells the engine which slot to
// play; Dexed allocates internally, by note.  The bridge is slotNote[]: which
// pitch this codebase believes each slot is holding, so a note-off (or the
// CPU guard's force-release, which passes only an index) can be turned back
// into the keyup(pitch) Dexed wants.
static int8_t dxSlotNote[DX_VOICES];

// Last values pushed into Dexed's controllers, so a block that changes
// nothing costs nothing.  Seeded out of range so the first block always
// pushes; reset on engine activate for the same reason.
static float dxBendCache = 2.0f, dxModCache = -1.0f, dxAtCache = -1.0f;

uint8_t dxNumVoicesE() { return DX_VOICES; }

void dxEngineInit() { g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin(); }

void dxEngineActivate() {
    DxAdapter& d = dex();
    d.panic();
    // MSFA is the reference core -- the one Dexed's own desktop plugin uses.
    // Dexed's constructor leaves it on MKI (a Mark-I DX7 variant), so pick
    // explicitly rather than inheriting that.
    d.setEngineType(MSFA);
    dxBendCache = 2.0f; dxModCache = -1.0f; dxAtCache = -1.0f;   // force a re-push
    for (int i = 0; i < DX_VOICES; i++) dxSlotNote[i] = -1;
    g_fxChorus.begin(); g_fxDelay.begin(); g_fxReverb.begin();
    d.settle();
}
void dxVoiceInitE() { dxEngineActivate(); }

void dxNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= DX_VOICES) return;
    if (note < 0 || note > 127) return;
    DxAdapter& d = dex();
    // A slot being reused without an intervening note-off (this codebase's
    // voice allocator reuses the voice already holding a pitch) must release
    // the old pitch, or Dexed keeps it sounding forever.
    if (dxSlotNote[voiceIdx] >= 0 && dxSlotNote[voiceIdx] != note)
        d.keyup((uint8_t)dxSlotNote[voiceIdx]);
    dxSlotNote[voiceIdx] = (int8_t)note;
    // trigEnv is deliberately ignored: on the analogue engines it means
    // "re-pitch without re-attacking", and Dexed has no way to start a note
    // without triggering it.  Skipping the keydown would drop the note
    // entirely, which is far worse than an extra attack -- and the DX7's own
    // answer to legato is its portamento/mono mode, on the VOICING page.
    (void)trigEnv;
    d.keydown((uint8_t)note, vel ? vel : 1);
}

void dxNoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= DX_VOICES) return;
    int8_t n = dxSlotNote[voiceIdx];
    if (n < 0) return;
    dxSlotNote[voiceIdx] = -1;
    dex().keyup((uint8_t)n);
}

void dxAllOffE() {
    for (int i = 0; i < DX_VOICES; i++) dxSlotNote[i] = -1;
    dex().notesOff();
}

void dxSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    // Deliberately nothing.  Dexed's aftertouch is a GLOBAL controller, not a
    // per-note one, and dxRenderBlockE() already pushes the channel value once
    // per block.  Writing it here as well would give the same controller two
    // writers racing each other for no gain -- the sketch sends channel
    // pressure, so every slot carries the identical value anyway.
    (void)voiceIdx; (void)pressure;
}

// ─── 5. Render ──────────────────────────────────────────────────────────────

int dxRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch) {
    DxAdapter& d = dex();

    // Dexed's own controllers, refreshed once per block rather than per
    // sample: these are 14-bit/7-bit MIDI controls on the real machine, not
    // audio-rate modulation.  The caches are file-scope and reset by
    // dxEngineActivate(), so re-selecting this engine always re-pushes them
    // rather than trusting a value cached under a previous session.
    if (pitchBend != dxBendCache) {
        dxBendCache = pitchBend;
        // -1..+1 -> the 0..16383 14-bit bend Dexed expects, 8192 centred.
        int b = (int)(pitchBend * 8191.0f);
        if (b < -8192) b = -8192; else if (b > 8191) b = 8191;
        d.setPitchbend((int16_t)b);          // signed, 0 = centre
    }
    if (modWheel != dxModCache) {
        dxModCache = modWheel;
        d.setModWheel((uint8_t)(modWheel * 127.0f));
        d.ControllersRefresh();
    }
    if (chanAftertouch != dxAtCache) {
        dxAtCache = chanAftertouch;
        d.setAftertouch((uint8_t)(chanAftertouch * 127.0f));
        d.ControllersRefresh();
    }

    // The FLOAT render path, not the int16 one.  Both exist in this Dexed
    // build; the int16 path shifts its 32-bit accumulator down by 16 with no
    // saturation (`audiobuf[j] >> (16 + simple_gain)`), which lands a
    // full-level factory brass patch at about -24 dBFS and quantises quieter
    // patches to nothing at all.  The float path shifts by 13 WITH saturation
    // (signed_saturate_rshift(x >> 4, 24, 9) / 32768) and is 8x hotter for it.
    // RP2350's Cortex-M33 has a single-precision FPU, so the float path is
    // not the expensive choice here either.
    static float dxBuf[AUDIO_BLOCK];
    static_assert(AUDIO_BLOCK % _N_ == 0,
                  "AUDIO_BLOCK must be a multiple of Dexed's _N_ (64) — its "
                  "render loop steps in whole _N_ blocks and overruns otherwise");
    d.getSamples(dxBuf, AUDIO_BLOCK);

    const bool fxChorus = dxo.tremMix > 0.004f;
    const bool fxDelay  = dxo.dlyMix  > 0.004f;
    const bool fxReverb = dxo.revMix  > 0.004f;
    g_fxChorus.setRate(dxo.tremRate);
    g_fxReverb.setRoom(dxo.revSize, 0.4f);
    g_fxDelay.setTimeMs(dxo.dlyTimeMs, false);

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        // DX_ENGINE_TRIM lifts Dexed's float output into the same working
        // range every other engine in this codebase hands to the FX chain.
        // A single loud factory patch peaks around 0.5 there and an eight-note
        // chord around 2.1, so this trims for the chord and lets the saturator
        // below do what it does on every other engine rather than clipping.
        const float DX_ENGINE_TRIM = 1.6f;
        float mix = dxBuf[n] * DX_ENGINE_TRIM;

        // MASTER is pure linear gain after the saturator, not baked into it —
        // same discipline every engine in this codebase follows.
        const float DX_MIX_DRIVE = 1.0f;
        float out = fastTanh(mix * DX_MIX_DRIVE);
        float l = out, r = out;

        if (fxChorus) g_fxChorus.process(l, r, dxo.tremDepth, dxo.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, dxo.dlyFb, dxo.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * dxo.revMix; r += rr * dxo.revMix;
        }
        l *= dxo.master; r *= dxo.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }

    // Publish per-slot busy state for the allocator and the PERF meter.  Dexed
    // owns the truth about what is still sounding (release tails included), but
    // only as a count -- so a slot is "busy" while this codebase believes it
    // holds a pitch, and the count is what the CPU guard reads.
    int playing = d.getNumNotesPlaying();
    for (int i = 0; i < DX_VOICES; i++) va.busy[i] = (dxSlotNote[i] >= 0) ? 1 : 0;
    return playing;
}

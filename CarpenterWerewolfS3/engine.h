#pragma once
#include <Arduino.h>
#include "config.h"
#include "param_types.h"

// ============================================================================
//  engine.h — the multi-synth mode switch.
//
//  Exactly one engine is "live" at a time — selected from the front panel like
//  a real instrument's power switch, not a per-note timbre.  That keeps the
//  RAM budget to the size of the largest single engine rather than the sum of
//  all of them, and it matches how every one of these machines actually
//  existed: nobody plugged a CS-80 and a Minimoog into the same set of keys
//  and got one instrument out of it.
//
//  What DOES run two-at-once is the pair of sequencer/live-play tracks (see
//  keystyle.h): both tracks drive whichever engine is currently live, each
//  through its own note-assignment style and velocity curve, the way a
//  two-manual arrangement or a split keyboard would.
//
//  Every engine implements the same free-function surface below under its own
//  name prefix (cs80*, mm*, p5*, a26*, ...); this header just dispatches by
//  `currentEngine` with a switch, matching the rest of the codebase's style —
//  no vtables, no virtuals.
// ============================================================================

enum EngineId : uint8_t {
    ENGINE_CS80 = 0,
    ENGINE_MINIMOOG,
    ENGINE_PROPHET5,
    ENGINE_PROPHET10,
    ENGINE_ARP2600,
    ENGINE_ODYSSEY,
    ENGINE_MELLOTRON,
    ENGINE_OBSEM,         // Oberheim Four-Voice / SEM
    ENGINE_JUPITER8,      // Roland Jupiter-8
    ENGINE_SYSTEM100,     // Roland System 100/100m
    ENGINE_MOOGMODULAR,   // Moog Modular System 55/III — the wildcard: the
                          // actual synth behind both Halloween (1978) and
                          // Suspiria (1977), never itself modelled here
    ENGINE_OBX,           // Oberheim OB-X — Claudio Simonetti/Goblin
    ENGINE_PPGWAVE,       // PPG Wave 2.3 — Claudio Simonetti/Goblin
    ENGINE_MS20,          // Korg MS-20 — the vintage mono synth pick
    ENGINE_DX7,           // Yamaha DX7 — FM synthesis, the odd one out
    ENGINE_STRINGENS,     // ARP/Solina String Ensemble — organ-style divide-down
    ENGINE_PROPHETVS,     // Sequential Prophet-VS — vector synthesis
    NUM_ENGINES
};

extern const char* const ENGINE_NAMES[NUM_ENGINES];   // short, panel-width names
extern const char* const ENGINE_MAKER[NUM_ENGINES];   // one-line "as heard in" credit
extern const bool        ENGINE_IS_STUB[NUM_ENGINES]; // true = not modelled yet

// A preset/pattern lock index is only meaningful for the engine it was
// recorded against.  This is the widest param count across every engine, used
// to size the on-disk/UI arrays; each engine still uses its own exact count
// (<= this) for its own internal loops.
#define MAX_ENGINE_PARAMS 90
#define MAX_ENGINE_VOICES MAX_VOICES    // 10 — the Prophet-10 is the widest engine

// Every engine's parameter enum ends with this exact 8-entry tail, in this
// order, so the arpeggiator (which is engine-agnostic — it just schedules
// note events) can find ARP_ENABLE.._SYNC on ANY engine at
// `engineNumParams() - ARP_TAIL_COUNT` without engine-specific code.
#define ARP_TAIL_COUNT 8

extern EngineId currentEngine;

// Switches the live engine: silences the outgoing one, resets voice/queue
// state, calls the incoming engine's init + paramsInit.  Safe to call from
// core0 at any time; core1 only ever reads `currentEngine` once per block.
void engineSwitchTo(EngineId id);

// ── Parameters / presets — dispatched to whichever engine is current ────────
void        engineParamsInit();
void        engineSetParameter(uint8_t id, float v);
float       engineGetParameter(uint8_t id);
void        engineFormatParameter(uint8_t id, char* buf, size_t n);
bool        engineParamIsLockable(uint8_t id);
uint8_t     engineNumParams();
const ParamInfo* engineParamInfo(uint8_t id);
uint8_t     engineNumPresets();
const char* enginePresetName(uint8_t idx);
void        engineLoadPreset(uint8_t idx);
void        engineLoadInitPatch();
void        engineSnapshotParams(uint8_t* out);        // sized MAX_ENGINE_PARAMS
void        engineRestoreParams(const uint8_t* in);

// ── UI paging: contiguous [first, first+count) ranges of param ids ──────────
struct EnginePageDef { const char* name; uint8_t first; uint8_t count; };
uint8_t              engineNumPages();
const EnginePageDef* enginePage(uint8_t idx);

// ── Voice control (core1 / audio side) ───────────────────────────────────────
uint8_t engineNumVoices();
void    engineVoiceInit();                                          // (re)init all voices
void    engineNoteOn(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
// `note` is the MIDI note being released, or -1 when there isn't one (the
// CPU polyphony guard force-releasing a voice by index alone, for example).
// Most engines ignore it — CS-80/Minimoog/ARP/Mellotron voices release by
// index and don't care what pitch they were playing. Three engines need it,
// each for a different engine-level feature that can't be represented by
// voiceIdx alone:
//  - Prophet-5/10 UNISON fans one note across every internal voice (see
//    p5NoteOnE/p5NoteOffE), so releasing voiceIdx alone can't tell "the
//    note that started this chord let go" apart from "a different note,
//    still held, that also happens to map to this voiceIdx" — without the
//    actual note, an early release wrongly cuts a still-held later note.
//  - OB-X HOLD needs it to know which note of a memorized chord shape just
//    released (obxNoteOnE/obxNoteOffE).
//  - The String Ensemble's mono Cello/Contrabass bass coupler needs it to
//    pop the right note off its own held-note stack and re-target whichever
//    note is now lowest (seNoteOnE/seNoteOffE) — see stringens.h.
void    engineNoteOff(uint8_t voiceIdx, int note);
void    engineAllOff();
void    engineSetVoiceAftertouch(uint8_t voiceIdx, uint8_t pressure);

// Renders one full AUDIO_BLOCK of stereo 16-bit samples into `audioBuf`
// (interleaved L/R, AUDIO_BLOCK*2 int16s). Reads the shared performance
// globals (pitch bend as a ratio -1..+1 of the engine's own bend range, mod
// wheel 0..1, channel aftertouch 0..1). Always renders every currently-alive
// voice in the engine's own pool — the CPU guard enforces its ceiling
// separately, by not allocating past it (VoiceAlloc::limit) and by force-
// releasing (engineNoteOff) whichever voice sits at a newly-lowered ceiling,
// so a voice already fading out is never cut off mid-release. Returns the
// number of voices that rendered audible output this block, for the PERF
// meter and the guard's hysteresis.
int engineRenderBlock(int16_t* audioBuf, float pitchBend, float modWheel,
                      float chanAftertouch);

// Engine-relative helper for the arpeggiator (see ARP_TAIL_COUNT above).
static inline uint8_t engineArpBase() { return engineNumParams() - ARP_TAIL_COUNT; }

// Every engine's own numeric id for its master-volume parameter — there is
// no fixed offset shared across all of them (unlike the ARP tail), since the
// CS-80's own panel groups MASTER inside VOICING rather than right before
// its output section the way the newer engines do. Used by the one place
// that needs to nudge volume without caring which engine is live (the
// SETTINGS screen's MASTER VOL row).
uint8_t engineMasterParamId();

// Every engine's own numeric id for the two controls the front-panel CUTOFF/
// RESONANCE pots (GP26/GP27) drive live -- see Controls::cutoffPot/
// resonancePot and the pot handling in the sketch's loop(). Most engines name
// these params CUTOFF/RESONANCE outright (Minimoog calls its own "Emphasis",
// matching the real panel); the CS-80's own architecture has no single cutoff
// knob, so it maps to Brilliance/Resonance instead, exactly as the
// single-engine CS-80 build's own hardware pots already did, and the MS-20 --
// which has two independently resonant sections -- maps to its LOWPASS.
//
// Either can return ENGINE_PARAM_NONE (param_types.h) for an engine with no
// such control: the DX7 is the one case, since an FM engine has no filter at
// all. Callers MUST check for it before calling engineSetParameter(), because
// 0 is a real parameter id on every engine.
uint8_t engineFilterCutoffParamId();
uint8_t engineFilterResonanceParamId();

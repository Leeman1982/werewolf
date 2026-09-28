#pragma once
#include <Arduino.h>

// ============================================================================
//  param_types.h — the descriptor types shared by every synth engine.
//
//  Pulled out of cs80_params.h so new engines (Minimoog, Prophet-5, ARP 2600,
//  ...) can describe their own panels with the same ParamInfo/Preset shape
//  that the UI, MIDI CC map and preset system already know how to walk,
//  without each engine redeclaring identical structs.
// ============================================================================

enum ParamKind : uint8_t {
    PK_CONT = 0, PK_ENUM, PK_TOGGLE, PK_BIPOLAR, PK_HZ, PK_MS, PK_CENTS, PK_INT
};

struct ParamInfo {
    const char*  name;
    ParamKind    kind;
    const char* const* opts;
    uint8_t      numOpts;
    int16_t      dispMin, dispMax;
    uint8_t      cc;
};

struct PresetDelta { uint8_t param; uint8_t value; };
struct Preset      { const char* name; const PresetDelta* deltas; uint8_t n; };

// "This engine has no parameter of that role."  Returned by the
// engineFilterCutoffParamId()/engineFilterResonanceParamId() dispatch in
// engine.cpp for an engine with nothing a CUTOFF/RESONANCE pot can drive --
// the DX7, whose FM architecture has no filter at all.  It has to be a real
// sentinel rather than 0, because 0 is a valid parameter id on EVERY engine
// (DX_OP1_RATIO on the DX7), so a "no such control" answer of 0 would have
// the front-panel pots silently rewriting an operator's ratio.  Callers test
// for it before calling engineSetParameter() -- see the pot handling in
// CarpenterSynth.ino's loop().
#define ENGINE_PARAM_NONE ((uint8_t)0xFF)

// ─── Enum/toggle raw-byte helper for INIT patches and preset deltas ────────
// Every *Decode()/*FormatParameterE() in this codebase reads a PK_ENUM or
// PK_TOGGLE parameter as a PROPORTION of the 0..127 raw range, not as a
// literal option index: `i = (int)(raw/127.0f * (numOpts - 0.001f))`. A
// preset author writing the literal index (e.g. 2, meaning "the third
// option") instead of a properly scaled raw byte gets back index 0 almost
// every time, because 2/127 is a tiny fraction of any bucket wider than
// one or two steps. EI(idx, n) is the fix: it returns the raw byte at the
// CENTER of option `idx`'s bucket out of `n` options, so it round-trips
// through that exact decode formula regardless of n. Use it everywhere an
// INIT array or PresetDelta table sets a PK_ENUM/PK_TOGGLE parameter to
// anything other than option 0 (raw 0 already decodes correctly on its own).
#define EI(idx, n) ((uint8_t)((((idx) * 2 + 1) * 127) / (2 * (n))))

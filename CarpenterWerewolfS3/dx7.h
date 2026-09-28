#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"

// ============================================================================
//  dx7.h — Yamaha DX7, on the real Dexed/MSFA engine.
//
//  This engine is NOT hand-rolled.  It drives Dexed — the same six-operator
//  DX7 emulation that MiniDexed and Pico2Dexed run, itself built on Google's
//  music-synthesizer-for-android (MSFA) FM core — through the vendored
//  Synth_Dexed library in extras/Synth_Dexed/.  That is a deliberate reversal
//  of this codebase's usual "model it from the architecture" approach, and
//  the reason is specific to this instrument: every other engine here is an
//  ANALOGUE machine, where oscillator/filter/VCA behaviour can be modelled
//  from a documented topology and judged by ear against records.  The DX7 is
//  a DIGITAL machine whose sound IS its exact fixed-point arithmetic —
//  operator phase accumulation, the EGS chip's 4-stage rate/level envelope
//  curves, the log-domain sine and exp2 tables.  An approximation of those
//  numbers is not a slightly-less-accurate DX7, it is a different synth.
//
//  What that buys, versus the 6-op approximation this file used to hold:
//
//  1. All 32 real algorithms, not 8 representative shapes.
//  2. Real DX7 envelopes — 4 independent Rate/Level pairs per operator with
//     the real level-scaling table (Env::scaleoutlevel) and rate scaling,
//     not a 4-knob ADSR standing in for them.
//  3. Keyboard level scaling (breakpoint, left/right depth and curve),
//     keyboard rate scaling, velocity sensitivity and amp-mod sensitivity
//     per operator — the parameters that make a DX7 patch respond to WHERE
//     and HOW HARD it is played, none of which the approximation had.
//  4. The factory ROM library: 8 cartridges x 32 voices (dx7_rom.h), the
//     actual sounds — E.PIANO 1, TUB BELLS, BASS 1 — rather than
//     hand-written imitations of them.
//
//  What it costs: this engine's patch model is a ROM voice plus edits, not a
//  freely-designed patch saved whole.  A DX7 voice is 155 bytes and this
//  codebase's preset/step-lock system stores MAX_ENGINE_PARAMS (90) bytes
//  per patch, so a preset here records WHICH cartridge voice plus the
//  parameters listed in DxParamId below — not an arbitrary edited voice.
//  That is the honest trade and it is stated in the README too.
//
//  ── Beyond the 8 ROM banks ────────────────────────────────────────────────
//  The BANK row also reaches .syx cartridges dropped into /banks on the
//  filesystem (SETTINGS -> USB DRIVE exports it to a computer; see
//  voicebank.h) — up to MAX_FS_BANKS (config.h) more, 32 voices apiece.
//  This engine doesn't talk to the filesystem directly: dxExtBankCount/
//  dxExtBankName/dxExtGetVoice below are wired to voicebank.cpp's real
//  functions once, at boot, after storage mounts (CarpenterSynth.ino's
//  setup()). Left null, BANK simply can't reach past ROM — the same
//  behaviour this engine had before voicebank.cpp existed, not a build
//  break — which is what happens in the host tests (tests/run.sh links
//  neither voicebank.cpp, which needs the real FatFS, nor a sketch to wire
//  the pointers up) and on real hardware before storage.begin() succeeds.
//
//  Voice count is DX_VOICES (8), against the real machine's 16: Dexed's
//  per-note cost on RP2350 is the binding constraint (the MedusaDexed build
//  this port came from runs 12 Dexed notes total at 44.1 kHz across four
//  instances).  The CPU guard lowers it further under load like any other
//  engine.
//
//  ── Operator numbering ──────────────────────────────────────────────────
//  Dexed indexes operators 0..5 in DATA order, and DX7 voice data starts
//  with OP6.  So Dexed op 0 IS the panel's OP6.  Everything user-facing in
//  this engine uses PANEL numbering (OP1..OP6) and converts at the Dexed
//  boundary through dxOpToDexed() in dx7.cpp.  Getting this backwards
//  silently edits the wrong operator, which is why it is a named function
//  and not an inline "5 - i".
//
//  Licensing: Synth_Dexed is GPLv3 (Holger Wirtz' MicroDexed layer) over
//  Apache-2.0 (Google's MSFA core).  Linking it makes this firmware a GPLv3
//  work.  See extras/Synth_Dexed/LICENSE-GPL3.txt and the README's Licensing
//  section.
// ============================================================================

#define DX_VOICES  8         // Dexed note slots; real hardware is 16

// See "Beyond the 8 ROM banks" above. `extBank` in all three is 0-based over
// just the filesystem banks (voicebank.h's own numbering) -- dx7.cpp adds
// DX7_ROM_BANKS back on before it reaches the panel.
extern uint8_t (*dxExtBankCount)();
extern void    (*dxExtBankName)(uint8_t extBank, char* out, size_t outSize);
extern bool    (*dxExtGetVoice)(uint8_t extBank, uint8_t prog, uint8_t* out);   // out: DX7_PACKED_VOICE (128) bytes

// ─── Engine interface (see engine.h) ────────────────────────────────────────
void        dxEngineInit();
void        dxEngineActivate();
void        dxParamsInitE();
void        dxSetParameterE(uint8_t id, float v);
float       dxGetParameterE(uint8_t id);
void        dxFormatParameterE(uint8_t id, char* buf, size_t n);
bool        dxParamIsLockableE(uint8_t id);
uint8_t     dxNumParamsE();
const ParamInfo* dxParamInfoE(uint8_t id);
uint8_t     dxNumPresetsE();
const char* dxPresetNameE(uint8_t idx);
void        dxLoadPresetE(uint8_t idx);
void        dxLoadInitPatchE();
void        dxSnapshotParamsE(uint8_t* out);
void        dxRestoreParamsE(const uint8_t* in);
uint8_t     dxNumVoicesE();
void        dxVoiceInitE();
void        dxNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        dxNoteOffE(uint8_t voiceIdx);
void        dxAllOffE();
void        dxSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     dxMasterParamIdE();
uint8_t     dxFilterCutoffParamIdE();
uint8_t     dxFilterResonanceParamIdE();
int         dxRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     dxNumPagesE();
const struct EnginePageDef* dxPageE(uint8_t idx);

// Re-installs whatever (bank, program) is currently selected. Call this
// after voicebank.h's vbRescan() (i.e. once the USB drive is ejected): a
// bank past the ROM range can end up pointing at a different file once the
// /banks directory listing changes, and the currently-loaded voice needs to
// be re-resolved under the new numbering rather than silently going stale.
// A no-op if the selection is still ROM, or still resolves the same way;
// keeps the current sound if it no longer resolves at all (dxInstallVoice()'s
// usual policy).
void dxRefreshCurrentBank();

// ─── Test hooks ─────────────────────────────────────────────────────────────
// Exposed for tests/test.cpp only: the name of the voice currently loaded,
// and a direct (bank, program) load, so the regression test can walk all 256
// ROM voices without going through the 0..1 parameter encoding.
const char* dxCurrentVoiceName();
void        dxSelectRomVoice(uint8_t bank, uint8_t program);

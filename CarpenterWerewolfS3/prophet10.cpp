#include "prophet10.h"
#include "prophet5.h"
#include "param_types.h"
#include "engine.h"

// ============================================================================
//  prophet10.cpp — the Prophet-10 as what it actually was: two Prophet-5
//  voice banks under one hood, ten voices instead of five, otherwise the
//  identical circuit (same DCOs, same Poly-Mod, same filter). Sequential
//  built it by doubling the Prophet-5's voice boards, not by redesigning the
//  voice — so this engine doubles prophet5.cpp's voice count the same way,
//  rather than re-implementing the DSP.
//
//  Everything below is a thin forward to the Prophet-5 adapter with
//  polyphony pinned to 10 instead of 5. Panel, presets, Poly-Mod, Unison
//  (which now stacks all ten) are all shared verbatim.
// ============================================================================

void        p10EngineInit()                          { p5EngineInit(); }
void        p10EngineActivate()                      { p5SetPolyphonyE(10); p5EngineActivate(); }
void        p10ParamsInitE()                         { p5ParamsInitE(); }
void        p10SetParameterE(uint8_t id, float v)     { p5SetParameterE(id, v); }
float       p10GetParameterE(uint8_t id)              { return p5GetParameterE(id); }
void        p10FormatParameterE(uint8_t id, char* b, size_t n) { p5FormatParameterE(id, b, n); }
bool        p10ParamIsLockableE(uint8_t id)            { return p5ParamIsLockableE(id); }
uint8_t     p10NumParamsE()                           { return p5NumParamsE(); }
const ParamInfo* p10ParamInfoE(uint8_t id)             { return p5ParamInfoE(id); }
uint8_t     p10NumPresetsE()                          { return p5NumPresetsE(); }
const char* p10PresetNameE(uint8_t idx)                { return p5PresetNameE(idx); }
void        p10LoadPresetE(uint8_t idx)                { p5LoadPresetE(idx); }
void        p10LoadInitPatchE()                       { p5LoadInitPatchE(); }
void        p10SnapshotParamsE(uint8_t* out)           { p5SnapshotParamsE(out); }
void        p10RestoreParamsE(const uint8_t* in)       { p5RestoreParamsE(in); }
uint8_t     p10NumVoicesE()                           { return p5NumVoicesE(); }
void        p10VoiceInitE()                           { p5SetPolyphonyE(10); p5VoiceInitE(); }
void        p10NoteOnE(uint8_t v, int note, uint8_t vel, bool trig) { p5NoteOnE(v, note, vel, trig); }
void        p10NoteOffE(uint8_t v, int note)           { p5NoteOffE(v, note); }
void        p10AllOffE()                              { p5AllOffE(); }
void        p10SetVoiceAftertouchE(uint8_t v, uint8_t pressure) { p5SetVoiceAftertouchE(v, pressure); }
int         p10RenderBlockE(int16_t* audioBuf, float bend, float mw, float at) {
    return p5RenderBlockE(audioBuf, bend, mw, at);
}
uint8_t     p10MasterParamIdE()                       { return p5MasterParamIdE(); }
uint8_t     p10FilterCutoffParamIdE()                  { return p5FilterCutoffParamIdE(); }
uint8_t     p10FilterResonanceParamIdE()               { return p5FilterResonanceParamIdE(); }
uint8_t     p10NumPagesE()                            { return p5NumPagesE(); }
const EnginePageDef* p10PageE(uint8_t idx)             { return p5PageE(idx); }

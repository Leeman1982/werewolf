#include "odyssey.h"
#include "arp2600.h"
#include "param_types.h"
#include "engine.h"

// ============================================================================
//  odyssey.cpp — the ARP Odyssey as a duophonic ARP 2600: same VCOs, same
//  state-variable filter, same S&H and ring mod, but the Odyssey's panel
//  really did add a switchable Duo/Mono mode the 2600 never had, so this
//  engine runs arp2600.cpp's voice code with polyphony pinned to 2 instead
//  of 1. Everything else — panel, presets, filter character — is shared
//  verbatim; the 2600 and Odyssey used closely related VCF designs, and
//  building two separate filter models here would model a difference this
//  codebase has no measurements to back up.
// ============================================================================

void        odyEngineInit()                          { a26EngineInit(); }
void        odyEngineActivate()                      { a26SetPolyphonyE(2); a26EngineActivate(); }
void        odyParamsInitE()                         { a26ParamsInitE(); }
void        odySetParameterE(uint8_t id, float v)     { a26SetParameterE(id, v); }
float       odyGetParameterE(uint8_t id)              { return a26GetParameterE(id); }
void        odyFormatParameterE(uint8_t id, char* b, size_t n) { a26FormatParameterE(id, b, n); }
bool        odyParamIsLockableE(uint8_t id)            { return a26ParamIsLockableE(id); }
uint8_t     odyNumParamsE()                           { return a26NumParamsE(); }
const ParamInfo* odyParamInfoE(uint8_t id)             { return a26ParamInfoE(id); }
uint8_t     odyNumPresetsE()                          { return a26NumPresetsE(); }
const char* odyPresetNameE(uint8_t idx)                { return a26PresetNameE(idx); }
void        odyLoadPresetE(uint8_t idx)                { a26LoadPresetE(idx); }
void        odyLoadInitPatchE()                       { a26LoadInitPatchE(); }
void        odySnapshotParamsE(uint8_t* out)           { a26SnapshotParamsE(out); }
void        odyRestoreParamsE(const uint8_t* in)       { a26RestoreParamsE(in); }
uint8_t     odyNumVoicesE()                           { return a26NumVoicesE(); }
void        odyVoiceInitE()                           { a26SetPolyphonyE(2); a26VoiceInitE(); }
void        odyNoteOnE(uint8_t v, int note, uint8_t vel, bool trig) { a26NoteOnE(v, note, vel, trig); }
void        odyNoteOffE(uint8_t v)                     { a26NoteOffE(v); }
void        odyAllOffE()                              { a26AllOffE(); }
void        odySetVoiceAftertouchE(uint8_t v, uint8_t pressure) { a26SetVoiceAftertouchE(v, pressure); }
int         odyRenderBlockE(int16_t* audioBuf, float bend, float mw, float at) {
    return a26RenderBlockE(audioBuf, bend, mw, at);
}
uint8_t     odyMasterParamIdE()                       { return a26MasterParamIdE(); }
uint8_t     odyFilterCutoffParamIdE()                  { return a26FilterCutoffParamIdE(); }
uint8_t     odyFilterResonanceParamIdE()               { return a26FilterResonanceParamIdE(); }
uint8_t     odyNumPagesE()                            { return a26NumPagesE(); }
const EnginePageDef* odyPageE(uint8_t idx)             { return a26PageE(idx); }

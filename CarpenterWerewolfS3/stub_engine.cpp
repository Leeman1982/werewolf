#include "stub_engine.h"
#include "config.h"
#include "engine.h"
#include <string.h>

// ============================================================================
//  stub_engine.cpp — does nothing, safely. See stub_engine.h.
// ============================================================================

void        stubEngineInit() {}
void        stubEngineActivate() {}
void        stubParamsInitE() {}
void        stubSetParameterE(uint8_t, float) {}
float       stubGetParameterE(uint8_t) { return 0.0f; }
void        stubFormatParameterE(uint8_t, char* buf, size_t n) { if (n) buf[0] = 0; }
bool        stubParamIsLockableE(uint8_t) { return false; }
uint8_t     stubNumParamsE() { return 0; }
// A step lock recorded under a wider engine and later displayed while a
// stub engine is live is exactly the scenario this guards against — callers
// like the PLOCK_EDIT screen dereference this unconditionally, so nullptr
// would crash rather than just show "--".
static const ParamInfo STUB_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* stubParamInfoE(uint8_t) { return &STUB_INVALID_PARAM; }
uint8_t     stubNumPresetsE() { return 0; }
const char* stubPresetNameE(uint8_t) { return ""; }
void        stubLoadPresetE(uint8_t) {}
void        stubLoadInitPatchE() {}
void        stubSnapshotParamsE(uint8_t*) {}
void        stubRestoreParamsE(const uint8_t*) {}
uint8_t     stubNumVoicesE() { return 0; }
void        stubVoiceInitE() {}
void        stubNoteOnE(uint8_t, int, uint8_t, bool) {}
void        stubNoteOffE(uint8_t) {}
void        stubAllOffE() {}
void        stubSetVoiceAftertouchE(uint8_t, uint8_t) {}

int stubRenderBlockE(int16_t* audioBuf, float, float, float) {
    memset(audioBuf, 0, sizeof(int16_t) * AUDIO_BLOCK * 2);
    return 0;
}

uint8_t stubMasterParamIdE() { return 0; }
// ENGINE_PARAM_NONE, not 0: this is what the engineFilterCutoffParamId()
// dispatch falls back to, and 0 is a real, writable parameter on every
// engine -- so an engine id that ever went missing from that switch would
// have the front-panel pots quietly rewriting its first parameter instead of
// doing nothing.  See param_types.h.
uint8_t stubFilterCutoffParamIdE()    { return ENGINE_PARAM_NONE; }
uint8_t stubFilterResonanceParamIdE() { return ENGINE_PARAM_NONE; }
uint8_t stubNumPagesE() { return 0; }
// Defensive, matching stubParamInfoE above: engineNumPages()==0 means the UI
// should never actually index this, but a null return dereferenced by a
// caller that forgot to check would crash where a placeholder just no-ops.
static const EnginePageDef STUB_INVALID_PAGE = { "--", 0, 0 };
const EnginePageDef* stubPageE(uint8_t) { return &STUB_INVALID_PAGE; }

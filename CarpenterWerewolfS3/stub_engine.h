#pragma once
#include <Arduino.h>
#include "param_types.h"

// ============================================================================
//  stub_engine.h — the "not modelled yet" engine.
//
//  ENGINE_OBSEM (Oberheim Four-Voice/SEM), ENGINE_JUPITER8 and
//  ENGINE_SYSTEM100 all point at this one adapter. Selecting one of them
//  in the mode menu is safe — it renders silence, accepts no parameters,
//  and the UI shows "COMING SOON" rather than an empty or broken panel —
//  but there is no synthesis behind it. See engine.h's ENGINE_IS_STUB table.
// ============================================================================

void        stubEngineInit();
void        stubEngineActivate();
void        stubParamsInitE();
void        stubSetParameterE(uint8_t id, float v);
float       stubGetParameterE(uint8_t id);
void        stubFormatParameterE(uint8_t id, char* buf, size_t n);
bool        stubParamIsLockableE(uint8_t id);
uint8_t     stubNumParamsE();
const ParamInfo* stubParamInfoE(uint8_t id);
uint8_t     stubNumPresetsE();
const char* stubPresetNameE(uint8_t idx);
void        stubLoadPresetE(uint8_t idx);
void        stubLoadInitPatchE();
void        stubSnapshotParamsE(uint8_t* out);
void        stubRestoreParamsE(const uint8_t* in);
uint8_t     stubNumVoicesE();
void        stubVoiceInitE();
void        stubNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        stubNoteOffE(uint8_t voiceIdx);
void        stubAllOffE();
void        stubSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     stubMasterParamIdE();
uint8_t     stubFilterCutoffParamIdE();
uint8_t     stubFilterResonanceParamIdE();
int         stubRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     stubNumPagesE();
const struct EnginePageDef* stubPageE(uint8_t idx);

#pragma once
#include <Arduino.h>
#include "param_types.h"

// See odyssey.cpp — a thin polyphony-2 forward onto arp2600.cpp's engine.
void        odyEngineInit();
void        odyEngineActivate();
void        odyParamsInitE();
void        odySetParameterE(uint8_t id, float v);
float       odyGetParameterE(uint8_t id);
void        odyFormatParameterE(uint8_t id, char* buf, size_t n);
bool        odyParamIsLockableE(uint8_t id);
uint8_t     odyNumParamsE();
const ParamInfo* odyParamInfoE(uint8_t id);
uint8_t     odyNumPresetsE();
const char* odyPresetNameE(uint8_t idx);
void        odyLoadPresetE(uint8_t idx);
void        odyLoadInitPatchE();
void        odySnapshotParamsE(uint8_t* out);
void        odyRestoreParamsE(const uint8_t* in);
uint8_t     odyNumVoicesE();
void        odyVoiceInitE();
void        odyNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        odyNoteOffE(uint8_t voiceIdx);
void        odyAllOffE();
void        odySetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     odyMasterParamIdE();
uint8_t     odyFilterCutoffParamIdE();
uint8_t     odyFilterResonanceParamIdE();
int         odyRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     odyNumPagesE();
const struct EnginePageDef* odyPageE(uint8_t idx);

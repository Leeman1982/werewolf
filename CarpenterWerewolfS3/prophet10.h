#pragma once
#include <Arduino.h>
#include "param_types.h"

// See prophet10.cpp — a thin polyphony-10 forward onto prophet5.cpp's engine.
void        p10EngineInit();
void        p10EngineActivate();
void        p10ParamsInitE();
void        p10SetParameterE(uint8_t id, float v);
float       p10GetParameterE(uint8_t id);
void        p10FormatParameterE(uint8_t id, char* buf, size_t n);
bool        p10ParamIsLockableE(uint8_t id);
uint8_t     p10NumParamsE();
const ParamInfo* p10ParamInfoE(uint8_t id);
uint8_t     p10NumPresetsE();
const char* p10PresetNameE(uint8_t idx);
void        p10LoadPresetE(uint8_t idx);
void        p10LoadInitPatchE();
void        p10SnapshotParamsE(uint8_t* out);
void        p10RestoreParamsE(const uint8_t* in);
uint8_t     p10NumVoicesE();
void        p10VoiceInitE();
void        p10NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        p10NoteOffE(uint8_t voiceIdx, int note);
void        p10AllOffE();
void        p10SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     p10MasterParamIdE();
uint8_t     p10FilterCutoffParamIdE();
uint8_t     p10FilterResonanceParamIdE();
int         p10RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     p10NumPagesE();
const struct EnginePageDef* p10PageE(uint8_t idx);

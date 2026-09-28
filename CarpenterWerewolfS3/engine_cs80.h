#pragma once
#include <Arduino.h>
#include "config.h"
#include "param_types.h"

// ============================================================================
//  engine_cs80.h — the multi-engine adapter around the existing, unmodified
//  CS-80 model (cs80_params.*, cs80_voice.h).
//
//  This is the ONLY new file touching CS-80 behaviour, and it changes none of
//  it: applyNoteOn/channelControl/renderChannel/renderBlock are moved here
//  byte-for-byte from the sketch (previously the only place they could live,
//  since core1's setup1()/loop1() were sketch-level entry points), just
//  renamed to the generic per-engine surface every engine implements.
// ============================================================================

void        cs80EngineInit();                 // voices[], noiseGen, finalLp, FX — once at boot
void        cs80EngineActivate();             // called every time this engine becomes current

void        cs80ParamsInitE();
void        cs80SetParameterE(uint8_t id, float v);
float       cs80GetParameterE(uint8_t id);
void        cs80FormatParameterE(uint8_t id, char* buf, size_t n);
bool        cs80ParamIsLockableE(uint8_t id);
uint8_t     cs80NumParamsE();
const ParamInfo* cs80ParamInfoE(uint8_t id);
uint8_t     cs80NumPresetsE();
const char* cs80PresetNameE(uint8_t idx);
void        cs80LoadPresetE(uint8_t idx);
void        cs80LoadInitPatchE();
void        cs80SnapshotParamsE(uint8_t* out);
void        cs80RestoreParamsE(const uint8_t* in);

uint8_t     cs80NumVoicesE();
void        cs80VoiceInitE();
void        cs80NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        cs80NoteOffE(uint8_t voiceIdx);
void        cs80AllOffE();
void        cs80SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);

uint8_t     cs80MasterParamIdE();
uint8_t     cs80FilterCutoffParamIdE();
uint8_t     cs80FilterResonanceParamIdE();
uint8_t     cs80NumPagesE();
const struct EnginePageDef* cs80PageE(uint8_t idx);
int         cs80RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel,
                             float chanAftertouch);

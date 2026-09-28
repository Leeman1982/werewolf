#pragma once
#include <Arduino.h>
#include "config.h"
#include "dsp_tables.h"
#include "param_types.h"
#include "variable_filter.h"
#include "mellotron_samples.h"

// ============================================================================
//  mellotron.h — tape-loop playback, the way a Mellotron actually made sound:
//  no oscillator, no filter envelope, just a physical loop of tape running
//  at whatever speed the key's pitch calls for, started and stopped by a
//  solenoid-and-pinch-roller mechanism per key.
//
//  One liberty taken past strict tape-machine authenticity: a CUTOFF/
//  RESONANCE/FILT TYPE section (variable_filter.h's shared 16-flavour bank,
//  same one Minimoog/Prophet-5/10/ARP 2600/Odyssey expose) sits after the
//  tape heads, giving this engine the same live sound-shaping depth as
//  every subtractive engine here instead of just TONE's fixed one-pole. A
//  real M400 never had this; FILT TYPE's STOCK option (index 0, the
//  default) is wired to mean "no filter at all" rather than some real
//  Mellotron circuit, so an unfiltered loop plays exactly as it always did
//  and every existing preset is untouched by this addition.
//
//  Neither loop is sampled from a real M400 — there is no physical Mellotron
//  tape to sample from here. The Choir loop IS a real recording (a
//  user-supplied vocal take, trimmed/loop-crossfaded/downsampled — see
//  tools/gen_mellotron_samples.py); Strings is still a synthesised
//  approximation, baked to flash the same way. See mellotron_samples.h.
//  What IS modelled authentically regardless of a loop's own origin:
//
//  1. Playback is pitch-shifted by resampling a fixed recording, exactly as
//     a real tape head reads the same physical groove faster or slower —
//     not by re-synthesising the waveform at the target pitch.
//  2. Attack/Release are the tape motor spinning up to speed and braking
//     back down on key-up, not a VCA envelope shaping a sustained tone —
//     there is no sustain STAGE because the tape simply keeps running at
//     full speed for as long as the key (and the loop) allow.
//  3. Wow & flutter: an extra, independent slow pitch wobble on TOP of
//     whatever modulation is baked into the loop content, modelling the
//     mechanical speed instability of the pinch roller — real tape wow, not
//     a synth LFO routed to pitch.
// ============================================================================

#define MEL_VOICES 6

enum MelSound : uint8_t { MEL_S_CHOIR = 0, MEL_S_STRINGS, MEL_S_BOTH };

struct MelPlayer {
    float    posSamples = 0.0f;     // read position into the loop, in source samples
    float    incPerSample = 1.0f;   // source-samples advanced per system sample

    inline float read(const int8_t* data, uint32_t len) const {
        uint32_t i0 = (uint32_t)posSamples % len;
        uint32_t i1 = (i0 + 1) % len;
        float frac = posSamples - floorf(posSamples);
        float a = (float)data[i0] * (1.0f / 128.0f);
        float b = (float)data[i1] * (1.0f / 128.0f);
        return a + (b - a) * frac;
    }
    inline void advance(uint32_t len) {
        posSamples += incPerSample;
        if (posSamples >= (float)len) posSamples -= (float)len;
    }
};

enum MelEnvStage : uint8_t { MEL_E_IDLE = 0, MEL_E_ATTACK, MEL_E_HOLD, MEL_E_RELEASE };
struct MelTapeEnv {
    uint8_t stage = MEL_E_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, relRate = 0.02f;
    inline void trigger(float atk, float rel) { stage = MEL_E_ATTACK; atkRate = atk; relRate = rel; }
    inline void release() { if (stage != MEL_E_IDLE) stage = MEL_E_RELEASE; }
    inline float process() {
        switch (stage) {
        case MEL_E_IDLE: return 0.0f;
        case MEL_E_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = MEL_E_HOLD; }
            break;
        case MEL_E_HOLD: level = 1.0f; break;
        case MEL_E_RELEASE:
            level -= relRate;
            if (level <= 0.0f) { level = 0.0f; stage = MEL_E_IDLE; }
            break;
        }
        return level;
    }
};

struct MelVoice {
    MelPlayer choir, strings;   // both tape heads always advance; SOUND param picks the mix
    MelTapeEnv env;             // one shared tape motor envelope for both heads
    VarFilter varFilt;          // post-tape-head filter; bypassed when FILT TYPE == STOCK

    int      note = -1;
    uint8_t  velocity = 0;
    bool     gate = false;
    float    aftertouch = 0.0f;
};

// ─── Generic engine adapter surface (see engine.h) ─────────────────────────
void        melEngineInit();
void        melEngineActivate();
void        melParamsInitE();
void        melSetParameterE(uint8_t id, float v);
float       melGetParameterE(uint8_t id);
void        melFormatParameterE(uint8_t id, char* buf, size_t n);
bool        melParamIsLockableE(uint8_t id);
uint8_t     melNumParamsE();
const ParamInfo* melParamInfoE(uint8_t id);
uint8_t     melNumPresetsE();
const char* melPresetNameE(uint8_t idx);
void        melLoadPresetE(uint8_t idx);
void        melLoadInitPatchE();
void        melSnapshotParamsE(uint8_t* out);
void        melRestoreParamsE(const uint8_t* in);
uint8_t     melNumVoicesE();
void        melVoiceInitE();
void        melNoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv);
void        melNoteOffE(uint8_t voiceIdx);
void        melAllOffE();
void        melSetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure);
uint8_t     melMasterParamIdE();
uint8_t     melFilterCutoffParamIdE();
uint8_t     melFilterResonanceParamIdE();
int         melRenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel, float chanAftertouch);
uint8_t     melNumPagesE();
const struct EnginePageDef* melPageE(uint8_t idx);

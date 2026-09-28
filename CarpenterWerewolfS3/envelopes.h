#pragma once
#include <Arduino.h>

// ============================================================================
//  envelopes.h — a plain 4-stage ADSR, shared by engines whose real
//  instrument had one (Prophet-5's contour generators are genuine ADSR).
//
//  Deliberately NOT used by the Minimoog engine — the Model D's contours are
//  Attack/Decay/Sustain only, with no Release control at all, and that
//  omission is authentic, not a simplification, so it gets its own type
//  (MMEnv in minimoog.h) rather than being forced through this one with
//  Release wired to Decay.
// ============================================================================

enum ADSRStage : uint8_t { ADSR_IDLE = 0, ADSR_ATTACK, ADSR_DECAY, ADSR_SUSTAIN, ADSR_RELEASE };

struct ADSREnv {
    uint8_t stage = ADSR_IDLE;
    float   level = 0.0f;
    float   atkRate = 0.02f, decCoef = 0.01f, sus = 0.7f, relCoef = 0.01f;

    inline void trigger(float atk, float dec, float s, float rel) {
        stage = ADSR_ATTACK; atkRate = atk; decCoef = dec; sus = s; relCoef = rel;
    }
    inline void release() { if (stage != ADSR_IDLE) stage = ADSR_RELEASE; }
    inline float process() {
        switch (stage) {
        case ADSR_IDLE: return 0.0f;
        case ADSR_ATTACK:
            level += atkRate;
            if (level >= 1.0f) { level = 1.0f; stage = ADSR_DECAY; }
            break;
        case ADSR_DECAY:
            level += (sus - level) * decCoef;
            if (fabsf(level - sus) < 0.001f) { level = sus; stage = ADSR_SUSTAIN; }
            break;
        case ADSR_SUSTAIN:
            level = sus;
            break;
        case ADSR_RELEASE:
            level -= level * relCoef;
            if (level < 0.0005f) { level = 0.0f; stage = ADSR_IDLE; }
            break;
        }
        return level;
    }
};

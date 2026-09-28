#include "cs80_voice.h"

// Per-voice analogue offsets.  Fixed rather than random so a patch sounds
// identical every power-up — a real CS-80 drifts, but it does not become a
// different instrument each time you switch it on.  Values are deliberately
// small and deliberately not symmetrical.
const float CS_VOICE_DETUNE[CS_VOICES] = {   // cents
    0.0f, -2.3f, 1.7f, -1.1f, 2.9f, -3.4f, 0.8f, -1.9f
};
const float CS_VOICE_FILT[CS_VOICES] = {     // fraction of a semitone-ish on cutoff
    0.0f, 0.018f, -0.012f, 0.026f, -0.021f, 0.009f, -0.028f, 0.015f
};
const float CS_VOICE_LEVEL[CS_VOICES] = {    // linear gain trim
    1.000f, 0.988f, 1.012f, 0.994f, 1.007f, 0.981f, 1.016f, 0.997f
};

void cs80VoiceInit(Cs80Voice* v, int index) {
    for (int c = 0; c < CS_CHANNELS; c++) {
        Cs80Channel& ch = v->ch[c];
        cs80OscInit(&ch.osc);
        ch.pwmPhase = (uint32_t)index * 0x21000000u + (uint32_t)c * 0x40000000u;
        ch.pwmInc = 0;
        cs80SvfInit(&ch.hpf);
        cs80SvfInit(&ch.lpf);
        ch.fixedLp.z = 0.0f;
        ch.ampEnv.stage = CA_IDLE;  ch.ampEnv.level = 0.0f;
        ch.filtEnv.stage = CF_IDLE; ch.filtEnv.value = 0.0f;
    }
    v->note = -1;
    v->velocity = 100;
    v->velNorm = 0.8f;
    v->aftertouch = 0.0f;
    v->gate = false;
    v->startTick = 0;
    v->targetFreq = v->currentFreq = 440.0f;
    v->glideCoef = 1.0f;
    v->gliding = false;
    v->mixScale = 1.0f;

    int i = index % CS_VOICES;
    v->driftCents = CS_VOICE_DETUNE[i];
    v->driftFilt  = CS_VOICE_FILT[i];
    v->driftLevel = CS_VOICE_LEVEL[i];
    v->driftPhase = (float)i * 0.7853f;
}

#include "engine_cs80.h"
#include "engine.h"
#include "dsp_tables.h"
#include "cs80_params.h"
#include "cs80_voice.h"
#include "fx.h"
#include "voicealloc.h"

// Owned by the sketch, shared by every engine: the allocator core0 uses to
// pick which voice index a new note goes to.  Rendering publishes which
// voices are genuinely still busy (envelope not IDLE) back into it, which is
// what lets steal() prefer a truly silent voice over one merely gated off but
// still finishing its release tail.
extern VoiceAlloc va;

// ============================================================================
//  engine_cs80.cpp — CS-80 engine, moved verbatim out of the sketch.
//
//  Nothing about the DSP changed in this move.  What DID change is where the
//  state lives: on the RP2350 build this was all sketch-global because
//  setup1()/loop1() (arduino-pico's automatic second core) had to see it
//  directly.  Now that audio rendering is a portable function called from a
//  FreeRTOS task (see the .ino), the state can live wherever the engine that
//  owns it lives — so it moved here, file-static, with no cross-core sharing
//  except the one byte-per-voice aftertouch array, which genuinely is written
//  from core0 (MIDI/UI) and read from the audio task.
// ============================================================================

static Cs80Voice voices[CS_VOICES];
static NoiseGen  noiseGen;

static uint32_t lfoPhase = 0;
static float    lfoShValue = 0.0f;

static uint32_t ringPhase = 0;
static float    ringEnv = 0.0f;
static uint8_t  ringEnvStage = 0;
static bool     ringTrigger = false;

static Cs80Svf     finalLp;
// Chorus/delay/reverb are the one shared g_fxChorus/g_fxDelay/g_fxReverb
// trio (see fx.h) — not private to this engine.

// Written by core0 (poly aftertouch), read by the audio task — the one piece
// of state here that genuinely crosses the core boundary.
static volatile uint8_t g_voiceAT[CS_VOICES];

void cs80EngineInit() {
    for (int i = 0; i < CS_VOICES; i++) { cs80VoiceInit(&voices[i], i); g_voiceAT[i] = 0; }
    noiseGen.seed = 0x1234567u; noiseGen.p0 = noiseGen.p1 = noiseGen.p2 = 0.0f;
    cs80SvfInit(&finalLp);
    g_fxChorus.begin();
    g_fxDelay.begin();
    g_fxReverb.begin();
}

void cs80EngineActivate() {
    for (int i = 0; i < CS_VOICES; i++) { cs80VoiceInit(&voices[i], i); g_voiceAT[i] = 0; }
    lfoPhase = 0; lfoShValue = 0.0f;
    ringPhase = 0; ringEnv = 0.0f; ringEnvStage = 0; ringTrigger = false;
    cs80SvfInit(&finalLp);
    // g_fxChorus/g_fxDelay/g_fxReverb are shared with every other engine now
    // (see fx.h) — reset them on every activation, same as the other four
    // engines already did, so switching into CS-80 never inherits an
    // uncleared reverb/delay tail left over from whatever was playing
    // before it.
    g_fxChorus.begin();
    g_fxDelay.begin();
    g_fxReverb.begin();
}

// ─── Parameters — thin pass-through to the untouched cs80_params API ───────
void        cs80ParamsInitE()                          { paramsInit(); }
void        cs80SetParameterE(uint8_t id, float v)      { setParameter(id, v); }
float       cs80GetParameterE(uint8_t id)               { return getParameter(id); }
void        cs80FormatParameterE(uint8_t id, char* b, size_t n) { formatParameter(id, b, n); }
bool        cs80ParamIsLockableE(uint8_t id)             { return paramIsLockable(id); }
uint8_t     cs80NumParamsE()                            { return NUM_PARAMETERS; }
// A step's parameter lock stores a raw id that can outlive an engine switch
// (the pattern was recorded under a different, possibly wider, engine) —
// so this bounds-checks rather than trusting the caller.
static const ParamInfo CS80_INVALID_PARAM = { "--", PK_CONT, nullptr, 0, 0, 1, 0 };
const ParamInfo* cs80ParamInfoE(uint8_t id) {
    return (id < NUM_PARAMETERS) ? &PARAM_INFO[id] : &CS80_INVALID_PARAM;
}
uint8_t     cs80NumPresetsE()                           { return (uint8_t)NUM_PRESETS; }
const char* cs80PresetNameE(uint8_t idx)                 { return PRESETS[idx].name; }
void        cs80LoadPresetE(uint8_t idx)                 { loadPreset(idx); }
void        cs80LoadInitPatchE()                        { loadInitPatch(); }
void        cs80SnapshotParamsE(uint8_t* out)            { snapshotParams(out); }
void        cs80RestoreParamsE(const uint8_t* in)        { restoreParams(in); }

uint8_t cs80MasterParamIdE() { return P_MASTER; }
// Brilliance is the closest single-knob equivalent to "cutoff" here — the
// CS-80 has independent per-channel HP/LP filters, and Brilliance is the
// global offset that opens both at once, same as Resonance already does.
uint8_t cs80FilterCutoffParamIdE()    { return P_BRILLIANCE; }
uint8_t cs80FilterResonanceParamIdE() { return P_RESONANCE; }

// The CS-80's page table, moved here verbatim from what used to be ui.cpp's
// hardcoded SYNTH_PAGE[] — now exposed the same way every other engine
// exposes its pages, so ui.cpp doesn't need a CS-80 special case at all.
static const EnginePageDef CS80_PAGES[] = {
    { "CHANNEL I",  C1_FEET,      CS_CH_PARAMS },
    { "CHANNEL II", C2_FEET,      CS_CH_PARAMS },
    { "VOICING",    P_MIX,        10 },
    { "SUB OSC",    P_LFO_WAVE,    7 },
    { "RING MOD",   P_RING_SPEED,  5 },
    { "OUTPUT",     P_TREM_RATE,   8 },
    { "ARP",        P_ARP_ENABLE,  8 },
};
#define CS80_NUM_PAGES (sizeof(CS80_PAGES) / sizeof(CS80_PAGES[0]))
uint8_t cs80NumPagesE() { return (uint8_t)CS80_NUM_PAGES; }
const EnginePageDef* cs80PageE(uint8_t idx) { return (idx < CS80_NUM_PAGES) ? &CS80_PAGES[idx] : &CS80_PAGES[0]; }

uint8_t cs80NumVoicesE() { return CS_VOICES; }
void    cs80VoiceInitE() { cs80EngineActivate(); }

void cs80NoteOnE(uint8_t voiceIdx, int note, uint8_t vel, bool trigEnv) {
    if (voiceIdx >= CS_VOICES) return;
    Cs80Voice* v = &voices[voiceIdx];
    float freq = noteToFreq(note);

    v->note     = note;
    v->velocity = vel;
    v->velNorm  = (float)vel * (1.0f / 127.0f);
    v->gate     = true;
    v->targetFreq = freq;

    // Initial pitch bend: a velocity-sensitive dip INTO the note.  Harder
    // playing starts further below and slides up — one of the gestures that
    // makes a CS-80 sound played rather than triggered.
    float startFreq = freq;
    bool  needGlide = false;
    if (cs.initBend > 0.01f) {
        startFreq = freq * fastPow2(-(cs.initBend * v->velNorm) / 12.0f);
        needGlide = true;
    }

    if (cs.portaMode != PORTA_OFF && cs.portaTime > 0.005f && v->currentFreq > 20.0f) {
        v->gliding = true;
        v->glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (cs.portaTime * SAMPLE_RATE_F);
    } else if (needGlide) {
        v->currentFreq = startFreq;
        v->gliding = true;
        v->glideCoef = 6.9f * (float)CONTROL_RATE_DIV / (0.045f * SAMPLE_RATE_F);
    } else {
        v->currentFreq = freq;
        v->gliding = false;
    }
    if (v->glideCoef > 1.0f) v->glideCoef = 1.0f;

    if (trigEnv) {
        for (int c = 0; c < CS_CHANNELS; c++) {
            const Cs80ChanParams& p = cs.ch[c];
            Cs80Channel& ch = v->ch[c];
            cs80AmpTrigger(&ch.ampEnv, p.ampAtk, p.ampDec, p.ampSus, p.ampRel);
            cs80FiltTrigger(&ch.filtEnv, p.vcfIL, p.vcfAL,
                            fabsf(p.vcfAL - p.vcfIL) * p.vcfAtkStep, p.vcfDec, p.vcfRel);
            if (ch.ampEnv.level < 0.01f) ch.osc.phase = 0;
        }
        ringTrigger = true;      // the ring-mod AD rides the common key-on
    }
}

void cs80NoteOffE(uint8_t voiceIdx) {
    if (voiceIdx >= CS_VOICES) return;
    Cs80Voice* v = &voices[voiceIdx];
    for (int c = 0; c < CS_CHANNELS; c++) {
        cs80AmpRelease(&v->ch[c].ampEnv);
        cs80FiltRelease(&v->ch[c].filtEnv);
    }
    v->gate = false;
}

void cs80AllOffE() {
    for (int i = 0; i < CS_VOICES; i++) {
        for (int c = 0; c < CS_CHANNELS; c++) {
            cs80AmpRelease(&voices[i].ch[c].ampEnv);
            cs80FiltRelease(&voices[i].ch[c].filtEnv);
        }
        voices[i].gate = false;
    }
}

void cs80SetVoiceAftertouchE(uint8_t voiceIdx, uint8_t pressure) {
    if (voiceIdx < CS_VOICES) g_voiceAT[voiceIdx] = pressure;
}

// Control-rate update for one channel.
static inline void channelControl(Cs80Voice* v, int ci, float baseFreq,
                                  float lfoVcfIdx, float driftIdx) {
    const Cs80ChanParams& p = cs.ch[ci];
    Cs80Channel& c = v->ch[ci];

    c.osc.wave = p.wave;
    cs80OscSetFreq(&c.osc, baseFreq * p.feet * p.tune);
    c.pwmInc = p.pwmInc;

    const float env = cs80FiltProcess(&c.filtEnv);
    const float at  = v->aftertouch;

    const float common = cs.brilliance + env + lfoVcfIdx + driftIdx
                       + v->velNorm * p.velBri
                       + at * p.atBri;

    // See CS_HPF_TRACK_SCALE (config.h): the real HPF section tracks CV at
    // half the LPF section's rate, so the same modulation bus sweeps the two
    // filters by different amounts even though they share one CUTOFF-knob
    // baseline each (hpIdx/lpIdx, untouched below).
    float hp = p.hpIdx + common * CS_HPF_TRACK_SCALE;
    float lp = p.lpIdx + common;
    const float hi = (float)(FILT_TAB_SIZE - 1);
    if (hp < 0.0f) hp = 0.0f; else if (hp > hi) hp = hi;
    if (lp < 0.0f) lp = 0.0f; else if (lp > hi) lp = hi;

    float hres = p.hpRes + cs.resonance; if (hres > 1.0f) hres = 1.0f;
    float lres = p.lpRes + cs.resonance; if (lres > 1.0f) lres = 1.0f;
    cs80SvfSet(&c.hpf, hp, hres);
    cs80SvfSet(&c.lpf, lp, lres);
}

// One sample of one channel.
static inline float renderChannel(Cs80Voice* v, int ci, float noise) {
    const Cs80ChanParams& p = cs.ch[ci];
    Cs80Channel& c = v->ch[ci];

    float pw = p.pw;
    if (p.wave == CSW_PULSE && p.pwmDepth > 0.001f) {
        c.pwmPhase += c.pwmInc;
        pw += sineFromPhase(c.pwmPhase) * p.pwmDepth;
        if (pw < 0.05f) pw = 0.05f; else if (pw > 0.95f) pw = 0.95f;
    }

    float x = cs80OscNext(&c.osc, pw);
    if (p.noise > 0.004f) x += noise * p.noise;

    float lp, hp;
    cs80SvfProcess(&c.hpf, x, &lp, &hp);
    cs80SvfProcess(&c.lpf, hp, &lp, &hp);

    float out = cs80OnePole(&c.fixedLp, lp, CS_FIXED_LP_G);

    if (p.sineLvl > 0.004f) out += sineFromPhase(c.osc.phase) * p.sineLvl;

    return out * cs80AmpProcess(&c.ampEnv);
}

int cs80RenderBlockE(int16_t* audioBuf, float pitchBend, float modWheel,
                     float chanAftertouch) {
    const float bendMul = fastPow2(pitchBend * cs.bendRange * (1.0f / 12.0f));
    const float modW    = modWheel;
    const float chanAT  = chanAftertouch;

    const float gI  = (cs.mix <= 0.5f) ? 1.0f : (2.0f - 2.0f * cs.mix);
    const float gII = (cs.mix >= 0.5f) ? 1.0f : (2.0f * cs.mix);
    const float lvlI  = cs.ch[0].level * gI;
    const float lvlII = cs.ch[1].level * gII;
    const bool  useI  = lvlI  > 0.002f;
    const bool  useII = lvlII > 0.002f;

    const bool fxRing   = cs.ringDepth   > 0.004f;
    const bool fxChorus = cs.tremMix     > 0.004f;
    const bool fxDelay  = cs.dlyMix      > 0.004f;
    const bool fxReverb = cs.revMix      > 0.004f;
    // MASTER used to be multiplied straight into the saturator below, so
    // turning the volume up also drove more distortion. CS_MIX_DRIVE is a
    // fixed pre-saturator drive instead, tuned from a probe over every
    // stock preset (chord across all 8 voices x 2 channels, 1.5s render):
    // peaks ran 0.21-3.13 (this engine is always both channels, so it runs
    // hottest of all six). 0.42 puts the loudest preset at a moderate,
    // characterful tanh knee (~0.86) without flattening the quiet ones.
    // MASTER itself is now applied as pure linear gain after the saturator
    // and the whole effect chain — see the end of this loop.
    const float CS_MIX_DRIVE = 0.42f;

    g_fxChorus.setRate(cs.tremRate);
    g_fxReverb.setRoom(cs.revSize, 0.4f);
    g_fxDelay.setTimeMs(cs.dlyTimeMs, false);
    cs80SvfSet(&finalLp, cs.finalLpIdx, 0.0f);

    uint32_t lfoInc = cs.lfoInc;
    if (cs.atLfoSpeed > 0.004f)
        lfoInc = (uint32_t)(lfoInc * (1.0f + chanAT * cs.atLfoSpeed * 3.0f));

    if (ringTrigger) { ringTrigger = false; ringEnvStage = 1; ringEnv = 0.0f; }

    int active = 0;

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        const bool ctrl = ((n & (CONTROL_RATE_DIV - 1)) == 0);

        uint32_t prevLfo = lfoPhase;
        lfoPhase += lfoInc;
        float lfo;
        switch (cs.lfoWave) {
        case CSLFO_SAWUP: lfo = 2.0f * phaseToFloat(lfoPhase) - 1.0f; break;
        case CSLFO_SAWDN: lfo = 1.0f - 2.0f * phaseToFloat(lfoPhase); break;
        case CSLFO_SQR:   lfo = (lfoPhase & 0x80000000u) ? -1.0f : 1.0f; break;
        case CSLFO_SH:    if (lfoPhase < prevLfo) lfoShValue = noiseWhite(&noiseGen);
                          lfo = lfoShValue; break;
        default:          lfo = sineFromPhase(lfoPhase); break;
        }
        const float lfoAmt = 1.0f + chanAT * cs.atLfoDepth * 2.0f + modW;
        const float lfoV   = lfo * lfoAmt;

        const float lfoPitchOfs = lfoV * cs.lfoPitchHz;
        const float lfoVcfIdx   = lfoV * cs.lfoVcf;
        const float lfoVcaGain  = 1.0f - (0.5f + 0.5f * lfoV) * cs.lfoVca;

        const float noise = noiseWhite(&noiseGen);

        if (ctrl) {
            if (ringEnvStage == 1) {
                ringEnv += cs.ringEgAtk;
                if (ringEnv >= 1.0f) { ringEnv = 1.0f; ringEnvStage = 2; }
            } else if (ringEnvStage == 2) {
                ringEnv -= ringEnv * cs.ringEgDec;
                if (ringEnv < 0.001f) { ringEnv = 0.0f; ringEnvStage = 0; }
            }
        }

        float mix = 0.0f;
        int sounding = 0;

        for (int i = 0; i < CS_VOICES; i++) {
            Cs80Voice* v = &voices[i];
            const bool aliveI  = v->ch[0].ampEnv.stage != CA_IDLE;
            const bool aliveII = v->ch[1].ampEnv.stage != CA_IDLE;
            if (!aliveI && !aliveII) { va.busy[i] = 0; continue; }

            if (ctrl) {
                v->aftertouch = (float)g_voiceAT[i] * (1.0f / 127.0f);
                if (v->aftertouch < chanAT) v->aftertouch = chanAT;

                if (v->gliding) {
                    float d = v->targetFreq - v->currentFreq;
                    if (d < 0.05f && d > -0.05f) { v->currentFreq = v->targetFreq; v->gliding = false; }
                    else v->currentFreq += d * v->glideCoef;
                }
                float f = v->currentFreq;
                if (cs.portaMode == PORTA_GLISS && v->gliding) {
                    int semi = (int)lroundf(69.0f + 12.0f * log2f(f * (1.0f / 440.0f)));
                    f = noteToFreq(semi);
                }
                f = f * bendMul * fastPow2(v->driftCents * cs.drift * (1.0f / 1200.0f))
                    + lfoPitchOfs;
                if (f < 8.0f) f = 8.0f; else if (f > 16000.0f) f = 16000.0f;

                const float driftIdx = v->driftFilt * cs.drift * FILT_IDX_PER_OCT;
                if (useI)  channelControl(v, 0, f, lfoVcfIdx, driftIdx);
                if (useII) channelControl(v, 1, f, lfoVcfIdx, driftIdx);

                // Velocity/aftertouch/drift trim: only aftertouch (just
                // updated above) can move between control ticks, so this is
                // safe to cache here rather than redo every sample below.
                const float velScale = 1.0f - cs.ch[0].velLvl + cs.ch[0].velLvl * v->velNorm;
                const float atScale  = 1.0f + v->aftertouch * cs.ch[0].atLvl;
                const float trim     = 1.0f + (v->driftLevel - 1.0f) * cs.drift;
                v->mixScale = velScale * atScale * trim;
            }

            // A channel gated off by useI/useII (Level at zero, or MIX panned
            // fully to the other side) skips the expensive oscillator+filter
            // synthesis below, same as always — that CPU saving is deliberate
            // (see the README). But its envelope state machines must still
            // advance even while inaudible: cs80AmpProcess is the ONLY thing
            // that ever moves ampEnv out of CA_RELEASE, and that call
            // previously lived inside renderChannel, so skipping
            // renderChannel entirely froze a released channel's envelope at
            // whatever stage it was in for as long as it stayed unused —
            // permanently marking the voice "alive" (aliveI/aliveII gate
            // va.busy[]) even though it was already silent and released.
            // cs80AmpProcess's coefficients are calibrated for SAMPLE_RATE_F
            // and must run every sample, matching its call inside
            // renderChannel. cs80FiltProcess's coefficients (vcfDec/vcfRel),
            // by contrast, are calibrated for CTRL_RATE — it is normally only
            // ever called from channelControl(), which itself only runs
            // `if (ctrl)` (once every CONTROL_RATE_DIV samples). Calling it
            // every sample here would race filtEnv through CA_RELEASE up to
            // 8x faster than the panel's Filter Release/Decay settings say,
            // so it is gated the same way channelControl() is.
            float voiceOut = 0.0f;
            if (useI)  { if (aliveI)  voiceOut += renderChannel(v, 0, noise) * lvlI; }
            else       { cs80AmpProcess(&v->ch[0].ampEnv); if (ctrl) cs80FiltProcess(&v->ch[0].filtEnv); }
            if (useII) { if (aliveII) voiceOut += renderChannel(v, 1, noise) * lvlII; }
            else       { cs80AmpProcess(&v->ch[1].ampEnv); if (ctrl) cs80FiltProcess(&v->ch[1].filtEnv); }

            mix += voiceOut * v->mixScale;

            va.busy[i] = 1;
            sounding++;
        }
        if (sounding > active) active = sounding;

        if (sounding > 0) mix *= INV_SQRT_N[sounding] * 0.9f;
        mix *= lfoVcaGain;

        if (fxRing) {
            float rf = cs.ringSpeedHz * (1.0f + ringEnv * cs.ringEgDepth * 8.0f);
            ringPhase += (uint32_t)(rf * PHASE_PER_HZ);
            float carrier = sineFromPhase(ringPhase);
            mix += (mix * carrier - mix) * cs.ringDepth;
        }

        float flp, fhp;
        cs80SvfProcess(&finalLp, mix, &flp, &fhp);
        mix = flp;

        mix = fastTanh(mix * CS_MIX_DRIVE);
        float l = mix, r = mix;

        if (fxChorus) g_fxChorus.process(l, r, cs.tremDepth, cs.tremMix);
        if (fxDelay)  g_fxDelay.process(l, r, cs.dlyFb, cs.dlyMix, false);
        if (fxReverb) {
            float rl, rr;
            g_fxReverb.process((l + r) * 0.5f, rl, rr);
            l += rl * cs.revMix;
            r += rr * cs.revMix;
        }

        // MASTER: pure linear gain, after the saturator and the whole
        // effect chain — see CS_MIX_DRIVE above for why it doesn't live in
        // the tanh argument any more.
        l *= cs.master; r *= cs.master;

        audioBuf[n * 2]     = (int16_t)(safeClamp(l, -1.0f, 1.0f) * 32000.0f);
        audioBuf[n * 2 + 1] = (int16_t)(safeClamp(r, -1.0f, 1.0f) * 32000.0f);
    }

    return active;
}

/*
 * Carpenter Synth — RP2350 multi-engine workstation, "Synthesize the Season" edition
 * ───────────────────────────────────────────────────────────────────────────
 *  A Halloween/John-Carpenter-scored reskin of UltimateSynthNYR, with its
 *  original single-engine voice replaced by the multi-engine architecture
 *  (17 engines live, CS-80 through Prophet-VS — see engine.h and README.md's
 *  Synth engines section for the full roster) ported from this project's
 *  own ESP32-S3 branch, and then back onto the main xtro board's own
 *  hardware: 128x64 SSD1309 OLED, rotary encoder, five panel buttons, two
 *  performance switches and two pots. Same sequencer (4 tracks, time
 *  signatures, chains, 200 riffs, songs), same audio and DSP to the byte —
 *  only the display, the control surface and the branding changed in that
 *  port. See README.md.
 *
 *  Target    : Raspberry Pi Pico 2 / RP2350, 16 MB flash
 *  Toolchain : Arduino IDE + "Raspberry Pi Pico/RP2040/RP2350" core
 *              (Earle Philhower, arduino-pico) — board "Raspberry Pi Pico 2".
 *  Libraries : U8g2, and Synth_Dexed (the DX7 engine — a ready-to-install
 *              copy is vendored at extras/Synth_Dexed/; see README.md).
 *              Everything else — I2S, SerialPIO, FatFS — ships with the
 *              core. No MIDI library, no AudioTools.
 *  Licence   : GPLv3 as a whole — Synth_Dexed is GPLv3 over Apache-2.0.
 *              See README.md's Licensing section.
 *
 *  Hardware — exact CS-80 / New York Ripper board: same pinout, same 128x64
 *  SSD1309 OLED, same encoder-and-five-button panel, same two performance
 *  switches and two performance pots. See config.h for the full pinout:
 *    Audio  : PCM5102 I2S   BCK=GP15  LRCK=GP16  DIN=GP17
 *    Display: SSD1309 128x64 OLED, hardware I2C  SDA=GP4  SCL=GP5
 *    Encoder: A=GP6 B=GP7 SW=GP8
 *    Buttons: PLAY=GP9 SHIFT=GP10 PAGE=GP11 TRACK=GP12 MUTE=GP13
 *    Perf sw: OCT DOWN=GP2  OCT UP=GP3
 *    Perf pot: CUTOFF=GP26 (ADC0)  RESONANCE=GP27 (ADC1)
 *    Click  : GP14 (metronome gate out)
 *    MIDI   : OUT = GP1, IN = GP0  (PIO UART — see config.h)
 *
 *  Board settings (Tools):
 *    CPU Speed  : 250 MHz
 *    Flash Size : a layout that reserves an FS partition, e.g. 8 MB / 8 MB FS
 *    USB Stack  : "Pico SDK" — required for SETTINGS -> USB DRIVE (FatFSUSB
 *                 registers through the core's own pluggable-USB layer; the
 *                 Adafruit TinyUSB stack is a hard #error against it)
 *
 *  Architecture:
 *    core0 — sequencer timing, UI, controls, MIDI in/out, storage, arpeggiator
 *    core1 — audio only: whichever engine is live renders + applies its own
 *            FX chain -> I2S (see engine.h — exactly one engine at a time)
 */

#include "config.h"
#include "dsp_tables.h"
#include "engine.h"
#include "dx7.h"
#include "voicebank.h"
#include "voicealloc.h"
#include "fx.h"
#include "sequencer.h"
#include "controls.h"
#include "midi.h"
#include "storage.h"
#include "ui.h"
#include "arps.h"
#include "voicequeue.h"
#include "scales.h"

#include <I2S.h>
#include <FatFS.h>
#include <FatFSUSB.h>

// ═══════════════════════════════════════════════════════════════════════════
//  Globals
// ═══════════════════════════════════════════════════════════════════════════
Sequencer   seqs[NUM_TRACKS];
MidiIO      midi;
Storage     storage;
Controls    controls;
UI          ui;
GlobalSettings gs;

uint8_t     activeTrack = 0;
int         g_currentPreset = 0;
bool        g_recArm = false;

volatile float g_cpuLoad = 0.0f;
volatile int   g_activeVoices = 0;
volatile float g_peakL = 0.0f, g_peakR = 0.0f;

// Live polyphony ceiling.  Lowered automatically if core1 starts running out of
// time, raised again when it recovers — an overloaded render is a stream of
// clicks, and dropping the oldest voice is far less offensive than that.
static volatile uint8_t g_voiceLimit = MAX_VOICES;

I2S i2s(OUTPUT);
static volatile bool g_audioReady = false;

// ── Flash-write safety (RP2350 XIP) ────────────────────────────────────────
// Erasing flash stalls XIP, so core1 must not be fetching instructions from
// flash while core0 writes the filesystem.  arduino-pico's flash backend does
// call idleOtherCore() itself, so this handshake is not what keeps the chip
// alive — it is what makes core1 stop at a clean block boundary of its own
// accord, instead of being frozen mid-render and resuming into a half-filled
// I2S buffer.  The 50 ms bail-out is safe for exactly that reason.
static volatile bool g_flashPause = false;
static volatile bool g_audioParked = false;

static void __no_inline_not_in_flash_func(audioParkSpin)() {
    g_audioParked = true;
    while (g_flashPause) { tight_loop_contents(); }
    g_audioParked = false;
}
void audioFlashBegin() {
    if (!g_audioReady) return;
    g_flashPause = true;
    unsigned long t0 = millis();
    while (!g_audioParked && (millis() - t0) < 50) { tight_loop_contents(); }
}
void audioFlashEnd() { g_flashPause = false; }

// ── USB mass storage (drag-and-drop DX7 voice banks) ────────────────────────
// FatFSUSB exports the FS partition to a PC as a memory stick. FAT allows only
// ONE writer, so while the host has it mounted this box must not touch the
// filesystem at all -- not even reads, since the host's own housekeeping
// writes would corrupt it. g_diskMode (storage.h) is that gate: storage.cpp
// and voicebank.cpp both refuse every FatFS access while it's set. The
// sequencer keeps running regardless -- nothing about the host owning the
// flash requires the music to stop.
//
// Not exported at boot: SETTINGS -> USB DRIVE (ui.cpp) is what calls
// uiUsbBegin() below, a deliberate, on-screen action, because once the host
// has it mounted it may erase/program flash at any moment, and a flash write
// stalls XIP for whichever core is executing from it -- the audio core.
// Keeping MSC off during normal play removes that race entirely rather than
// hoping it never lands mid-render.
static volatile bool g_dx7BanksDirty = false;   // set on eject: /banks needs rescanning
// Mount/unmount deferred to loop() -- see the two callbacks just below.
static volatile bool g_fsUnmountReq = false;
static volatile bool g_fsRemountReq = false;

// These run in USB *interrupt* context, so they may do nothing but set flags.
// Calling FatFS.end()/begin() from here was the real hazard being guarded
// against: unmounting frees the FTL and flushes metadata (a flash erase,
// which idles the other core) while core0's own main loop could be
// part-way through a FatFS call of its own -- and the host issues the SCSI
// command that triggers this automatically, without anyone here asking for
// it. loop() picks the flags up instead, exactly like a save.
static void usbPlug(uint32_t) {
    g_diskMode     = true;            // stop every FS access at once
    g_fsUnmountReq = true;
}
static void usbUnplug(uint32_t) {
    g_fsRemountReq = true;            // loop() remounts, then clears g_diskMode
}
// The host may only mount while core0 isn't mid-flash-write -- reuses the
// same g_flashPause a save/format already sets, rather than a second flag
// tracking the identical window.
static bool usbDriveReady(uint32_t) { return !g_flashPause; }

// Enter/leave USB drive mode -- thin wrappers around FatFSUSB itself. The
// transport-stop/silence/screen-push side lives in ui.cpp, which composes
// these with its own uiStopAll()/uiPanic() the same way it already composes
// other multi-step panel actions.
void uiUsbBegin() {
    FatFSUSB.begin();
    delay(100);            // TinyUSB settle after re-enumerating (per core example)
}
void uiUsbEnd() {
    FatFSUSB.end();
    if (g_diskMode) {
        // The host still had it mounted, so the filesystem is currently
        // unmounted on our side -- ask loop() to take it back.
        g_fsRemountReq = true;
    } else {
        g_dx7BanksDirty = true;   // rescan /banks on the next loop() pass
    }
}

// ui.cpp is deliberately engine-agnostic (it only ever calls the generic
// engineXxx() dispatch, never a DX7 function directly), so it can't touch
// g_dx7BanksDirty itself -- that flag is file-scope here for exactly that
// reason. This is the hook it calls instead, e.g. after FORMAT FS wipes
// /banks along with everything else.
void uiBanksMayHaveChanged() { g_dx7BanksDirty = true; }

// ═══════════════════════════════════════════════════════════════════════════
//  Voice command queue (core0 -> core1) — see voicequeue.h
// ═══════════════════════════════════════════════════════════════════════════
VMsg vq[VQ_SIZE];
volatile uint32_t vqHead = 0, vqTail = 0;

// Core0-owned voice bookkeeping — engine-agnostic (see voicealloc.h): every
// engine module externs this same instance to publish per-voice busy state.
VoiceAlloc va;

// ═══════════════════════════════════════════════════════════════════════════
//  Modulation state shared with core1
// ═══════════════════════════════════════════════════════════════════════════
static volatile float g_pitchBend = 0.0f;    // -1..1
static volatile float g_modWheel  = 0.0f;
static volatile float g_chanAftertouch = 0.0f;

// ═══════════════════════════════════════════════════════════════════════════
//  CORE 1 — audio
// ═══════════════════════════════════════════════════════════════════════════
static int16_t audioBuf[AUDIO_BLOCK * 2];

// Drains the core0->core1 note queue into whichever engine is currently
// live. Bounded so a burst can't starve the render — matches the queue-drain
// shape every engine already assumes (see engine.h's note-control comments).
static inline void drainVoiceQueue() {
    VMsg m;
    for (int c = 0; c < 12 && vqPop(&m); c++) {
        if (m.cmd == VC_ALL_OFF)      engineAllOff();
        else if (m.cmd == VC_NOTE_ON) engineNoteOn(m.voice, m.note, m.vel, m.trigEnv);
        else                          engineNoteOff(m.voice, (int)m.note);
    }
}

// __no_inline_ matters as much as _not_in_flash_: with a plain
// __not_in_flash_func the compiler inlines this whole function into loop1(),
// which is NOT marked, and the copy that actually executes ends up back in
// flash — silently undoing the placement.  Verified with nm on the ELF: the
// symbol must live at 0x2xxxxxxx, not 0x10xxxxxx.
static void __no_inline_not_in_flash_func(renderBlock)() {
    drainVoiceQueue();

    // The whole render — oscillators, filters, envelopes, the engine's own
    // effects chain and saturator/master gain — happens inside whichever
    // engine is live (see engine.h). This function's only remaining job is
    // feeding it the shared performance state and turning its returned
    // active-voice count into the PERF meter.
    int active = engineRenderBlock(audioBuf, g_pitchBend, g_modWheel, g_chanAftertouch);

    float peakL = 0.0f, peakR = 0.0f;
    for (int n = 0; n < AUDIO_BLOCK; n++) {
        float l = fabsf((float)audioBuf[n * 2]     * (1.0f / 32768.0f));
        float r = fabsf((float)audioBuf[n * 2 + 1] * (1.0f / 32768.0f));
        if (l > peakL) peakL = l;
        if (r > peakR) peakR = r;
    }
    g_peakL = peakL; g_peakR = peakR;
    g_activeVoices = active;
}

void setup1() {
    while (!g_audioReady) { tight_loop_contents(); }
    i2s.setBCLK(PIN_I2S_BCLK);        // LRCK is BCLK+1 = GP16 on arduino-pico
    i2s.setDATA(PIN_I2S_DOUT);
    i2s.setBitsPerSample(16);
    i2s.setFrequency(SAMPLE_RATE);
    // Depth, not block size: AUDIO_BLOCK stays 64 samples (1.33 ms) so note-on
    // is tight, while I2S_BUFFERS of them gives core1 a cushion to ride out
    // anything that momentarily steals its time.
    i2s.setBuffers(I2S_BUFFERS, AUDIO_BLOCK);
    i2s.begin();
}

void loop1() {
    if (g_flashPause) audioParkSpin();

    uint32_t t0 = micros();
    renderBlock();
    uint32_t dt = micros() - t0;

    // One block is AUDIO_BLOCK/SAMPLE_RATE seconds of audio; the fraction of
    // that spent rendering is the CPU load.  Smoothed so the meter is readable.
    const float blockUs = (float)AUDIO_BLOCK * 1000000.0f / SAMPLE_RATE_F;
    float load = (float)dt / blockUs;
    g_cpuLoad += (load - g_cpuLoad) * 0.05f;

    // Overload guard: shed a voice before the buffer underruns, take it back
    // when there is room again.  Clicks are worse than a stolen voice.
    // Notes already sounding always run to completion — lowering the ceiling
    // force-releases exactly the one voice at the new boundary, not anything
    // below it.
    static uint16_t hot = 0, cool = 0;
    uint8_t maxV = engineNumVoices();
    if (g_cpuLoad > 0.90f) {
        if (++hot > 40 && g_voiceLimit > 1) {
            g_voiceLimit--;
            engineNoteOff(g_voiceLimit, -1);   // forced by the CPU guard, not a real key release
            hot = 0;
        }
        cool = 0;
    } else if (g_cpuLoad < 0.65f) {
        hot = 0;
        if (++cool > 400 && g_voiceLimit < maxV) { g_voiceLimit++; cool = 0; }
    }

    for (int n = 0; n < AUDIO_BLOCK; n++) {
        i2s.write(audioBuf[n * 2]);
        i2s.write(audioBuf[n * 2 + 1]);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  CORE 0 — voice allocation
// ═══════════════════════════════════════════════════════════════════════════
// Mono/duo/poly all fall out of `va.limit` (== engineNumVoices()) rather than
// a separate PLAY_MODE switch: a 1-voice engine (Minimoog) always steals its
// single voice, a 2-voice one (ARP Odyssey) behaves like a duo, and an 8/10-
// voice one (CS-80/Prophet-10) is plainly polyphonic — see voicealloc.h.
static void synthNoteOnRaw(int note, int vel, bool trigEnv) {
    note = constrain(note, 0, 127);
    va.limit = g_voiceLimit;
    int vi = va.noteOn(note);
    engineSetVoiceAftertouch((uint8_t)vi, 0);   // a reused voice slot starts clean, not at its previous note's pressure
    vqPush(VC_NOTE_ON, (uint8_t)vi, (uint8_t)note, (uint8_t)vel, noteToFreq(note), trigEnv);
}

static void synthNoteOffRaw(int note) {
    int hit[MAX_VOICES];
    int n = va.noteOff(note, hit);
    for (int i = 0; i < n; i++) vqPush(VC_NOTE_OFF, (uint8_t)hit[i], (uint8_t)note, 0, 0.0f, false);
}

static void synthAllOff() {
    va.allOff();
    vqPush(VC_ALL_OFF, 0, 0, 0, 0.0f, false);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Arpeggiator (core0)
// ═══════════════════════════════════════════════════════════════════════════
#define ARP_MAX_NOTES 16
static int  arpBuf[ARP_MAX_NOTES];
static int  arpCount = 0;
static int  arpStep = 0;
static int  arpPlaying = -1;
static int  arpVel = 100;
static bool arpSounding = false;
static unsigned long arpStepStart = 0;

// Every engine's parameter enum ends with the same 8-entry ARP tail, in this
// fixed order (see engine.h's ARP_TAIL_COUNT): ENABLE, PATTERN, RATE, GATE,
// OCTAVES, DIR, LATCH, SYNC. Reading them relative to engineArpBase() means
// this code doesn't care which engine is live. Unlike the single-engine
// build, there is no per-step swing here -- the shared tail doesn't carry
// one, so the arp always plays its raw grid.
static inline bool  arpOn()     { return engineGetParameter(engineArpBase() + 0) > 0.5f; }
static inline bool  arpLatch()  { return engineGetParameter(engineArpBase() + 6) > 0.5f; }
static inline int   arpPattern(){ return constrain((int)(engineGetParameter(engineArpBase() + 1) * ARP_NUM_PATTERNS), 0, ARP_NUM_PATTERNS - 1); }
static inline int   arpOct()    { return 1 + (int)(engineGetParameter(engineArpBase() + 4) * 3.99f); }
static inline int   arpDir()    { return constrain((int)(engineGetParameter(engineArpBase() + 5) * 6.0f), 0, 5); }
static inline float arpGate()   { return 0.1f + engineGetParameter(engineArpBase() + 3) * 0.9f; }

static float arpStepMs() {
    if (engineGetParameter(engineArpBase() + 7) > 0.5f)
        return 15000.0f / (float)seqs[INTERNAL_TRACK].bpm();   // 16th notes
    float bpm = 40.0f + engineGetParameter(engineArpBase() + 2) * 260.0f;
    return 15000.0f / bpm;
}

static void arpAdd(int note, int vel) {
    for (int i = 0; i < arpCount; i++) if (arpBuf[i] == note) return;
    if (arpCount >= ARP_MAX_NOTES) return;
    arpBuf[arpCount++] = note;
    arpVel = vel;
    for (int i = 0; i < arpCount - 1; i++)               // keep sorted ascending
        for (int j = i + 1; j < arpCount; j++)
            if (arpBuf[i] > arpBuf[j]) { int t = arpBuf[i]; arpBuf[i] = arpBuf[j]; arpBuf[j] = t; }
    if (arpCount == 1) { arpStep = 0; arpStepStart = millis(); }
}

static void arpRemove(int note) {
    if (arpLatch()) return;
    for (int i = 0; i < arpCount; i++) {
        if (arpBuf[i] == note) {
            for (int j = i; j < arpCount - 1; j++) arpBuf[j] = arpBuf[j + 1];
            arpCount--;
            return;
        }
    }
}

static int arpCalcNote() {
    if (arpCount == 0) return 60;
    int dir = arpDir();
    if (dir == 5) return arpBuf[0] + ARP_PATTERNS[arpPattern()][arpStep & 15];

    int oct = arpOct();
    int total = arpCount * oct;
    if (total < 1) total = 1;
    int pos;
    switch (dir) {
    case 0: pos = arpStep % total; break;                            // up
    case 1: pos = total - 1 - (arpStep % total); break;              // down
    case 2: {                                                        // up-down
        int cycle = (total > 1) ? (total * 2 - 2) : 1;
        int p = arpStep % cycle;
        pos = (p < total) ? p : (cycle - p);
        break;
    }
    case 3: {                                                        // down-up
        int cycle = (total > 1) ? (total * 2 - 2) : 1;
        int p = arpStep % cycle;
        pos = (p < total) ? (total - 1 - p) : (p - total + 1);
        break;
    }
    default: pos = (int)random(total); break;                        // random
    }
    pos = constrain(pos, 0, total - 1);
    return arpBuf[pos % arpCount] + (pos / arpCount) * 12;
}

static void arpProcess() {
    if (!arpOn() || arpCount == 0) {
        if (arpSounding) { synthNoteOffRaw(arpPlaying); arpSounding = false; arpPlaying = -1; }
        return;
    }
    unsigned long now = millis();
    float stepMs = arpStepMs();
    unsigned long elapsed = now - arpStepStart;

    if (arpSounding && elapsed >= (unsigned long)(stepMs * arpGate())) {
        synthNoteOffRaw(arpPlaying);
        arpSounding = false;
    }
    if (elapsed >= (unsigned long)stepMs) {
        arpStep = (arpStep + 1) & 15;
        int note = constrain(arpCalcNote(), 0, 127);
        if (arpSounding) synthNoteOffRaw(arpPlaying);
        arpPlaying = note;
        synthNoteOnRaw(note, arpVel, true);
        arpSounding = true;
        arpStepStart = now;
    }
}

// Front door for anything that wants to make a sound on the internal engine.
static void synthNoteOn(int note, int vel) {
    if (arpOn()) arpAdd(note, vel);
    else         synthNoteOnRaw(note, vel, true);
}
static void synthNoteOff(int note) {
    if (arpOn()) arpRemove(note);
    else         synthNoteOffRaw(note);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Parameter locks
// ═══════════════════════════════════════════════════════════════════════════
// A lock is momentary: the first step to lock a parameter records what it was,
// and everything is put back when the transport stops.  That way a locked
// pattern never silently rewrites the patch you saved.
static float plockOrig[MAX_ENGINE_PARAMS];
static bool  plockHeld[MAX_ENGINE_PARAMS];

static void plockApply(uint8_t id, uint8_t value) {
    if (id >= MAX_ENGINE_PARAMS) return;
    if (!plockHeld[id]) { plockOrig[id] = engineGetParameter(id); plockHeld[id] = true; }
    engineSetParameter(id, (float)value * (1.0f / 255.0f));
}
static void plockRestoreAll() {
    for (uint8_t i = 0; i < MAX_ENGINE_PARAMS; i++) {
        if (plockHeld[i]) { engineSetParameter(i, plockOrig[i]); plockHeld[i] = false; }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  Sequencer routing
// ═══════════════════════════════════════════════════════════════════════════
static void onSeqNoteOn(uint8_t track, uint8_t chan, uint8_t note, uint8_t vel,
                        bool accent, bool slide) {
    (void)accent; (void)slide;
    midi.noteOn(chan, note, vel);
    if (track == INTERNAL_TRACK) synthNoteOn(note, vel);
}
static void onSeqNoteOff(uint8_t track, uint8_t chan, uint8_t note) {
    midi.noteOff(chan, note);
    if (track == INTERNAL_TRACK) synthNoteOff(note);
}
void seqEmitClock()    { midi.clock(); }
void seqEmitStart()    { midi.start(); }
void seqEmitStop()     { midi.stop(); }
void seqEmitContinue() { midi.continueMsg(); }

// ═══════════════════════════════════════════════════════════════════════════
//  MIDI in
// ═══════════════════════════════════════════════════════════════════════════
// OCT DOWN/UP (GP2/GP3) transpose incoming MIDI performance notes only — the
// sequencer's own patterns are left alone. g_noteOctShift records the shift
// that was ACTUALLY applied at note-on, per raw incoming note number, so a
// note-off is released at the same transposed pitch even if the octave
// button was pressed again while the note was still held — otherwise that
// would either leave a stuck note or release the wrong one.
static int8_t g_octaveShift = 0;
static int8_t g_noteOctShift[128] = {0};

// Sustain pedal (CC 64). Not engine DSP -- a real synth's sustain is "the
// note keeps ringing after key-up until the pedal itself comes up", which is
// entirely a note-ROUTING behaviour, so it's implemented once here for
// whichever engine is live rather than touched in all seventeen of them.
// Indexed by RAW incoming MIDI note number, same as g_noteOctShift above, so
// the two features never fight over which array slot a note owns.
static bool g_sustainOn = false;
static bool g_sustainHeld[128] = {false};

static void onMidiNoteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    if (gs.midiThru) midi.noteOn(ch, note, vel);
    g_noteOctShift[note & 0x7F] = g_octaveShift;
    // A fresh key-down retakes ownership of its note from the sustain pedal:
    // without this, a same-pitch retrigger while the pedal is already held
    // would still be marked "ringing only because of the pedal" from its
    // PREVIOUS release, and the eventual pedal-up would cut the note the
    // player is currently holding down.
    g_sustainHeld[note & 0x7F] = false;
    int n = constrain((int)note + g_octaveShift * 12, 0, 127);
    // "Kbd Play" (MIDI SET page) gates only the NEW note-on -- never the
    // matching note-off in onMidiNoteOff() below, so turning it off mid-note
    // can't strand a voice sounding forever. Recording is independent of it:
    // a keyboard can step-record a pattern with this off and no live monitor
    // sounding, same as it could already record with it on.
    if (gs.midiKbdPlay) synthNoteOn(n, vel);
    if (g_recArm) seqs[activeTrack].recordNote(n, vel);
}
static void onMidiNoteOff(uint8_t ch, uint8_t note, uint8_t vel) {
    (void)vel;
    if (gs.midiThru) midi.noteOff(ch, note);
    // Pedal down: defer the audible release rather than stopping the note --
    // the flush lives in onMidiCC's CC-64 handler, below.
    if (g_sustainOn) { g_sustainHeld[note & 0x7F] = true; return; }
    int n = constrain((int)note + g_noteOctShift[note & 0x7F] * 12, 0, 127);
    synthNoteOff(n);
}
static void onMidiCC(uint8_t ch, uint8_t cc, uint8_t val) {
    (void)ch;
    float n = (float)val * (1.0f / 127.0f);
    if (cc == 1) { g_modWheel = n; return; }
    if (cc == 123 || cc == 120) {
        synthAllOff();
        for (int raw = 0; raw < 128; raw++) g_sustainHeld[raw] = false;   // nothing left ringing to defer
        return;
    }
    if (cc == 64) {
        bool down = val >= 64;
        if (g_sustainOn && !down) {
            // Pedal released: flush every note that's ringing only because
            // of it -- everything else already stopped at its own key-up.
            for (int raw = 0; raw < 128; raw++) {
                if (!g_sustainHeld[raw]) continue;
                g_sustainHeld[raw] = false;
                int held = constrain(raw + g_noteOctShift[raw] * 12, 0, 127);
                synthNoteOff(held);
            }
        }
        g_sustainOn = down;
        return;
    }
    // Everything else resolves through the live engine's own parameter
    // table, so the CC map and the UI can never drift apart.
    uint8_t numParams = engineNumParams();
    for (uint8_t i = 0; i < numParams; i++) {
        if (engineParamInfo(i)->cc == cc) { engineSetParameter(i, n); ui.markDirty(); return; }
    }
}
static void onMidiPC(uint8_t ch, uint8_t prog) {
    (void)ch;
    if (prog < engineNumPresets()) { engineLoadPreset(prog); g_currentPreset = prog; ui.markDirty(); }
}
static void onMidiBend(uint8_t ch, int16_t bend) {
    (void)ch;
    g_pitchBend = (float)bend * (1.0f / 8192.0f);
}
static void onMidiPolyAT(uint8_t ch, uint8_t note, uint8_t pressure) {
    (void)ch;
    for (int i = 0; i < MAX_VOICES; i++)
        if (va.gate[i] && va.note[i] == note) engineSetVoiceAftertouch((uint8_t)i, pressure);
}
static void onMidiChanAT(uint8_t ch, uint8_t pressure) {
    (void)ch;
    g_chanAftertouch = (float)pressure * (1.0f / 127.0f);
}
static void onMidiRealtime(uint8_t status) {
    switch (status) {
    case MIDI_RT_CLOCK:    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].extTick();     break;
    case MIDI_RT_START:    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].extStart();    break;
    case MIDI_RT_CONTINUE: for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].extContinue(); break;
    case MIDI_RT_STOP:     for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].extStop();     break;
    default: break;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  UI hooks
// ═══════════════════════════════════════════════════════════════════════════
void uiApplySync() {
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        seqs[t].setExternalSync(gs.extSync != 0);
        // Only track 0 emits clock: four tracks each sending 24 PPQN would be
        // 96 PPQN of nonsense, and clock out is suppressed entirely while
        // slaved so two boxes can't fight over the tempo.
        seqs[t].setMidiClock(gs.midiClockOut && !gs.extSync && t == 0);
    }
}

void uiTogglePlayAll() {
    bool anyPlaying = false;
    for (uint8_t t = 0; t < NUM_TRACKS; t++)
        if (seqs[t].playState() == PlayState::PLAYING) anyPlaying = true;
    if (anyPlaying) {
        for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].stop();
        plockRestoreAll();
        synthAllOff();
    } else {
        unsigned long t0 = micros();       // one timestamp so the lanes stay locked
        for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].play(t0);
    }
}

void uiStopAll() {
    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].stop();
    plockRestoreAll();
    synthAllOff();
}

void uiPanic() {
    synthAllOff();
    arpCount = 0; arpSounding = false;
    for (int raw = 0; raw < 128; raw++) g_sustainHeld[raw] = false;
    for (uint8_t ch = 1; ch <= 16; ch++) midi.allNotesOff(ch);
}

void uiLoadFactoryPreset(int idx) {
    engineLoadPreset(idx);
    g_currentPreset = idx;
    for (uint8_t i = 0; i < MAX_ENGINE_PARAMS; i++) plockHeld[i] = false;
}

void uiLoadUserPreset(uint8_t slot) {
    UserPreset p;
    if (storage.loadUserPreset((uint8_t)currentEngine, slot, p)) {
        engineRestoreParams(p.params);
        for (uint8_t k = 0; k < MAX_ENGINE_PARAMS; k++) plockHeld[k] = false;
        g_currentPreset = -1;
        ui.toast(p.name);
    } else {
        ui.toast("EMPTY SLOT");
    }
}

void uiSaveUserPreset(uint8_t slot, const char* name) {
    UserPreset p;
    memset(&p, 0, sizeof(p));
    strncpy(p.name, name, sizeof(p.name) - 1);
    p.engineId = (uint8_t)currentEngine;
    engineSnapshotParams(p.params);
    if (!storage.saveUserPreset((uint8_t)currentEngine, slot, p)) ui.toast("SAVE FAILED");
}

// Switches the live engine (see engine.h): silences the outgoing one and
// resets its param array the first time it's ever selected, but otherwise
// preserves whatever was tweaked -- switching away and back is exactly like
// leaving one real instrument's knobs alone while you play another.
void uiSetEngine(EngineId id) {
    engineSwitchTo(id);
    g_voiceLimit = engineNumVoices();
    for (uint8_t i = 0; i < MAX_ENGINE_PARAMS; i++) plockHeld[i] = false;
    g_currentPreset = 0;
}

// A song is a track's chain + which pattern it's sitting on, not a copy of
// the patterns themselves -- see the Song comment in storage.h.  Loading one
// arms the chain immediately (setChain/clearChain), it doesn't just stage it
// for a later APPLY the way CHAIN_EDIT does, since "load the song" is already
// the explicit action.
void uiLoadSong(uint8_t slot) {
    Song s;
    if (!storage.loadSong(slot, s)) { ui.toast("LOAD FAILED"); return; }
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        const SongTrack& st = s.tracks[t];
        uint8_t n = (st.chainLen <= CHAIN_LEN) ? st.chainLen : CHAIN_LEN;
        for (uint8_t i = 0; i < CHAIN_LEN; i++) gs.chain[t][i] = st.chain[i];
        gs.chainLen[t] = n;
        if (n) seqs[t].setChain(st.chain, n);
        else   seqs[t].clearChain();
        if (st.activePattern < NUM_PATTERNS) seqs[t].setPattern(st.activePattern);
    }
    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].setBPM(s.bpm);
    gs.bpm = s.bpm;
    ui.toast(s.name[0] ? s.name : "SONG LOADED");
}

void uiSaveSong(uint8_t slot, const char* name) {
    // Unlike UserPreset, Song's fields all carry their own default member
    // initialiser (magic/version/name/bpm), so a plain `Song s;` is already
    // fully and correctly zeroed/defaulted -- no memset needed, and memset-ing
    // it anyway trips -Wclass-memaccess for no benefit.
    Song s;
    strncpy(s.name, name, sizeof(s.name) - 1);
    s.bpm = seqs[INTERNAL_TRACK].bpm();
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        s.tracks[t].activePattern = seqs[t].currentPattern();
        s.tracks[t].chainLen      = gs.chainLen[t];
        for (uint8_t i = 0; i < CHAIN_LEN; i++) s.tracks[t].chain[i] = gs.chain[t][i];
    }
    if (!storage.saveSong(slot, s)) ui.toast("SAVE FAILED");
}

// Auditions the note you are dialling in on the step editor.  Released by the
// housekeeping timer in loop().
static unsigned long g_previewOffAt = 0;

void uiPreviewNote(uint8_t note, bool on) {
    static int lastPreview = -1;
    if (!on) { if (lastPreview >= 0) { synthNoteOffRaw(lastPreview); lastPreview = -1; } return; }
    if (lastPreview >= 0) synthNoteOffRaw(lastPreview);
    synthNoteOnRaw(note, 100, true);
    lastPreview = note;
    g_previewOffAt = millis() + 220;
}

void uiSaveAll() {
    gs.bpm       = seqs[INTERNAL_TRACK].bpm();
    gs.lastTrack = activeTrack;
    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        gs.lastPattern[t] = seqs[t].currentPattern();
        gs.trackMute[t]   = seqs[t].muted() ? 1 : 0;
        for (uint8_t p = 0; p < NUM_PATTERNS; p++)
            storage.savePattern(t, p, seqs[t].getPattern(p));
    }
    gs.currentPreset = (uint8_t)((g_currentPreset >= 0) ? g_currentPreset : 0);
    gs.engineId = (uint8_t)currentEngine;
    engineSnapshotParams(gs.params);
    storage.saveSettings(gs);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Demo pattern
// ═══════════════════════════════════════════════════════════════════════════
static void initDemoPattern(Sequencer& s, uint8_t track) {
    Pattern& p = s.getPattern(0);
    p.rootNote = 0;                 // C
    p.scaleIdx = 9;                 // pentatonic minor
    p.midiChannel = track + 1;
    p.length = DEFAULT_STEPS;

    static const uint8_t BASS[] = {36,36,48,36,39,36,43,36,36,48,36,41,36,39,36,34};
    static const uint8_t LEAD[] = {60,63,67,63,70,67,63,60,72,70,67,63,60,63,67,70};
    static const uint8_t ARP [] = {72,75,79,84,79,75,72,67,72,75,79,84,79,75,72,67};
    const uint8_t* src = (track == 0) ? BASS : (track == 2) ? ARP : LEAD;

    for (uint8_t i = 0; i < DEFAULT_STEPS; i++) {
        Step& st = p.steps[i];
        st.notes[0]    = src[i];
        st.noteCount   = 1;
        st.velocity    = (i % 4 == 0) ? 112 : 82;
        st.gate        = (i % 4 == 0) ? 80 : 55;
        st.probability = 100;
        st.ratchet     = 1;
        st.micro       = 0;
        st.flags       = (i % 8 == 0) ? ST_ACCENT : 0;
        bool on = (track == 0) ? true
                : (track == 1) ? (i % 2 == 0)
                : (track == 2) ? (i % 4 == 0) : false;
        if (on) st.flags |= ST_ACTIVE;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  setup / loop (core 0)
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
    // ── 1. DISPLAY FIRST ───────────────────────────────────────────────────
    // Before Serial, before tables, before storage.  Bringing the panel up
    // first is the difference between "it stalled during stage N" and a blank
    // screen that tells you nothing.  The sequence inside beginDisplay() is the
    // New York Ripper one verbatim, because that build is known good on this
    // exact board.
    //
    // There is deliberately NO watchdog in this firmware, for the same reason
    // NYR has none: the only things slow enough to trip one at boot are flash
    // operations, and resetting the chip part-way through a flash operation is
    // strictly worse than waiting for it.
    ui.beginDisplay();

    Serial.begin(115200);

    randomSeed(rp2040.hwrand32());
    pinMode(PIN_CLICK_OUT, OUTPUT);
    digitalWrite(PIN_CLICK_OUT, LOW);

    // Holding SHIFT at power-up skips the filesystem entirely and boots on
    // the built-in defaults — the way back in if a bad save ever wedges the box.
    pinMode(PIN_BTN_SHIFT, INPUT_PULLUP);
    delay(2);
    const bool safeBoot = (digitalRead(PIN_BTN_SHIFT) == LOW);

    // ── 2. Engine ──────────────────────────────────────────────────────────
    ui.bootStatus("DSP TABLES");
    dspTablesInit();
    va.reset();

    ui.bootStatus("EFFECTS");
    g_fxChorus.begin();
    g_fxDelay.begin();
    g_fxReverb.begin();

    // ── 3. Storage ─────────────────────────────────────────────────────────
    // Mount only; see Storage::begin().  A missing or unformatted filesystem
    // is reported and then ignored — the instrument is fully playable from RAM.
    ui.bootStatus(safeBoot ? "SAFE BOOT" : "STORAGE");
    bool fsOk = false;
    if (!safeBoot) {
        fsOk = storage.begin();
        if (!fsOk) {
            Serial.println("[WARN] no filesystem - Tools > Flash Size must reserve one");
            ui.bootStatus("NO FS - RAM ONLY");
            delay(1200);
        }
    }

    // DX7 filesystem extension banks (voicebank.h / SETTINGS -> USB DRIVE).
    // Wired unconditionally -- vbBankCount() simply reports 0 until a scan
    // finds something, so this is exactly as safe with no filesystem as
    // leaving it null was -- but it has to happen HERE, before
    // engineRestoreParams() below can replay a saved BANK value that points
    // at one of these banks: wiring it any later would make that restore
    // silently fail to find the voice on this boot.
    dxExtBankCount = vbBankCount;
    dxExtBankName  = vbBankName;
    dxExtGetVoice  = vbGetVoice;
    if (fsOk) vbRescan();

    seqNoteOn  = onSeqNoteOn;
    seqNoteOff = onSeqNoteOff;
    seqLock    = plockApply;

    ui.bootStatus("SEQUENCER");
    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].begin(t);

    bool haveSettings = fsOk && storage.loadSettings(gs);
    EngineId bootEngine = (haveSettings && gs.engineId < NUM_ENGINES)
                         ? (EngineId)gs.engineId : ENGINE_CS80;
    engineSwitchTo(bootEngine);
    g_voiceLimit = engineNumVoices();
    if (haveSettings) {
        for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].setBPM(gs.bpm);
        engineRestoreParams(gs.params);
        g_currentPreset = gs.currentPreset;
        activeTrack = gs.lastTrack % NUM_TRACKS;
    } else {
        engineLoadPreset(0);
        g_currentPreset = 0;
        for (uint8_t t = 0; t < NUM_TRACKS; t++)
            for (uint8_t i = 0; i < CHAIN_LEN; i++) gs.chain[t][i].patternIdx = -1;
    }

    for (uint8_t t = 0; t < NUM_TRACKS; t++) {
        bool loadedAny = false;
        for (uint8_t p = 0; p < NUM_PATTERNS; p++) {
            Pattern tmp;
            if (storage.loadPattern(t, p, tmp)) { seqs[t].getPattern(p) = tmp; loadedAny = true; }
        }
        if (!loadedAny) initDemoPattern(seqs[t], t);
        if (haveSettings && gs.lastPattern[t] < NUM_PATTERNS) seqs[t].setPattern(gs.lastPattern[t]);
        seqs[t].setMuted(gs.trackMute[t] != 0);
    }

    ui.bootStatus("MIDI GP1");
    midi.begin();
    midi.setInChannel(gs.midiInChannel);
    midi.setOutEnabled(gs.midiOutEnable != 0);
    midi.setNoteOn(onMidiNoteOn);
    midi.setNoteOff(onMidiNoteOff);
    midi.setCC(onMidiCC);
    midi.setPC(onMidiPC);
    midi.setBend(onMidiBend);
    midi.setRealtime(onMidiRealtime);
    midi.setPolyAT(onMidiPolyAT);
    midi.setChanAT(onMidiChanAT);
    uiApplySync();

    controls.begin();
    ui.bind(seqs, &activeTrack, &storage, &gs);
    ui.applyContrast(gs.contrast);

    // Export the filesystem as a USB stick (SETTINGS -> USB DRIVE) -- drop
    // DX7 .syx cartridges into /banks and they show up on the BANK row after
    // ejecting. Only worth registering if the filesystem actually mounted:
    // FatFSUSB reports the drive's capacity straight from the FS, so with no
    // FS partition reserved (Tools -> Flash Size) it would advertise a
    // 0-byte disk, which hosts silently ignore. Not exported yet -- callbacks
    // are registered up front so entering USB DRIVE is just a begin(); see
    // uiUsbBegin()/uiUsbEnd() above and ui.cpp's SETTINGS handling.
    if (fsOk) {
        FatFSUSB.onPlug(usbPlug);
        FatFSUSB.onUnplug(usbUnplug);
        FatFSUSB.driveReady(usbDriveReady);
    } else {
        Serial.println("[Carpenter Synth] no filesystem - USB DRIVE unavailable. "
                       "Pick a Flash Size with an FS partition in Tools.");
    }

    // ── 4. Release the audio core ──────────────────────────────────────────
    // Everything core1 touches now exists, so setup1() can stop spinning.
    ui.bootStatus("AUDIO");
    g_audioReady = true;

    ui.bootStatus(fsOk ? "READY" : "READY (NO FS)");
    delay(900);                 // let the splash sit for a beat before the UI takes over
    ui.markDirty();
    Serial.printf("[Carpenter Synth] ready - engine %s, %d presets, %d riffs, MIDI OUT on GP%d, FS %s\n",
                  ENGINE_NAMES[currentEngine], engineNumPresets(), (int)NUM_RIFFS, PIN_MIDI_TX, fsOk ? "ok" : "absent");
}

// Metronome: a 6 ms gate pulse on GP14 at the top of each beat.  Cheap, silent
// in the audio path, and drives an LED or a click track input directly.
static void metronomeTick() {
    static unsigned long pulseUntil = 0;
    static uint8_t lastStep = 255;
    if (!gs.metronome) { digitalWrite(PIN_CLICK_OUT, LOW); return; }
    Sequencer& s = seqs[INTERNAL_TRACK];
    if (s.playState() == PlayState::PLAYING) {
        uint8_t st = s.currentStep();
        if (st != lastStep) {
            lastStep = st;
            if ((st & 3) == 0) { digitalWrite(PIN_CLICK_OUT, HIGH); pulseUntil = millis() + 6; }
        }
    }
    if (pulseUntil && millis() > pulseUntil) { digitalWrite(PIN_CLICK_OUT, LOW); pulseUntil = 0; }
}

void loop() {
    // Timing first, and MIDI in around anything that can block.
    midi.poll();
    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].update();
    arpProcess();

    // ── USB disk mode ────────────────────────────────────────────────────────
    // Most OSes auto-mount a new USB drive the instant it enumerates -- no
    // user action required -- so usbPlug() can fire seconds after boot with
    // the cable just providing power/Serial, not because anyone chose to
    // browse files. The filesystem itself is already protected at every
    // access point (storage.cpp / voicebank.cpp both check g_diskMode before
    // touching FatFS), so nothing here needs the sequencer to stop or the
    // screen to be taken over for an incidental mount -- only the deliberate,
    // on-screen SETTINGS -> USB DRIVE path does that (uiUsbBegin() above).
    //
    // Deferred mount/unmount requested by the USB callbacks (which run in IRQ
    // context and only set flags). Both end up erasing flash, so park the
    // audio core around them exactly like a save.
    if (g_fsUnmountReq) {
        g_fsUnmountReq = false;
        audioFlashBegin();
        FatFS.end();
        audioFlashEnd();
    }
    if (g_fsRemountReq) {
        g_fsRemountReq = false;
        audioFlashBegin();
        FatFS.begin();
        audioFlashEnd();
        g_diskMode      = false;
        g_dx7BanksDirty = true;       // rescan /banks now that it is ours again
    }
    static bool wasDiskMode = false;
    if (g_diskMode != wasDiskMode) {
        wasDiskMode = g_diskMode;
        ui.markDirty();               // SETTINGS row / USB DRIVE screen reflects it
    }
    if (g_dx7BanksDirty && !g_diskMode) {
        g_dx7BanksDirty = false;
        vbRescan();                   // pick up newly copied banks
        dxRefreshCurrentBank();       // re-resolve the live voice under the new numbering
        ui.markDirty();
    }

    controls.update();
    ui.setShift(controls.shift.isHeld());

    int d = controls.encoder.getDelta();
    if (d != 0) ui.onEncoder(d);
    if (controls.encoder.wasPressed())   ui.onEncPress();
    if (controls.encoder.wasLongPress()) ui.onEncLong();

    if (controls.play.wasPressed())     ui.onPlay();
    if (controls.play.wasLongPress())   ui.onPlayLong();
    if (controls.page.wasPressed())     ui.onPage();
    if (controls.page.wasLongPress())   ui.onPageLong();
    if (controls.track.wasPressed())    ui.onTrack();
    if (controls.track.wasLongPress())  ui.onTrackLong();
    if (controls.mute.wasPressed())     ui.onMute();
    if (controls.mute.wasLongPress())   ui.onMuteLong();
    controls.shift.wasPressed();        // consumed; SHIFT is a hold, not a press

    // ── Performance switches: octave transpose (external MIDI keyboard) ────
    if (controls.octDown.wasPressed() && g_octaveShift > -3) {
        g_octaveShift--;
        char b[12]; snprintf(b, sizeof(b), "OCT %+d", (int)g_octaveShift);
        ui.toast(b);
    }
    if (controls.octUp.wasPressed() && g_octaveShift < 3) {
        g_octaveShift++;
        char b[12]; snprintf(b, sizeof(b), "OCT %+d", (int)g_octaveShift);
        ui.toast(b);
    }

    // ── Performance pots: filter cutoff/resonance, always live, mapped
    // through whichever engine is currently selected (engine.h) ────────────
    // ENGINE_PARAM_NONE means the live engine has no such control (the DX7,
    // which has no filter at all) -- the knob is inert there rather than
    // writing whatever parameter happens to sit at id 0.
    if (controls.cutoffPot.changed()) {
        uint8_t id = engineFilterCutoffParamId();
        if (id != ENGINE_PARAM_NONE) { engineSetParameter(id, controls.cutoffPot.value()); ui.markDirty(); }
    }
    if (controls.resonancePot.changed()) {
        uint8_t id = engineFilterResonanceParamId();
        if (id != ENGINE_PARAM_NONE) { engineSetParameter(id, controls.resonancePot.value()); ui.markDirty(); }
    }

    if (g_previewOffAt && millis() > g_previewOffAt) {
        uiPreviewNote(0, false);
        g_previewOffAt = 0;
    }

    metronomeTick();

    midi.poll();      // drain before the blocking display transfer
    ui.update();
    midi.poll();      // ...and catch up straight after it
}

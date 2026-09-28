#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
//  voicequeue.h — lock-free note pipe, core0 -> core1.
//
//  Single producer (core0 allocates voices), single consumer (core1 applies
//  them at a block boundary).  Power-of-two capacity so head and tail wrap by
//  bit-mask instead of a modulo, and a full memory fence on each side so the
//  payload is visible before the index that publishes it (the xtro build's
//  ARM __dmb(); on the S3's Xtensa cores this fence compiles to MEMW).
//
//  This lives in a header rather than the .ino because the Arduino build
//  inserts generated prototypes directly after the includes — a function taking
//  a type declared later in the sketch would be prototyped before that type
//  exists.
// ============================================================================

#define VQ_SIZE 64
#define VQ_MASK (VQ_SIZE - 1)

enum VCmd : uint8_t { VC_NOTE_ON, VC_NOTE_OFF, VC_ALL_OFF };

struct VMsg {
    uint8_t cmd, voice, note, vel;
    float   freq;
    bool    trigEnv;
};

extern VMsg vq[VQ_SIZE];
extern volatile uint32_t vqHead, vqTail;

inline bool vqPush(uint8_t cmd, uint8_t v, uint8_t note, uint8_t vel, float freq, bool trig) {
    uint32_t h = vqHead;
    uint32_t next = (h + 1) & VQ_MASK;
    if (next == vqTail) return false;      // full — drop rather than block core0
    vq[h].cmd = cmd; vq[h].voice = v; vq[h].note = note;
    vq[h].vel = vel; vq[h].freq = freq; vq[h].trigEnv = trig;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    vqHead = next;
    return true;
}

inline bool vqPop(VMsg* out) {
    uint32_t t = vqTail;
    if (t == vqHead) return false;
    *out = vq[t];
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    vqTail = (t + 1) & VQ_MASK;
    return true;
}

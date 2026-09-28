#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
//  voicealloc.h — core0-side voice bookkeeping.
//
//  Split out of the sketch so it can be exercised natively: this is where the
//  stuck-note bugs lived, and "note still ringing" is not something you want to
//  be debugging by ear on hardware.
//
//  Two rules make stranded notes impossible:
//
//    1. A note-on for a pitch that is ALREADY sounding reuses that same voice.
//       Without this, a step whose gate overlaps the next one (gate > 100 %, a
//       tie, or a ratchet) allocates a second voice for the same pitch, and the
//       single note-off that follows releases only one of them.  The other
//       holds at its sustain level forever.
//
//    2. A note-off releases EVERY voice holding that pitch, not the first one
//       found.  Belt and braces for rule 1, and it also cleans up after a
//       unison patch or a preset change mid-note.
// ============================================================================

struct VoiceAlloc {
    int      note[MAX_VOICES];
    bool     gate[MAX_VOICES];        // key still held
    uint32_t start[MAX_VOICES];       // allocation age
    uint8_t  busy[MAX_VOICES];        // published by core1: amp env != IDLE
    uint32_t tick;
    uint8_t  limit;                   // allocation ceiling (CPU guard)

    void reset() {
        for (int i = 0; i < MAX_VOICES; i++) {
            note[i] = -1; gate[i] = false; start[i] = 0; busy[i] = 0;
        }
        tick = 0;
        limit = MAX_VOICES;
    }

    // The voice currently holding `n`, or -1.
    int findSounding(int n) const {
        for (int i = 0; i < MAX_VOICES; i++) if (gate[i] && note[i] == n) return i;
        return -1;
    }

    int steal() {
        uint8_t lim = limit ? limit : 1;
        // 1: a voice that has finished releasing
        for (int i = 0; i < lim; i++) if (!gate[i] && !busy[i]) return i;
        // 2: the oldest voice still in its release tail
        int best = -1; uint32_t oldest = 0xFFFFFFFFu;
        for (int i = 0; i < lim; i++)
            if (!gate[i] && start[i] < oldest) { oldest = start[i]; best = i; }
        if (best >= 0) return best;
        // 3: the oldest sounding voice
        best = 0; oldest = start[0];
        for (int i = 1; i < lim; i++) if (start[i] < oldest) { oldest = start[i]; best = i; }
        return best;
    }

    // Rule 1.  Returns the voice index to drive.
    int noteOn(int n) {
        int v = findSounding(n);
        if (v < 0) v = steal();
        note[v] = n; gate[v] = true; start[v] = ++tick;
        return v;
    }

    // Rule 2.  Fills `out` with every voice holding `n`; returns how many.
    int noteOff(int n, int* out) {
        int c = 0;
        for (int i = 0; i < MAX_VOICES; i++) {
            if (gate[i] && note[i] == n) { gate[i] = false; out[c++] = i; }
        }
        return c;
    }

    void allOff() {
        for (int i = 0; i < MAX_VOICES; i++) gate[i] = false;
    }
};

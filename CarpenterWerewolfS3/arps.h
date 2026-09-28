#pragma once
#include <Arduino.h>
#include "config.h"

// ============================================================================
//  arps.h — arpeggiator interval patterns and the sequencer riff library.
//
//  Both tables are `const` and therefore live in flash, which is exactly right:
//  they are read at step rate (tens of hertz), never on the audio path, and on
//  a 16 MB part there is no reason to spend SRAM on them.
// ============================================================================

// ─── Arpeggiator patterns (semitone offsets from the lowest held note) ──────
#define ARP_NUM_PATTERNS 50
extern const int8_t ARP_PATTERNS[ARP_NUM_PATTERNS][16];

// ─── Riff library ───────────────────────────────────────────────────────────
// Ready-made 16-step parts you can drop into any pattern and then edit.  Notes
// are semitone offsets from the pattern root; -1 means "rest".  `vel` carries
// the accent shape, which is most of what makes a riff feel like a riff.
//
// `cat` is browsing, not a musical constraint — the TOOLS page filters the
// riff list by it (SHIFT+turn cycles the filter, turn cycles riffs within
// it) so 200 riffs is still fast to page through instead of one long list.
enum RiffCategory : uint8_t {
    RIFF_CAT_ALL = 0,     // filter value only — no riff is tagged ALL
    RIFF_CAT_CLASSIC,     // the original 24: house, techno, trance, acid...
    RIFF_CAT_RETROWAVE,   // synthwave/outrun: octave pulses, arpeggiated 5ths
    RIFF_CAT_GRINDHOUSE,  // grindhouse/exploitation score: sleazy chromatic crawls, stabs
    RIFF_CAT_SEVENTIES,   // 70s: funk/disco basslines, syncopated octaves
    RIFF_CAT_EIGHTIES,    // 80s: gated synth-pop and action-movie riffs
    RIFF_CAT_COUNT
};
extern const char* const RIFF_CATEGORY_NAMES[RIFF_CAT_COUNT];

struct Riff {
    const char* name;
    int8_t      note[16];
    uint8_t     vel[16];
    uint8_t     gate;
    uint8_t     cat;      // one of RIFF_CAT_* (never RIFF_CAT_ALL)
};
extern const Riff RIFFS[];
extern const uint8_t NUM_RIFFS;

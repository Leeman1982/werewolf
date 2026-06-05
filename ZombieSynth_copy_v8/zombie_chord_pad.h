#ifndef ZOMBIE_CHORD_PAD_H
#define ZOMBIE_CHORD_PAD_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "zombie_synth_mode.h"

// ZOMBIE SS Chord Pad – Bonus Feature
// Play chords with one touch. Choose root + type, instant polyphonic chords.

// ── Chord type definitions ──────────────────────────────────────────────────
struct ChordType {
  const char* name;
  int intervals[5];   // semitone offsets from root
  int numNotes;
};

static const ChordType chordTypes[] = {
  {"MAJ",  {0, 4,  7,  -1, -1}, 3},
  {"MIN",  {0, 3,  7,  -1, -1}, 3},
  {"7",    {0, 4,  7,  10, -1}, 4},
  {"MAJ7", {0, 4,  7,  11, -1}, 4},
  {"MIN7", {0, 3,  7,  10, -1}, 4},
  {"DIM",  {0, 3,  6,  -1, -1}, 3},
  {"AUG",  {0, 4,  8,  -1, -1}, 3},
  {"SUS4", {0, 5,  7,  -1, -1}, 3},
};
static const int NUM_CHORD_TYPES = 8;

// ── Root notes ───────────────────────────────────────────────────────────────
static const char* rootNoteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
static const int   NUM_ROOTS = 12;

// ── State ────────────────────────────────────────────────────────────────────
static int  chordRootNote    = 0;    // 0=C .. 11=B
static int  chordTypeIdx     = 0;    // 0=MAJ
static int  chordOctave      = 4;    // middle C octave
static bool chordHoldMode    = false;
static bool chordNeedsRedraw = true;
static int  chordActiveNotes[5];
static int  chordActiveCount = 0;

// ── Note management ──────────────────────────────────────────────────────────
static void chordAllOff() {
  SynthEngine* synth = getZombieSynth();
  if (!synth) return;
  for (int i = 0; i < chordActiveCount; i++) {
    if (chordActiveNotes[i] >= 0) synth->noteOff(chordActiveNotes[i]);
  }
  chordActiveCount = 0;
}

static void chordPlay(int root, int typeIdx) {
  SynthEngine* synth = getZombieSynth();
  if (!synth) return;

  if (!chordHoldMode) chordAllOff();

  const ChordType& ct = chordTypes[typeIdx];
  int baseNote = (chordOctave + 1) * 12 + root;

  chordActiveCount = 0;
  for (int i = 0; i < ct.numNotes; i++) {
    int note = baseNote + ct.intervals[i];
    if (note >= 0 && note <= 127) {
      synth->noteOn(note, 100);
      chordActiveNotes[chordActiveCount++] = note;
      extern int lastPlayedMidiNote;
      lastPlayedMidiNote = note;
    }
  }
}

// ── Draw ─────────────────────────────────────────────────────────────────────
void zombieChordDraw() {
  if (!chordNeedsRedraw) return;

  // ── Header ────────────────────────────────────────────────────────────
  drawZombiHeader("CHORD PAD");

  // Current chord label (top-right)
  char cname[16];
  snprintf(cname, sizeof(cname), "%s %s", rootNoteNames[chordRootNote], chordTypes[chordTypeIdx].name);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawRightString(cname, 315, 8, 4);

  // ── Chord type selector (y=53..77, 8 buttons across) ─────────────────
  tft.fillRect(0, 52, 320, 27, THEME_BG);
  for (int i = 0; i < NUM_CHORD_TYPES; i++) {
    int x = 5 + i * 38;
    bool sel = (i == chordTypeIdx);
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(x, 53, 35, 24, 3, bg);
    tft.drawRoundRect(x, 53, 35, 24, 3, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(chordTypes[i].name, x + 17, 58, 2);
  }

  // ── Octave controls (y=80..100) ───────────────────────────────────────
  tft.fillRect(0, 80, 320, 22, THEME_BG);
  tft.fillRoundRect(5, 80, 45, 20, 3, THEME_BG);
  tft.drawRoundRect(5, 80, 45, 20, 3, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("OCT-", 27, 85, 2);

  char octBuf[8];
  sprintf(octBuf, "OCT %d", chordOctave);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(octBuf, 160, 85, 2);

  tft.fillRoundRect(270, 80, 45, 20, 3, THEME_BG);
  tft.drawRoundRect(270, 80, 45, 20, 3, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("OCT+", 292, 85, 2);

  // HOLD toggle
  bool hl = chordHoldMode;
  tft.fillRoundRect(115, 80, 90, 20, 3, hl ? THEME_PRIMARY : THEME_BG);
  tft.drawRoundRect(115, 80, 90, 20, 3, THEME_OUTLINE);
  tft.setTextColor(hl ? THEME_BG : THEME_PRIMARY, hl ? THEME_PRIMARY : THEME_BG);
  tft.drawCentreString(hl ? "HOLD: ON" : "HOLD: OFF", 160, 85, 2);

  // ── Root note grid (3 rows × 4 cols) y=106..200 ───────────────────────
  tft.fillRect(0, 104, 320, 100, THEME_BG);
  // Chromatic layout: natural notes first row (C D E F), second row (G A B -),
  // sharps/flats on third row (C# D# F# G# A#)
  // Simpler: 3 rows × 4 notes linear layout
  // Row order: C C# D D# | E F F# G | G# A A# B
  for (int i = 0; i < NUM_ROOTS; i++) {
    int row = i / 4;
    int col = i % 4;
    int x = 5 + col * 78;
    int y = 106 + row * 32;
    int w = 75, h = 29;

    bool sel = (i == chordRootNote);
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;

    // Sharp notes get THEME_SURFACE bg when not selected
    bool isSharp = (i==1||i==3||i==6||i==8||i==10);
    if (!sel && isSharp) bg = THEME_SURFACE;

    tft.fillRoundRect(x, y, w, h, 5, bg);
    tft.drawRoundRect(x, y, w, h, 5, THEME_OUTLINE);
    tft.setTextColor(txt, bg);

    // Show root name + chord type name
    char lbl[10];
    snprintf(lbl, sizeof(lbl), "%s %s", rootNoteNames[i], chordTypes[chordTypeIdx].name);
    tft.drawCentreString(lbl, x + w/2, y + h/2 - 7, 2);
  }

  // ── STRUM ALL button (play all 12 chords scrolling) ──────────────────
  tft.fillRect(0, 206, 320, 34, THEME_BG);
  tft.fillRoundRect(5,  207, 148, 30, 4, THEME_BG);
  tft.drawRoundRect(5,  207, 148, 30, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("ALL NOTES OFF", 79, 215, 2);

  tft.fillRoundRect(167, 207, 148, 30, 4, THEME_PRIMARY);
  tft.drawRoundRect(167, 207, 148, 30, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_BG, THEME_PRIMARY);
  tft.drawCentreString("STRUM SCALE", 241, 215, 2);

  chordNeedsRedraw = false;
}

// ── Touch handler ─────────────────────────────────────────────────────────
void zombieChordHandleTouch() {
  if (!touch.justPressed) return;

  // BACK
  if (isButtonPressed(5, 5, 55, 20)) { chordAllOff(); exitToMenu(); return; }

  // Chord type selector
  for (int i = 0; i < NUM_CHORD_TYPES; i++) {
    if (isButtonPressed(5 + i*38, 53, 35, 24)) {
      chordTypeIdx = i;
      chordNeedsRedraw = true;
      return;
    }
  }

  // Octave controls
  if (isButtonPressed(5, 80, 45, 20))   { chordOctave = constrain(chordOctave - 1, 2, 7); chordNeedsRedraw = true; return; }
  if (isButtonPressed(270, 80, 45, 20)) { chordOctave = constrain(chordOctave + 1, 2, 7); chordNeedsRedraw = true; return; }

  // HOLD toggle
  if (isButtonPressed(115, 80, 90, 20)) {
    chordHoldMode = !chordHoldMode;
    if (!chordHoldMode) chordAllOff();
    chordNeedsRedraw = true;
    return;
  }

  // Root note grid (3 rows × 4 cols)
  for (int i = 0; i < NUM_ROOTS; i++) {
    int row = i / 4;
    int col = i % 4;
    int x = 5 + col * 78;
    int y = 106 + row * 32;
    if (isButtonPressed(x, y, 75, 29)) {
      chordRootNote = i;
      chordPlay(chordRootNote, chordTypeIdx);
      chordNeedsRedraw = true;
      return;
    }
  }

  // ALL NOTES OFF
  if (isButtonPressed(5, 207, 148, 30)) {
    chordAllOff();
    chordNeedsRedraw = true;
    return;
  }

  // STRUM SCALE – play one note from each degree of the scale in sequence
  if (isButtonPressed(167, 207, 148, 30)) {
    SynthEngine* synth = getZombieSynth();
    if (synth) {
      chordAllOff();
      // Play root chord notes as arpeggiated burst (fire all at once, fun zombie strum)
      const ChordType& ct = chordTypes[chordTypeIdx];
      int baseNote = (chordOctave + 1) * 12 + chordRootNote;
      for (int i = 0; i < ct.numNotes; i++) {
        int note = baseNote + ct.intervals[i];
        if (note >= 0 && note <= 127) synth->noteOn(note, 90);
        delay(30);
      }
      // Also play the chord an octave up
      for (int i = 0; i < ct.numNotes; i++) {
        int note = baseNote + 12 + ct.intervals[i];
        if (note >= 0 && note <= 127) synth->noteOn(note, 70);
        delay(20);
      }
    }
    chordNeedsRedraw = true;
    return;
  }
}

void zombieChordInit() {
  chordRootNote    = 0;
  chordTypeIdx     = 0;
  chordOctave      = 4;
  chordHoldMode    = false;
  chordNeedsRedraw = true;
  chordActiveCount = 0;
  for (int i = 0; i < 5; i++) chordActiveNotes[i] = -1;
  tft.fillScreen(THEME_BG);
}

void zombieChordUpdate() {
  // No continuous processing needed
}

#endif

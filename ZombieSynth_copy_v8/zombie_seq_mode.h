#ifndef ZOMBIE_SEQ_MODE_H
#define ZOMBIE_SEQ_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "zombie_step_sequencer.h"
#include "zombie_synth_mode.h"
#include "zombie_presets.h"
#include "arpeggiator_patterns.h"

// ── Screen layout ─────────────────────────────────────────────────────────────
// y=0..49   : Header (BACK + title)
// y=50..71  : Tab bar  (GRID | EDIT | TRCK | PTRN)
// y=72..199 : Content  (128 px)
// y=200..239: Bottom   (PLAY | BPM | TAP | FILL)

// ── State ─────────────────────────────────────────────────────────────────────
static ZombieSequencer* zombieSeq    = NULL;
static int  seqTab         = 0;   // 0=GRID 1=EDIT 2=TRCK 3=PTRN 4=SYNTH 5=SONG
static int  seqEditTrack   = 0;
static int  seqEditStep    = 0;
static int  seqGridPage    = 0;   // 0=steps 0-15, 1=steps 16-31
static bool seqNeedsRedraw = true;
static int  seqLastStep    = -1;
static bool seqLastPlaying = false;
static int  seqCopyPat     = -1;  // -1 = nothing copied
static int  seqEuclK       = 4;
static int  seqEuclN       = 16;
static int  seqArpPatIdx      = 0;   // arp pattern for ARP→SEQ fill
static int  seqSynthSubTab    = 0;   // SYNTH sub-page: 0=OSC 1=FLT 2=SUB 3=AMP 4=ENV 5=PRST
static int  seqSynthPresetIdx = 0;   // preset selector in SYNTH→PRST sub-tab
static int  seqTrackSubTab    = 0;   // TRCK sub-page: 0=SOUND 1=MIX
static int  seqEditSubTab     = 0;   // EDIT sub-page: 0=STEP 1=PLOCK
static int  seqSongSlotIdx    = 0;   // selected slot in SONG tab editor
static int  seqSongSaveSlot   = 0;   // currently loaded/saved song slot (0..7)
static int  seqPtrnSubTab     = 0;   // PTRN sub-page: 0=BANK 1=TOOLS

// ── Two-tap confirm for destructive actions ──────────────────────────────────
// First tap arms (button shows "SURE?"), a second tap of the SAME control within
// the timeout commits; any timeout cancels.
static int           seqArmedAction = 0;     // 0 = none; else an action id below
static unsigned long seqArmedMs     = 0;
static const unsigned long SEQ_ARM_TIMEOUT = 3000;
#define SEQ_ARM_LOAD    1
#define SEQ_ARM_CLRPAT  2

// Returns true when the action is confirmed (this is the 2nd tap in time).
static bool seqConfirm(int actionId) {
  unsigned long now = millis();
  if (seqArmedAction == actionId && (now - seqArmedMs) < SEQ_ARM_TIMEOUT) {
    seqArmedAction = 0;
    return true;
  }
  seqArmedAction = actionId;
  seqArmedMs     = now;
  return false;
}
static inline bool seqArmed(int actionId) {
  return seqArmedAction == actionId &&
         (millis() - seqArmedMs) < SEQ_ARM_TIMEOUT;
}

// ── Note-on / note-off callbacks ──────────────────────────────────────────────
// Each track fires into its own private voice window with its own patch, so
// tracks stay fully independent (no cross-track voice stealing or timbre
// bleed).  pan already folds in the per-step panLock; cutoffMod is the per-step
// cutoff P-Lock — both are now honoured.
void seqNoteOnHandler(int trackIdx, int noteNum, int vel, float pan, float cutoffMod) {
  SynthEngine* synth = getZombieSynth();
  if (!synth) return;
  SequencerTrack* t = zombieSeq ? zombieSeq->getTrack(trackIdx) : NULL;
  if (t) synth->noteOnTrack(trackIdx, noteNum, vel, pan, cutoffMod, t->trackPatch);
  else   synth->noteOn(noteNum, vel);
}
void seqNoteOffHandler(int trackIdx, int noteNum) {
  SynthEngine* synth = getZombieSynth();
  if (synth) synth->noteOffTrack(trackIdx, noteNum);
}

// ── Drawing helpers ───────────────────────────────────────────────────────────
static void seqStepCell(int x, int y, int w, int h,
                        bool active, bool playing, bool selected) {
  uint16_t bg  = THEME_BG;
  uint16_t brd = THEME_OUTLINE;
  if      (playing && active)  { bg = THEME_ACCENT;   brd = THEME_ACCENT;   }
  else if (playing)            { bg = THEME_BG;        brd = THEME_ACCENT;   }
  else if (selected && active) { bg = THEME_PRIMARY;   brd = THEME_ACCENT;   }
  else if (selected)           { bg = THEME_SECONDARY; brd = THEME_ACCENT;   }
  else if (active)             { bg = THEME_PRIMARY;   brd = THEME_OUTLINE;  }
  tft.fillRoundRect(x, y, w, h, 3, bg);
  tft.drawRoundRect(x, y, w, h, 3, brd);
}

// ── Header ────────────────────────────────────────────────────────────────────
static void seqDrawHeader() {
  drawZombiHeader("THE WEREWOLF STEP SEQUENCER");
}

// ── Tab bar (6 tabs × 53 px) ──────────────────────────────────────────────────
#define SEQ_NUM_TABS 6
#define SEQ_TAB_W    53
static const char* SEQ_TAB_NAMES[SEQ_NUM_TABS] =
  {"GRID","EDIT","TRCK","PTRN","SYNTH","SONG"};

static void seqDrawTabs() {
  // Fill any leftover pixel at the right edge (6 × 53 = 318, screen 320)
  tft.fillRect(SEQ_NUM_TABS*SEQ_TAB_W, 50, 320-SEQ_NUM_TABS*SEQ_TAB_W, 22, THEME_BG);
  for (int i = 0; i < SEQ_NUM_TABS; i++) {
    bool sel = (i == seqTab);
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG     : THEME_PRIMARY;
    tft.fillRect(i*SEQ_TAB_W, 50, SEQ_TAB_W, 22, bg);
    tft.drawRect(i*SEQ_TAB_W, 50, SEQ_TAB_W, 22, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(SEQ_TAB_NAMES[i], i*SEQ_TAB_W+SEQ_TAB_W/2, 55, 2);
  }
}

// ── Bottom bar (always visible) ───────────────────────────────────────────────
static void seqDrawBottom() {
  bool playing = zombieSeq && zombieSeq->getIsPlaying();
  bool fill    = zombieSeq && zombieSeq->getFillMode();

  tft.fillRect(0, 200, 320, 40, THEME_BG);

  // PLAY/STOP (x=0, w=90)
  uint16_t pbg = playing ? THEME_PRIMARY : THEME_BG;
  tft.fillRoundRect(1, 201, 88, 37, 4, pbg);
  tft.drawRoundRect(1, 201, 88, 37, 4, THEME_OUTLINE);
  tft.setTextColor(playing ? THEME_BG : THEME_PRIMARY, pbg);
  tft.drawCentreString(playing ? "STOP" : "PLAY", 45, 211, 2);

  // BPM- (x=90, w=44)
  tft.fillRoundRect(90, 201, 43, 37, 4, THEME_BG);
  tft.drawRoundRect(90, 201, 43, 37, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("BPM-", 111, 211, 2);

  // BPM value (x=134, w=52) – display only
  char bpmStr[8];
  snprintf(bpmStr, sizeof(bpmStr), "%.0f", zombieSeq ? zombieSeq->getBPM() : 120.0f);
  tft.fillRect(134, 201, 52, 37, THEME_BG);
  tft.drawRect(134, 201, 52, 37, THEME_OUTLINE);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(bpmStr, 160, 211, 2);

  // BPM+ (x=186, w=44)
  tft.fillRoundRect(186, 201, 43, 37, 4, THEME_BG);
  tft.drawRoundRect(186, 201, 43, 37, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("BPM+", 207, 211, 2);

  // TAP (x=230, w=44)
  tft.fillRoundRect(230, 201, 43, 37, 4, THEME_BG);
  tft.drawRoundRect(230, 201, 43, 37, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("TAP", 251, 204, 2);
  tft.drawCentreString("TEMPO", 251, 219, 2);

  // FILL (x=274, w=45)
  uint16_t fbg = fill ? THEME_WARNING : THEME_BG;
  tft.fillRoundRect(274, 201, 45, 37, 4, fbg);
  tft.drawRoundRect(274, 201, 45, 37, 4, THEME_OUTLINE);
  tft.setTextColor(fill ? THEME_BG : THEME_PRIMARY, fbg);
  tft.drawCentreString("FILL", 296, 211, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// GRID TAB
// ─────────────────────────────────────────────────────────────────────────────
static void seqDrawGrid() {
  int  curStep = zombieSeq->getTrackStep(seqEditTrack);
  bool playing = zombieSeq->getIsPlaying();

  // Track row (y=72..93)
  if (seqNeedsRedraw) {
    tft.fillRect(0, 72, 320, 22, THEME_BG);
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
      SequencerTrack* tr = zombieSeq->getTrack(t);
      bool sel    = (t == seqEditTrack);
      bool muted  = tr && tr->muted;
      uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
      uint16_t txt = sel ? THEME_BG : (muted ? THEME_TEXT_DIM : THEME_PRIMARY);
      tft.fillRoundRect(1+t*79, 73, 78, 20, 3, bg);
      tft.drawRoundRect(1+t*79, 73, 78, 20, 3, muted ? THEME_TEXT_DIM : THEME_OUTLINE);
      tft.setTextColor(txt, bg);
      char tbuf[12];
      snprintf(tbuf, sizeof(tbuf), "T%d%s%.3s", t+1, muted?"[M]":" ",
               tr ? tr->trackPatch.name : "---");
      tft.drawString(tbuf, 4+t*79, 77, 2);
    }
  }

  // Step grid (y=95..155) – 2 rows × 8 cells each
  tft.fillRect(0, 95, 320, 61, THEME_BG);
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (tr) {
    for (int s = 0; s < 16; s++) {
      int stepIdx  = seqGridPage * 16 + s;
      int cx       = 3 + (s % 8) * 39;
      int cy       = (s < 8) ? 96 : 126;
      bool active  = (stepIdx < MAX_SEQ_STEPS) && tr->steps[stepIdx].active;
      bool playing_ = playing && (stepIdx == curStep);
      bool sel     = (stepIdx == seqEditStep);
      seqStepCell(cx, cy, 37, 28, active, playing_, sel);
      // Step number
      uint16_t nc = (active && !playing_) ? THEME_BG :
                    (playing_)            ? THEME_BG : THEME_TEXT_DIM;
      tft.setTextColor(nc, active ? (playing_ ? THEME_ACCENT : THEME_PRIMARY)
                                  : (playing_ ? THEME_BG     : THEME_BG));
      char num[4]; snprintf(num, sizeof(num), "%d", stepIdx+1);
      tft.drawCentreString(num, cx+18, cy+8, 2);
      // Note name on active steps
      if (active && stepIdx < MAX_SEQ_STEPS) {
        int nn = tr->steps[stepIdx].note % 12;
        tft.setTextColor(playing_ ? THEME_BG : THEME_BG,
                         playing_ ? THEME_ACCENT : THEME_PRIMARY);
        tft.drawCentreString(SEQ_NOTE_NAMES[nn], cx+18, cy+17, 2);
      }
    }
  }

  // Page selector + current step indicator (y=158..172)
  if (seqNeedsRedraw) {
    tft.fillRect(0, 157, 320, 16, THEME_BG);
    // Page A
    uint16_t abg = (seqGridPage==0) ? THEME_PRIMARY : THEME_BG;
    tft.fillRoundRect(3, 158, 38, 14, 3, abg);
    tft.drawRoundRect(3, 158, 38, 14, 3, THEME_OUTLINE);
    tft.setTextColor((seqGridPage==0)?THEME_BG:THEME_PRIMARY, abg);
    tft.drawCentreString("1-16", 22, 160, 2);
    // Page B (only if track length > 16)
    uint16_t bbg = (seqGridPage==1) ? THEME_PRIMARY : THEME_BG;
    bool hasB = tr && tr->length > 16;
    tft.fillRoundRect(44, 158, 38, 14, 3, hasB ? bbg : THEME_SURFACE);
    tft.drawRoundRect(44, 158, 38, 14, 3, hasB ? THEME_OUTLINE : THEME_TEXT_DIM);
    tft.setTextColor(hasB ? ((seqGridPage==1)?THEME_BG:THEME_PRIMARY) : THEME_TEXT_DIM,
                     hasB ? bbg : THEME_SURFACE);
    tft.drawCentreString("17-32", 63, 160, 2);

    // Step info
    if (tr && seqEditStep < MAX_SEQ_STEPS) {
      SequencerStep& st = tr->steps[seqEditStep];
      char info[40];
      int nn = st.note % 12;
      int no = st.note / 12 - 1;
      snprintf(info, sizeof(info), "S%d: %s%d V%d G%d P%d%%",
               seqEditStep+1, SEQ_NOTE_NAMES[nn], no,
               st.velocity, st.gate, (int)st.probability);
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(info, 90, 160, 2);
    }
  }

  // Track length bar indicator (y=174..183)
  if (seqNeedsRedraw) {
    tft.fillRect(0, 174, 320, 24, THEME_BG);
    int tlen = tr ? tr->length : 16;
    char lenBuf[20];
    snprintf(lenBuf, sizeof(lenBuf), "LEN:%d  DIV:%s",
             tlen, tr ? SEQ_DIV_NAMES[tr->divIdx] : "1/16");
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(lenBuf, 3, 176, 2);
    // Mute indicator for active track
    if (tr && tr->muted) {
      tft.setTextColor(THEME_WARNING, THEME_BG);
      tft.drawRightString("MUTED", 316, 176, 2);
    } else if (zombieSeq->fillMode) {
      tft.setTextColor(THEME_WARNING, THEME_BG);
      tft.drawRightString("FILL", 316, 176, 2);
    }
    // Pattern indicator
    char patBuf[12];
    snprintf(patBuf, sizeof(patBuf), "PAT:%d", zombieSeq->activePattern+1);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawRightString(patBuf, 316, 188, 2);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    char stepBuf[12];
    snprintf(stepBuf, sizeof(stepBuf), "STEP:%02d", curStep+1);
    tft.drawString(stepBuf, 3, 188, 2);

    // REC arm button + CLOCK mode indicator (centre of the info row)
    bool rec = zombieSeq->getRecordArmed();
    uint16_t rbg = rec ? THEME_ERROR : THEME_BG;
    tft.fillRoundRect(118, 184, 50, 14, 3, rbg);
    tft.drawRoundRect(118, 184, 50, 14, 3, THEME_OUTLINE);
    tft.setTextColor(rec ? THEME_ACCENT : THEME_PRIMARY, rbg);
    tft.drawCentreString(rec ? "REC ON" : "REC", 143, 186, 2);
    // Clock mode: INT / EXT / OUT (cycle on tap)
    const char* clkLbl;
    switch (zombieSeq->getClockMode()) {
      case ZombieSequencer::CLOCK_EXT: clkLbl = "EXT"; break;
      case ZombieSequencer::CLOCK_OUT: clkLbl = "OUT"; break;
      default:                         clkLbl = "INT"; break;
    }
    tft.fillRoundRect(172, 184, 42, 14, 3, THEME_BG);
    tft.drawRoundRect(172, 184, 42, 14, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(clkLbl, 193, 186, 2);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// EDIT TAB – per-step parameter editor
// ─────────────────────────────────────────────────────────────────────────────
static void seqDrawEdit() {
  if (!seqNeedsRedraw) return;
  tft.fillRect(0, 72, 320, 128, THEME_BG);
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;
  SequencerStep& step = tr->steps[seqEditStep];

  // ── Row 0: Step selector (y=73) ──────────────────────────────────────────
  tft.fillRect(0, 73, 320, 17, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("STEP", 3, 75, 2);
  // < prev
  tft.fillRoundRect(50, 73, 22, 17, 3, THEME_BG);
  tft.drawRoundRect(50, 73, 22, 17, 3, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("<", 61, 75, 2);
  // step number
  char snum[5]; snprintf(snum, sizeof(snum), "%02d", seqEditStep+1);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(snum, 110, 75, 2);
  // > next
  tft.fillRoundRect(130, 73, 22, 17, 3, THEME_BG);
  tft.drawRoundRect(130, 73, 22, 17, 3, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(">", 141, 75, 2);
  // ON/OFF toggle
  uint16_t abg  = step.active ? THEME_PRIMARY : THEME_BG;
  uint16_t atxt = step.active ? THEME_BG : THEME_PRIMARY;
  tft.fillRoundRect(160, 73, 60, 17, 3, abg);
  tft.drawRoundRect(160, 73, 60, 17, 3, THEME_OUTLINE);
  tft.setTextColor(atxt, abg);
  tft.drawCentreString(step.active ? "ON" : "OFF", 190, 75, 2);
  // TIE toggle
  uint16_t tbg  = step.tie ? THEME_SECONDARY : THEME_BG;
  tft.fillRoundRect(226, 73, 45, 17, 3, tbg);
  tft.drawRoundRect(226, 73, 45, 17, 3, THEME_OUTLINE);
  tft.setTextColor(step.tie ? THEME_BG : THEME_TEXT_DIM, tbg);
  tft.drawCentreString("TIE", 248, 75, 2);
  // STEP / PLOCK sub-tab toggle (replaces the standalone track indicator,
  // which is already shown on every other tab).
  const char* subLbl = (seqEditSubTab == 1) ? "PLK" : "STEP";
  uint16_t stbg = (seqEditSubTab == 1) ? THEME_ACCENT : THEME_BG;
  tft.fillRoundRect(275, 73, 42, 17, 3, stbg);
  tft.drawRoundRect(275, 73, 42, 17, 3, THEME_OUTLINE);
  tft.setTextColor((seqEditSubTab == 1) ? THEME_BG : THEME_PRIMARY, stbg);
  tft.drawCentreString(subLbl, 296, 75, 2);

  // When PLOCK page is selected, draw the P-Lock rows below instead of the
  // normal step parameters.  Step selector + ON/OFF/TIE/STEP-tag stay
  // visible on both pages.
  if (seqEditSubTab == 1) {
    auto drawRow = [](int y, const char* lbl, const char* val) {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(lbl, 3, y+2, 2);
      tft.fillRoundRect(50, y, 22, 16, 3, THEME_BG);
      tft.drawRoundRect(50, y, 22, 16, 3, THEME_OUTLINE);
      tft.setTextColor(THEME_PRIMARY, THEME_BG);
      tft.drawCentreString("-", 61, y+2, 2);
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawCentreString(val, 138, y+2, 2);
      tft.fillRoundRect(204, y, 22, 16, 3, THEME_BG);
      tft.drawRoundRect(204, y, 22, 16, 3, THEME_OUTLINE);
      tft.setTextColor(THEME_PRIMARY, THEME_BG);
      tft.drawCentreString("+", 215, y+2, 2);
    };
    char buf[16];
    int cl = (int)step.cutoffLock;
    snprintf(buf, sizeof(buf), "%+d", cl);
    drawRow(110, "CUTOFF LOCK", buf);
    int pl = (int)step.panLock;
    snprintf(buf, sizeof(buf), "%+d", pl);
    drawRow(140, "PAN LOCK",    buf);
    // Quick CLEAR LOCKS row
    bool locked = (step.cutoffLock != 0) || (step.panLock != 0);
    uint16_t cbg = locked ? THEME_WARNING : THEME_SURFACE;
    tft.fillRoundRect(50, 170, 176, 16, 3, cbg);
    tft.drawRoundRect(50, 170, 176, 16, 3, THEME_OUTLINE);
    tft.setTextColor(locked ? THEME_BG : THEME_TEXT_DIM, cbg);
    tft.drawCentreString(locked ? "CLEAR LOCKS ON THIS STEP" : "no locks on this step",
                         138, 172, 2);
    // Hint
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("Range -64..+63 (signed byte)", 3, 192, 2);
    return;  // STEP-page rows below are skipped
  }

  // Helper macro for value rows: label, value string, y position
  // Layout: label(x=3,w=46) | -(x=50,w=22) | value(x=73,w=130) | +(x=204,w=22) |
  auto drawRow = [](int y, const char* lbl, const char* val) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(lbl, 3, y+2, 2);
    tft.fillRoundRect(50, y, 22, 16, 3, THEME_BG);
    tft.drawRoundRect(50, y, 22, 16, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("-", 61, y+2, 2);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(val, 138, y+2, 2);
    tft.fillRoundRect(204, y, 22, 16, 3, THEME_BG);
    tft.drawRoundRect(204, y, 22, 16, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("+", 215, y+2, 2);
  };

  // ── Row 1: NOTE (y=92) ───────────────────────────────────────────────────
  int nn = step.note % 12, no = step.note / 12 - 1;
  char noteBuf[8]; snprintf(noteBuf, sizeof(noteBuf), "%s%d", SEQ_NOTE_NAMES[nn], no);
  drawRow(92, "NOTE", noteBuf);
  // shift octave buttons
  tft.fillRoundRect(232, 92, 40, 16, 3, THEME_BG);
  tft.drawRoundRect(232, 92, 40, 16, 3, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("OCT-", 252, 94, 2);
  tft.fillRoundRect(276, 92, 40, 16, 3, THEME_BG);
  tft.drawRoundRect(276, 92, 40, 16, 3, THEME_OUTLINE);
  tft.drawCentreString("OCT+", 296, 94, 2);

  // ── Row 2: VELOCITY (y=110) ──────────────────────────────────────────────
  char velBuf[5]; snprintf(velBuf, sizeof(velBuf), "%d", step.velocity);
  drawRow(110, "VEL", velBuf);

  // ── Row 3: GATE (y=128) ──────────────────────────────────────────────────
  char gateBuf[5]; snprintf(gateBuf, sizeof(gateBuf), "%d", step.gate);
  drawRow(128, "GATE", gateBuf);

  // ── Row 4: PROBABILITY (y=146) ───────────────────────────────────────────
  char probBuf[6]; snprintf(probBuf, sizeof(probBuf), "%d%%", (int)step.probability);
  drawRow(146, "PROB", probBuf);

  // ── Row 5: RATCHETS (y=164) – 4 toggle buttons ───────────────────────────
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("RTCH", 3, 166, 2);
  for (int r = 1; r <= 4; r++) {
    bool sel = (step.ratchets == r);
    uint16_t rbg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t rtxt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(50+(r-1)*54, 164, 50, 16, 3, rbg);
    tft.drawRoundRect(50+(r-1)*54, 164, 50, 16, 3, THEME_OUTLINE);
    tft.setTextColor(rtxt, rbg);
    char rbuf[3]; snprintf(rbuf, sizeof(rbuf), "x%d", r);
    tft.drawCentreString(rbuf, 75+(r-1)*54, 166, 2);
  }

  // ── Row 6: CONDITION (y=182) – 6 small toggle buttons ────────────────────
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("COND", 3, 184, 2);
  for (int c = 0; c < COND_COUNT; c++) {
    bool sel = (step.condition == c);
    uint16_t cbg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t ctxt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(50+c*44, 182, 42, 16, 3, cbg);
    tft.drawRoundRect(50+c*44, 182, 42, 16, 3, THEME_OUTLINE);
    tft.setTextColor(ctxt, cbg);
    tft.drawCentreString(COND_NAMES[c], 71+c*44, 184, 2);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// TRCK TAB – per-track settings
//   Sub-tab row at y=72..86 (h=14): SOUND | MIX
//   Content y=88..199 (112 px)
// ─────────────────────────────────────────────────────────────────────────────
static void seqDrawTrackSubTabs() {
  static const char* names[2] = {"SOUND","MIX"};
  for (int i = 0; i < 2; i++) {
    bool sel = (i == seqTrackSubTab);
    uint16_t bg  = sel ? THEME_ACCENT : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRect(i*160, 72, 160, 14, bg);
    tft.drawRect(i*160, 72, 160, 14, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(names[i], i*160+80, 74, 2);
  }
}

// MIX page: 4 track strips, each with M/S/VOL/PAN
static void seqDrawTrackMix() {
  // 4 tracks × strip-height = 28 px, starting at y=89
  for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
    SequencerTrack* tr = zombieSeq->getTrack(t);
    if (!tr) continue;
    int sy = 89 + t * 28;
    // Label
    bool sel = (t == seqEditTrack);
    uint16_t lbg = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t ltx = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(2, sy, 36, 24, 3, lbg);
    tft.drawRoundRect(2, sy, 36, 24, 3, THEME_OUTLINE);
    tft.setTextColor(ltx, lbg);
    char lbuf[6]; snprintf(lbuf, sizeof(lbuf), "T%d", t+1);
    tft.drawCentreString(lbuf, 20, sy+5, 2);

    // M button
    uint16_t mbg = tr->muted ? THEME_WARNING : THEME_BG;
    tft.fillRoundRect(40, sy, 24, 24, 3, mbg);
    tft.drawRoundRect(40, sy, 24, 24, 3, THEME_OUTLINE);
    tft.setTextColor(tr->muted ? THEME_BG : THEME_PRIMARY, mbg);
    tft.drawCentreString("M", 52, sy+5, 2);

    // S button
    uint16_t sbg = tr->soloed ? THEME_SUCCESS : THEME_BG;
    tft.fillRoundRect(66, sy, 24, 24, 3, sbg);
    tft.drawRoundRect(66, sy, 24, 24, 3, THEME_OUTLINE);
    tft.setTextColor(tr->soloed ? THEME_BG : THEME_PRIMARY, sbg);
    tft.drawCentreString("S", 78, sy+5, 2);

    // VOL: < value > occupies x=94..205
    tft.fillRoundRect(94, sy, 20, 24, 3, THEME_BG);
    tft.drawRoundRect(94, sy, 20, 24, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("<", 104, sy+5, 2);
    char vbuf[10]; snprintf(vbuf, sizeof(vbuf), "V %3d", (int)(tr->trackVolume*100));
    tft.fillRect(116, sy, 70, 24, THEME_BG);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(vbuf, 151, sy+5, 2);
    tft.fillRoundRect(188, sy, 20, 24, 3, THEME_BG);
    tft.drawRoundRect(188, sy, 20, 24, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(">", 198, sy+5, 2);

    // PAN: < value > occupies x=212..317
    tft.fillRoundRect(212, sy, 20, 24, 3, THEME_BG);
    tft.drawRoundRect(212, sy, 20, 24, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("<", 222, sy+5, 2);
    char pbuf[10];
    int pp = (int)(tr->trackPan * 100);
    if      (pp <= -99) snprintf(pbuf, sizeof(pbuf), "P L");
    else if (pp >=  99) snprintf(pbuf, sizeof(pbuf), "P R");
    else if (pp == 0)   snprintf(pbuf, sizeof(pbuf), "P C");
    else                snprintf(pbuf, sizeof(pbuf), "P%+d", pp);
    tft.fillRect(234, sy, 60, 24, THEME_BG);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(pbuf, 264, sy+5, 2);
    tft.fillRoundRect(296, sy, 20, 24, 3, THEME_BG);
    tft.drawRoundRect(296, sy, 20, 24, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(">", 306, sy+5, 2);
  }
}

static void seqDrawTrack() {
  if (!seqNeedsRedraw) return;
  tft.fillRect(0, 72, 320, 128, THEME_BG);
  seqDrawTrackSubTabs();
  if (seqTrackSubTab == 1) {
    seqDrawTrackMix();
    return;
  }
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;

  // Row layout helper – label + 4 option buttons
  auto draw4Buttons = [](int y, const char* lbl,
                          const char** opts, int n, int sel) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(lbl, 3, y+2, 2);
    int bw = (315 - 55) / n;
    for (int i = 0; i < n; i++) {
      bool s = (i == sel);
      uint16_t bg  = s ? THEME_PRIMARY : THEME_BG;
      uint16_t txt = s ? THEME_BG : THEME_PRIMARY;
      tft.fillRoundRect(55+i*bw, y, bw-2, 17, 3, bg);
      tft.drawRoundRect(55+i*bw, y, bw-2, 17, 3, THEME_OUTLINE);
      tft.setTextColor(txt, bg);
      tft.drawCentreString(opts[i], 55+i*bw+(bw-2)/2, y+2, 2);
    }
  };

  // SOUND rows shift down to clear sub-tab bar (y=72..86).
  // Spacing 18 px, h=17 → 6 rows fit in y=88..195.
  // ── LENGTH (y=88) ────────────────────────────────────────────────────────
  static const char* lenOpts[4] = {"8","16","24","32"};
  static const int   lenVals[4] = { 8,  16,  24,  32 };
  int lenSel = 1;
  for (int i = 0; i < 4; i++) if (lenVals[i] == tr->length) lenSel = i;
  draw4Buttons(88, "LEN", lenOpts, 4, lenSel);

  // ── DIVISION (y=106) ─────────────────────────────────────────────────────
  draw4Buttons(106, "DIV", SEQ_DIV_NAMES, 4, tr->divIdx);

  // Spinner helper: < label value > row
  auto drawSpinner = [](int y, const char* lbl, const char* val) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(lbl, 3, y+2, 2);
    tft.fillRoundRect(55, y, 22, 17, 3, THEME_BG);
    tft.drawRoundRect(55, y, 22, 17, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("<", 66, y+2, 2);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(val, 160, y+2, 2);
    tft.fillRoundRect(243, y, 22, 17, 3, THEME_BG);
    tft.drawRoundRect(243, y, 22, 17, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(">", 254, y+2, 2);
  };

  // ── SCALE (y=124) ────────────────────────────────────────────────────────
  drawSpinner(124, "SCALE", SEQ_SCALE_NAMES[tr->scale]);

  // ── ROOT (y=142) ─────────────────────────────────────────────────────────
  drawSpinner(142, "ROOT", SEQ_NOTE_NAMES[tr->rootNote]);

  // ── OCTAVE (y=160) ───────────────────────────────────────────────────────
  char octBuf[4]; snprintf(octBuf, sizeof(octBuf), "%d", tr->octave);
  drawSpinner(160, "OCT", octBuf);

  // ── SOUND PATCH (y=178) ──────────────────────────────────────────────────
  int pIdx = constrain(tr->soundPatchIdx, 0, NUM_FACTORY-1);
  drawSpinner(178, "SNDPATCH", factoryPresets[pIdx].name);
}

// ─────────────────────────────────────────────────────────────────────────────
// ARP→SEQ helpers
// Pure index function: maps a sequencer step to an arp-pattern note index.
// Mirrors the Arpeggiator::getNextNoteIndex() logic without needing held state.
// ─────────────────────────────────────────────────────────────────────────────
static int arpFillNoteIdx(ArpPattern pat, int step, int noteCount) {
  if (noteCount <= 0) return 0;
  int idx = 0;
  switch (pat) {
    case ARP_UP:
      idx = step % noteCount;
      break;
    case ARP_DOWN:
      idx = noteCount - 1 - (step % noteCount);
      break;
    case ARP_UP_DOWN: {
      int cycle = (noteCount > 1) ? (noteCount - 1) * 2 : 1;
      int pos   = step % cycle;
      idx = (pos < noteCount) ? pos : (cycle - pos);
      break;
    }
    case ARP_DOWN_UP: {
      int cycle = (noteCount > 1) ? (noteCount - 1) * 2 : 1;
      int pos   = step % cycle;
      idx = (pos < noteCount) ? (noteCount - 1 - pos) : (pos - noteCount + 1);
      break;
    }
    case ARP_RANDOM:
      idx = random(noteCount);
      break;
    case ARP_CONVERGE: {
      int half = noteCount / 2;
      if (half == 0) { idx = 0; break; }
      idx = ((step / half) % 2 == 0) ? (step % half) : (noteCount - 1 - (step % half));
      break;
    }
    case ARP_DIVERGE: {
      int half = noteCount / 2;
      idx = (step % 2 == 0) ? (half - (step / 2) % (half + 1)) : (half + (step / 2) % (half + 1));
      idx = constrain(idx, 0, noteCount - 1);
      break;
    }
    case ARP_THIRDS_UP:
      idx = ((step % noteCount) + (step / noteCount) * 2) % noteCount;
      break;
    case ARP_OCTAVES:
      idx = step % noteCount;
      break;
    case ARP_REPEAT_2X:
      idx = (step / 2) % noteCount;
      break;
    case ARP_REPEAT_3X:
      idx = (step / 3) % noteCount;
      break;
    case ARP_PENDULUM:
      idx = (step % 2 == 0) ? 0 : min(step / 2 + 1, noteCount - 1);
      break;
    case ARP_ZIGZAG: {
      int base = step / 2;
      idx = (step % 2 == 0) ? base : (base + noteCount / 2);
      idx = idx % noteCount;
      break;
    }
    case ARP_BOUNCE:
      idx = abs((step % (noteCount * 2)) - noteCount) % noteCount;
      break;
    case ARP_STUTTER:
      idx = (step / 2) % noteCount;
      break;
    case ARP_ALTERNATING:
      idx = (step % 2 == 0) ? 0 : (noteCount - 1);
      break;
    case ARP_PYRAMID: {
      int half = noteCount;
      int cycle = half * 2 - 1;
      int pos   = step % (cycle > 0 ? cycle : 1);
      idx = (pos < half) ? pos : (cycle - pos);
      break;
    }
    case ARP_SINE_WAVE: {
      float ph = (step * TWO_PI) / (float)noteCount;
      idx = (int)((sinf(ph) + 1.0f) * (noteCount - 1) * 0.5f);
      break;
    }
    case ARP_TRIANGLE_WAVE: {
      int cycle = noteCount * 2 - 1;
      int pos   = step % (cycle > 0 ? cycle : 1);
      idx = (pos < noteCount) ? pos : (cycle - pos);
      break;
    }
    case ARP_SAW_WAVE:
      idx = step % noteCount;
      break;
    case ARP_SQUARE_WAVE:
      idx = (step % 2 == 0) ? 0 : noteCount - 1;
      break;
    default:
      idx = step % noteCount;
      break;
  }
  return constrain(idx, 0, noteCount - 1);
}

// Fills the active track with notes generated by applying an arp pattern
// to the scale notes derived from the track's scale/root/octave settings.
static void seqDoArpFill(int trackIdx, int arpPatIdx) {
  SequencerTrack* tr = zombieSeq->getTrack(trackIdx);
  if (!tr) return;

  // Build an in-scale note pool centred on middle C (MIDI 60, C4) so arp fills
  // land in a musical mid range.  The track's own octave is applied later at
  // trigger time (raw = note + (octave-3)*12), so we deliberately work in
  // absolute MIDI here.  (The old code used tr->octave as an absolute MIDI
  // octave — oct*12 — which forced every fill down to ~C1 and sounded awful.)
  int  notes[48];
  int  nc   = 0;
  uint16_t mask = SEQ_SCALE_MASKS[tr->scale];
  const int loMidi = 48;   // C3
  const int hiMidi = 72;   // C5  (two octaves, centred on C4)

  for (int m = loMidi; m <= hiMidi && nc < 48; m++) {
    int rel = ((m - tr->rootNote) % 12 + 12) % 12;
    if (mask & (1 << rel)) notes[nc++] = m;
  }

  if (nc == 0) return;

  ArpPattern pat = (ArpPattern)constrain(arpPatIdx, 0, NUM_ARP_PATTERNS - 1);

  for (int s = 0; s < tr->length; s++) {
    int ni = arpFillNoteIdx(pat, s, nc);
    tr->steps[s].active      = true;
    tr->steps[s].note        = notes[ni];
    tr->steps[s].velocity    = 100;
    tr->steps[s].gate        = 8;
    tr->steps[s].probability = 100;
    tr->steps[s].ratchets    = 1;
    tr->steps[s].condition   = COND_NONE;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// PTRN TAB – pattern management
// ─────────────────────────────────────────────────────────────────────────────
// Big labelled button helper (filled, optional WARNING/armed colour).
static void seqBigBtn(int x, int y, int w, int h, const char* lbl,
                      bool armed = false, bool primary = false) {
  uint16_t bg  = armed ? THEME_WARNING : (primary ? THEME_PRIMARY : THEME_BG);
  uint16_t txt = (armed || primary) ? THEME_BG : THEME_PRIMARY;
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawRoundRect(x, y, w, h, 4, THEME_OUTLINE);
  tft.setTextColor(txt, bg);
  tft.drawCentreString(armed ? "SURE?" : lbl, x + w/2, y + (h-13)/2, 2);
}

// PTRN sub-tab bar (BANK | TOOLS) at y=72..86, mirrors the TRCK sub-tabs.
static void seqDrawPtrnSubTabs() {
  static const char* names[2] = {"BANK", "TOOLS"};
  for (int i = 0; i < 2; i++) {
    bool sel = (i == seqPtrnSubTab);
    uint16_t bg  = sel ? THEME_ACCENT : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRect(i*160, 72, 160, 14, bg);
    tft.drawRect(i*160, 72, 160, 14, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(names[i], i*160+80, 74, 2);
  }
}

// BANK sub-tab: 8 pattern slots (saved-dot + state) + COPY/PASTE + SAVE/LOAD.
static void seqDrawPtrnBank() {
  for (int p = 0; p < MAX_PATTERNS; p++) {
    int col = p % 4, row = p / 4;
    int px  = 4 + col * 78;
    int py  = 89 + row * 28;          // rows at y=89, 117 (h=26)
    bool isCurrent = (p == zombieSeq->activePattern);
    bool isNext    = (p == zombieSeq->nextPattern);
    bool isCopySrc = (p == seqCopyPat);

    uint16_t bg  = isCurrent ? THEME_PRIMARY :
                   isNext    ? THEME_SECONDARY :
                   isCopySrc ? THEME_SURFACE : THEME_BG;
    uint16_t brd = isCurrent ? THEME_ACCENT :
                   isNext    ? THEME_PRIMARY : THEME_OUTLINE;
    uint16_t txt = isCurrent ? THEME_BG : THEME_PRIMARY;

    tft.fillRoundRect(px, py, 76, 26, 4, bg);
    tft.drawRoundRect(px, py, 76, 26, 4, brd);
    tft.setTextColor(txt, bg);
    char pbuf[10]; snprintf(pbuf, sizeof(pbuf), "PAT %d", p+1);
    tft.drawCentreString(pbuf, px+38, py+3, 2);
    if (isNext)         { tft.setTextColor(THEME_ACCENT, bg);   tft.drawCentreString("NEXT", px+38, py+14, 2); }
    else if (isCopySrc) { tft.setTextColor(THEME_TEXT_DIM, bg); tft.drawCentreString("SRC",  px+38, py+14, 2); }
    // Saved-to-NVS dot (top-right corner)
    if (zombieSeq->isPatternSaved(p))
      tft.fillCircle(px+70, py+6, 3, THEME_SUCCESS);
  }

  // COPY / PASTE (y=147, h=22)
  seqBigBtn(4, 147, 150, 22, "COPY PAT");
  bool canPaste = (seqCopyPat >= 0 && seqCopyPat != zombieSeq->activePattern);
  if (canPaste) {
    seqBigBtn(166, 147, 150, 22, "PASTE");
  } else {
    tft.fillRoundRect(166, 147, 150, 22, 4, THEME_SURFACE);
    tft.drawRoundRect(166, 147, 150, 22, 4, THEME_TEXT_DIM);
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawCentreString("PASTE", 241, 151, 2);
  }

  // SAVE / LOAD this bank to/from its own NVS slot (y=173, h=22)
  seqBigBtn(4,   173, 150, 22, "SAVE");
  seqBigBtn(166, 173, 150, 22, "LOAD", seqArmed(SEQ_ARM_LOAD));
}

// TOOLS sub-tab: CHAIN, randomise, clear-pattern, ARP>SEQ fill.
static void seqDrawPtrnTools() {
  // CHAIN next-pattern selector (y=90, h=22)
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("CHAIN", 6, 96, 2);
  seqBigBtn(70, 90, 30, 22, "<");
  char chainBuf[8];
  if (zombieSeq->nextPattern < 0) snprintf(chainBuf, sizeof(chainBuf), "NONE");
  else                            snprintf(chainBuf, sizeof(chainBuf), "PAT %d", zombieSeq->nextPattern+1);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(chainBuf, 162, 96, 2);
  seqBigBtn(224, 90, 30, 22, ">");

  // RND TRACK / RND ALL (y=118, h=22)
  seqBigBtn(4,   118, 150, 22, "RND TRACK");
  seqBigBtn(166, 118, 150, 22, "RND ALL");

  // CLR PATTERN (2-tap confirm) (y=146, h=22)
  seqBigBtn(4, 146, 150, 22, "CLR PATTERN", seqArmed(SEQ_ARM_CLRPAT));

  // ARP > SEQ fill: pick an arp pattern then fill the edit track (y=174, h=22)
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawString("ARP>SEQ", 166, 152, 2);
  seqBigBtn(4, 174, 24, 22, "<");
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(arpPatternNames[seqArpPatIdx], 116, 180, 2);
  seqBigBtn(200, 174, 24, 22, ">");
  seqBigBtn(228, 174, 88, 22, "FILL TRK", false, true);
}

static void seqDrawPattern() {
  if (!seqNeedsRedraw) return;
  tft.fillRect(0, 72, 320, 128, THEME_BG);
  seqDrawPtrnSubTabs();
  if (seqPtrnSubTab == 0) seqDrawPtrnBank();
  else                    seqDrawPtrnTools();
}

// ─────────────────────────────────────────────────────────────────────────────
// SONG TAB – ordered list of patterns with per-slot repeat count
//   Row 1 (y=73..89):  8 song slot buttons (load/select)
//   Row 2 (y=92..108): SAVE / LOAD / CLR / scroll up/down
//   Rows 3..6 (y=111..175): 4 visible song slots
//   Row 7 (y=178..198): PLAY SONG / STOP / INSERT / DELETE / LOOP
// ─────────────────────────────────────────────────────────────────────────────
static int songVisibleStart = 0;   // first slot visible in the slot list
static bool seqPendingSongAction = false; // true while SAVE/LOAD pending

static void seqDrawSong() {
  if (!seqNeedsRedraw) return;
  tft.fillRect(0, 72, 320, 128, THEME_BG);
  if (!zombieSeq) return;

  // Row 1: 8 song slot buttons (y=73, h=16, each 38 px)
  for (int s = 0; s < NUM_SONGS; s++) {
    int sx  = 4 + s * 39;
    bool cur = (s == seqSongSaveSlot);
    bool saved = zombieSeq->isSongSaved(s);
    uint16_t bg  = cur ? THEME_PRIMARY : (saved ? THEME_BG : THEME_SURFACE);
    uint16_t brd = saved ? THEME_OUTLINE : THEME_TEXT_DIM;
    uint16_t txt = cur ? THEME_BG : (saved ? THEME_PRIMARY : THEME_TEXT_DIM);
    tft.fillRoundRect(sx, 73, 36, 16, 3, bg);
    tft.drawRoundRect(sx, 73, 36, 16, 3, brd);
    tft.setTextColor(txt, bg);
    char sb[5]; snprintf(sb, sizeof(sb), "S%d", s+1);
    tft.drawCentreString(sb, sx+18, 75, 2);
  }

  // Row 2: SAVE / LOAD / CLR / LOOP / SNAP / PASTE / UP / DN (y=92, h=16)
  auto songBtn = [](int x, int w, const char* lbl, uint16_t bg, uint16_t txt) {
    tft.fillRoundRect(x, 92, w, 16, 3, bg);
    tft.drawRoundRect(x, 92, w, 16, 3, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(lbl, x + w/2, 94, 2);
  };
  uint16_t pendBg = seqPendingSongAction ? THEME_WARNING : THEME_BG;
  bool     snap   = zombieSeq->hasSnapshot();
  bool     loop   = zombieSeq->songLoop;
  songBtn(2,   36, "SAVE", THEME_BG, THEME_PRIMARY);
  songBtn(40,  36, "LOAD", pendBg, seqPendingSongAction ? THEME_BG : THEME_PRIMARY);
  songBtn(78,  30, "CLR",  THEME_BG, THEME_PRIMARY);
  songBtn(110, 38, "LOOP", loop ? THEME_SUCCESS : THEME_BG, loop ? THEME_BG : THEME_PRIMARY);
  // SNAP captures the current 4 tracks; lit green once a snapshot is held.
  songBtn(150, 40, "SNAP", snap ? THEME_SUCCESS : THEME_BG, snap ? THEME_BG : THEME_PRIMARY);
  // PASTE drops the snapshot into the selected slot's pattern (disabled until SNAP).
  songBtn(192, 44, "PASTE", snap ? THEME_BG : THEME_SURFACE, snap ? THEME_PRIMARY : THEME_TEXT_DIM);
  songBtn(238, 38, "UP",   THEME_BG, THEME_PRIMARY);
  songBtn(278, 38, "DN",   THEME_BG, THEME_PRIMARY);

  // Rows 3..6: visible slots (y=111..175, 4 rows × 16 px = 64 px)
  Song& song = zombieSeq->currentSong;
  int total = (int)song.numSlots;
  if (songVisibleStart > total - 4) songVisibleStart = total - 4;
  if (songVisibleStart < 0) songVisibleStart = 0;

  for (int row = 0; row < 4; row++) {
    int idx = songVisibleStart + row;
    int ry  = 111 + row * 16;
    if (idx >= total) {
      tft.fillRect(0, ry, 320, 14, THEME_BG);
      continue;
    }
    bool sel = (idx == seqSongSlotIdx);
    uint16_t rowBg = sel ? THEME_SURFACE : THEME_BG;
    tft.fillRect(0, ry, 320, 15, rowBg);
    // Slot number (with current-playing indicator)
    bool playing = zombieSeq->songMode && idx == zombieSeq->songSlotIdx;
    tft.setTextColor(playing ? THEME_ACCENT : THEME_TEXT_DIM, rowBg);
    char nb[6]; snprintf(nb, sizeof(nb), "%02d%s", idx+1, playing?"*":"");
    tft.drawString(nb, 4, ry, 2);
    // PAT-
    tft.fillRoundRect(34, ry, 18, 14, 3, rowBg);
    tft.drawRoundRect(34, ry, 18, 14, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, rowBg);
    tft.drawCentreString("-", 43, ry, 2);
    // Pattern label — a dim "!" warns when this slot points at an empty
    // pattern (it would play silent).
    char pb[10]; snprintf(pb, sizeof(pb), "PAT %d", song.slots[idx].patternIdx + 1);
    bool emptyPat = !zombieSeq->patternHasContent(song.slots[idx].patternIdx);
    tft.setTextColor(THEME_ACCENT, rowBg);
    tft.drawCentreString(pb, 92, ry, 2);
    if (emptyPat) {
      tft.setTextColor(THEME_WARNING, rowBg);
      tft.drawString("!", 124, ry, 2);
    }
    // PAT+
    tft.fillRoundRect(138, ry, 18, 14, 3, rowBg);
    tft.drawRoundRect(138, ry, 18, 14, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, rowBg);
    tft.drawCentreString("+", 147, ry, 2);
    // REPS-
    tft.fillRoundRect(170, ry, 18, 14, 3, rowBg);
    tft.drawRoundRect(170, ry, 18, 14, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, rowBg);
    tft.drawCentreString("-", 179, ry, 2);
    // Reps value
    char rb[8]; snprintf(rb, sizeof(rb), "x %d", song.slots[idx].repeatCount);
    tft.setTextColor(THEME_ACCENT, rowBg);
    tft.drawCentreString(rb, 225, ry, 2);
    // REPS+
    tft.fillRoundRect(260, ry, 18, 14, 3, rowBg);
    tft.drawRoundRect(260, ry, 18, 14, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, rowBg);
    tft.drawCentreString("+", 269, ry, 2);
    // SELECT button
    tft.fillRoundRect(282, ry, 34, 14, 3, sel ? THEME_PRIMARY : rowBg);
    tft.drawRoundRect(282, ry, 34, 14, 3, THEME_OUTLINE);
    tft.setTextColor(sel ? THEME_BG : THEME_PRIMARY, sel ? THEME_PRIMARY : rowBg);
    tft.drawCentreString("SEL", 299, ry, 2);
  }

  // Row 7: PLAY/STOP, LIVE, INS, DEL, count (y=178, h=20)
  bool playing = zombieSeq->songMode && zombieSeq->getIsPlaying();
  uint16_t pbg = playing ? THEME_SUCCESS : THEME_PRIMARY;
  tft.fillRoundRect(4, 178, 80, 20, 4, pbg);
  tft.drawRoundRect(4, 178, 80, 20, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_BG, pbg);
  tft.drawCentreString(playing ? "STOP SONG" : "PLAY SONG", 44, 181, 2);
  // LIVE / INSTANT toggle for snap pattern switches
  bool live = zombieSeq->getInstantSwitch();
  uint16_t libg = live ? THEME_WARNING : THEME_BG;
  tft.fillRoundRect(88, 178, 52, 20, 4, libg);
  tft.drawRoundRect(88, 178, 52, 20, 4, THEME_OUTLINE);
  tft.setTextColor(live ? THEME_BG : THEME_PRIMARY, libg);
  tft.drawCentreString("LIVE", 114, 181, 2);
  tft.fillRoundRect(144, 178, 50, 20, 4, THEME_BG);
  tft.drawRoundRect(144, 178, 50, 20, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("INS", 169, 181, 2);
  tft.fillRoundRect(198, 178, 50, 20, 4, THEME_BG);
  tft.drawRoundRect(198, 178, 50, 20, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("DEL", 223, 181, 2);
  // Slot count display
  char cb[16];
  snprintf(cb, sizeof(cb), "%d/%d", (int)song.numSlots, MAX_SONG_SLOTS);
  tft.fillRect(252, 178, 66, 20, THEME_BG);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString(cb, 285, 183, 2);
}

static void seqTouchSong() {
  // Row 1: 8 song slot buttons
  for (int s = 0; s < NUM_SONGS; s++) {
    int sx = 4 + s * 39;
    if (isButtonPressed(sx, 73, 36, 16)) {
      if (seqPendingSongAction) {
        // LOAD pending → load this slot into currentSong
        if (zombieSeq->loadSong(s)) {
          seqSongSaveSlot = s;
          seqSongSlotIdx  = 0;
          songVisibleStart = 0;
        }
        seqPendingSongAction = false;
      } else {
        seqSongSaveSlot = s;
      }
      seqNeedsRedraw = true; return;
    }
  }
  // Row 2: SAVE / LOAD / CLR / LOOP / SNAP / PASTE / UP / DN
  if (isButtonPressed(2, 92, 36, 16)) {
    zombieSeq->saveSong(seqSongSaveSlot);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(40, 92, 36, 16)) {
    seqPendingSongAction = !seqPendingSongAction;
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(78, 92, 30, 16)) {
    zombieSeq->songClear();
    seqSongSlotIdx = 0;
    songVisibleStart = 0;
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(110, 92, 38, 16)) {
    zombieSeq->songLoop = !zombieSeq->songLoop;
    seqNeedsRedraw = true; return;
  }
  // SNAP: capture the active pattern's 4 tracks into the clipboard.
  if (isButtonPressed(150, 92, 40, 16)) {
    zombieSeq->snapshotTracks();
    seqNeedsRedraw = true; return;
  }
  // PASTE: drop the snapshot into the selected slot's pattern bank, so that
  // song slot plays the captured 4 tracks (loop count set via REPS +/-).
  if (isButtonPressed(192, 92, 44, 16)) {
    Song& sg = zombieSeq->currentSong;
    if (zombieSeq->hasSnapshot() && seqSongSlotIdx < (int)sg.numSlots) {
      zombieSeq->pasteSnapshot(sg.slots[seqSongSlotIdx].patternIdx);
    }
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(238, 92, 38, 16)) {
    songVisibleStart = max(0, songVisibleStart - 1);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(278, 92, 38, 16)) {
    int maxStart = max(0, (int)zombieSeq->currentSong.numSlots - 4);
    songVisibleStart = min(maxStart, songVisibleStart + 1);
    seqNeedsRedraw = true; return;
  }

  // Slot edit rows
  Song& song = zombieSeq->currentSong;
  for (int row = 0; row < 4; row++) {
    int idx = songVisibleStart + row;
    if (idx >= (int)song.numSlots) continue;
    int ry  = 111 + row * 16;
    // PAT-
    if (isButtonPressed(34, ry, 18, 14)) {
      int v = (int)song.slots[idx].patternIdx;
      v = (v - 1 + MAX_PATTERNS) % MAX_PATTERNS;
      zombieSeq->songSetSlotPattern(idx, v);
      seqNeedsRedraw = true; return;
    }
    // PAT+
    if (isButtonPressed(138, ry, 18, 14)) {
      int v = (int)song.slots[idx].patternIdx;
      v = (v + 1) % MAX_PATTERNS;
      zombieSeq->songSetSlotPattern(idx, v);
      seqNeedsRedraw = true; return;
    }
    // REPS-
    if (isButtonPressed(170, ry, 18, 14)) {
      zombieSeq->songSetSlotRepeats(idx, (int)song.slots[idx].repeatCount - 1);
      seqNeedsRedraw = true; return;
    }
    // REPS+
    if (isButtonPressed(260, ry, 18, 14)) {
      zombieSeq->songSetSlotRepeats(idx, (int)song.slots[idx].repeatCount + 1);
      seqNeedsRedraw = true; return;
    }
    // SEL
    if (isButtonPressed(282, ry, 34, 14)) {
      seqSongSlotIdx = idx;
      seqNeedsRedraw = true; return;
    }
  }

  // Bottom row
  if (isButtonPressed(4, 178, 80, 20)) {
    if (zombieSeq->songMode && zombieSeq->getIsPlaying()) {
      zombieSeq->stopSong();
    } else {
      zombieSeq->playSong();
    }
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(88, 178, 52, 20)) {
    zombieSeq->setInstantSwitch(!zombieSeq->getInstantSwitch());
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(144, 178, 50, 20)) {
    zombieSeq->songInsertSlot(seqSongSlotIdx);
    seqSongSlotIdx = constrain(seqSongSlotIdx + 1, 0, (int)zombieSeq->currentSong.numSlots - 1);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(198, 178, 50, 20)) {
    zombieSeq->songDeleteSlot(seqSongSlotIdx);
    seqSongSlotIdx = constrain(seqSongSlotIdx, 0, (int)zombieSeq->currentSong.numSlots - 1);
    seqNeedsRedraw = true; return;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// SYNTH TAB – per-track full patch editor
// ─────────────────────────────────────────────────────────────────────────────
static const char* SEQ_WAVE_NAMES[5] = {"SAW","SQR","TRI","SIN","PLS"};
static const char* SEQ_FILT_NAMES[4] = {"LP","HP","BP","NT"};
#define SEQ_NUM_SSTABS 8
#define SEQ_SSTAB_W    40
static const char* SEQ_SYNTH_STABS[SEQ_NUM_SSTABS] =
  {"OSC","FLT","SUB","AMP","FEG","LFO","FX","PRS"};
// Delay division labels (must match DelayFX::setTimeFromBPM index order)
static const char* SEQ_DELAY_DIV_NAMES[4] = {"1/4","1/8d","1/8","1/16"};
static const char* SEQ_LFO_WAVE_NAMES[5] = {"SIN","TRI","SAW","SQR","S&H"};
static const char* SEQ_LFO_TARGET_NAMES[3] = {"FILTER","PITCH","AMP"};

static void seqDrawSynth() {
  if (!seqNeedsRedraw) return;
  tft.fillRect(0, 72, 320, 128, THEME_BG);
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;
  SynthPatch& p = tr->trackPatch;

  // Track selector row (y=73..86, 4 buttons × 79px)
  for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
    SequencerTrack* tt = zombieSeq->getTrack(t);
    bool sel   = (t == seqEditTrack);
    bool muted = tt && tt->muted;
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : (muted ? THEME_TEXT_DIM : THEME_PRIMARY);
    tft.fillRoundRect(1+t*79, 73, 78, 14, 3, bg);
    tft.drawRoundRect(1+t*79, 73, 78, 14, 3, muted ? THEME_TEXT_DIM : THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    char tbuf[6]; snprintf(tbuf, sizeof(tbuf), "TRK%d", t+1);
    tft.drawCentreString(tbuf, 40+t*79, 75, 2);
  }

  // Sub-tab bar (y=88..101, 6 sub-tabs × 53 px)
  tft.fillRect(SEQ_NUM_SSTABS*SEQ_SSTAB_W, 88, 320-SEQ_NUM_SSTABS*SEQ_SSTAB_W, 14, THEME_BG);
  for (int i = 0; i < SEQ_NUM_SSTABS; i++) {
    bool sel = (i == seqSynthSubTab);
    uint16_t bg  = sel ? THEME_ACCENT : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRect(i*SEQ_SSTAB_W, 88, SEQ_SSTAB_W, 14, bg);
    tft.drawRect(i*SEQ_SSTAB_W, 88, SEQ_SSTAB_W, 14, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(SEQ_SYNTH_STABS[i], i*SEQ_SSTAB_W+SEQ_SSTAB_W/2, 90, 2);
  }

  // Spinner helper (label x=3, < x=55, value centred, > x=243)
  auto spin = [](int y, const char* lbl, const char* val) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(lbl, 3, y+2, 2);
    tft.fillRoundRect(55, y, 22, 17, 3, THEME_BG);
    tft.drawRoundRect(55, y, 22, 17, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("<", 66, y+2, 2);
    tft.fillRect(79, y, 162, 17, THEME_BG);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(val, 160, y+2, 2);
    tft.fillRoundRect(243, y, 22, 17, 3, THEME_BG);
    tft.drawRoundRect(243, y, 22, 17, 3, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(">", 254, y+2, 2);
  };

  char buf[16];
  switch (seqSynthSubTab) {

    case 0: { // OSC
      snprintf(buf, sizeof(buf), "%s", SEQ_WAVE_NAMES[constrain(p.osc1Wave, 0, 4)]);
      spin(104, "OSC1 WAVE", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.osc1Level);
      spin(123, "OSC1 LVL",  buf);
      snprintf(buf, sizeof(buf), "%s", SEQ_WAVE_NAMES[constrain(p.osc2Wave, 0, 4)]);
      spin(142, "OSC2 WAVE", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.osc2Level);
      spin(161, "OSC2 LVL",  buf);
      snprintf(buf, sizeof(buf), "%.3f", p.osc2Detune);
      spin(180, "DETUNE",    buf);
      break;
    }

    case 1: { // FLT
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("TYPE", 3, 106, 2);
      for (int i = 0; i < 4; i++) {
        bool sel = (p.filterType == i);
        uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
        uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
        tft.fillRoundRect(55+i*66, 104, 62, 17, 3, bg);
        tft.drawRoundRect(55+i*66, 104, 62, 17, 3, THEME_OUTLINE);
        tft.setTextColor(txt, bg);
        tft.drawCentreString(SEQ_FILT_NAMES[i], 55+i*66+31, 106, 2);
      }
      snprintf(buf, sizeof(buf), "%.2f", p.filterCutoff);
      spin(123, "CUTOFF",  buf);
      snprintf(buf, sizeof(buf), "%.2f", p.filterResonance);
      spin(142, "RESO",    buf);
      snprintf(buf, sizeof(buf), "%.2f", p.filterEnvAmount);
      spin(161, "ENV AMT", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.masterVolume);
      spin(180, "MAST VOL",buf);
      break;
    }

    case 2: { // SUB – dedicated sub oscillator
      snprintf(buf, sizeof(buf), "%s", SEQ_WAVE_NAMES[constrain(p.subWave, 0, 4)]);
      spin(104, "SUB WAVE", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.subLevel);
      spin(123, "SUB LVL",  buf);
      snprintf(buf, sizeof(buf), "-%d OCT", constrain(p.subOctave, 1, 2));
      spin(142, "SUB OCT",  buf);
      // Hint line
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("SUB OSC adds a fixed-octave-down", 6, 168, 2);
      tft.drawString("oscillator below the played note.", 6, 184, 2);
      break;
    }

    case 3: { // AMP envelope
      snprintf(buf, sizeof(buf), "%.3f", p.ampAttack);
      spin(104, "ATK", buf);
      snprintf(buf, sizeof(buf), "%.3f", p.ampDecay);
      spin(123, "DCY", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.ampSustain);
      spin(142, "SUS", buf);
      snprintf(buf, sizeof(buf), "%.3f", p.ampRelease);
      spin(161, "REL", buf);
      break;
    }

    case 4: { // Filter envelope
      snprintf(buf, sizeof(buf), "%.3f", p.filterAttack);
      spin(104, "F.ATK", buf);
      snprintf(buf, sizeof(buf), "%.3f", p.filterDecay);
      spin(123, "F.DCY", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.filterSustain);
      spin(142, "F.SUS", buf);
      snprintf(buf, sizeof(buf), "%.3f", p.filterRelease);
      spin(161, "F.REL", buf);
      break;
    }

    case 5: { // LFO – per-track modulation
      int w = constrain(p.lfoWave, 0, 4);
      snprintf(buf, sizeof(buf), "%s", SEQ_LFO_WAVE_NAMES[w]);
      spin(104, "LFO WAVE", buf);
      snprintf(buf, sizeof(buf), "%.2f Hz", p.lfoRate);
      spin(123, "LFO RATE", buf);
      snprintf(buf, sizeof(buf), "%.2f", p.lfoDepth);
      spin(142, "LFO DEPTH", buf);
      int tg = constrain(p.lfoTarget, 0, 2);
      snprintf(buf, sizeof(buf), "%s", SEQ_LFO_TARGET_NAMES[tg]);
      spin(161, "LFO TARGT", buf);
      // hint
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("Per-track LFO modulates the chosen", 4, 184, 2);
      break;
    }

    case 6: { // FX – master delay + selectable limiter
      SynthEngine* synth = getZombieSynth();
      int   div  = synth ? synth->getDelayDivision() : 0;
      float dmix = synth ? synth->getDelayMix()      : 0.0f;
      float dfb  = synth ? synth->getDelayFeedback() : 0.0f;
      bool  lim  = synth ? synth->getLimiterEnabled(): false;

      snprintf(buf, sizeof(buf), "%s", SEQ_DELAY_DIV_NAMES[constrain(div, 0, 3)]);
      spin(104, "DLY DIV",  buf);
      snprintf(buf, sizeof(buf), "%.2f", dmix);
      spin(123, "DLY MIX",  buf);
      snprintf(buf, sizeof(buf), "%.2f", dfb);
      spin(142, "DLY FBK",  buf);

      // LIMITER toggle (full-width button)
      uint16_t lbg = lim ? THEME_SUCCESS : THEME_BG;
      tft.fillRoundRect(10, 162, 300, 22, 4, lbg);
      tft.drawRoundRect(10, 162, 300, 22, 4, THEME_OUTLINE);
      tft.setTextColor(lim ? THEME_BG : THEME_PRIMARY, lbg);
      tft.drawCentreString(lim ? "LIMITER : ON   (soft-clip always on)"
                               : "LIMITER : OFF  (soft-clip alone)",
                           160, 167, 2);
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("Tanh soft-clip stays in chain regardless.", 6, 188, 2);
      break;
    }

    case 7: { // PRST – load factory preset into track patch
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("PRESET:", 3, 106, 2);
      tft.fillRoundRect(55, 104, 22, 17, 3, THEME_BG);
      tft.drawRoundRect(55, 104, 22, 17, 3, THEME_OUTLINE);
      tft.setTextColor(THEME_PRIMARY, THEME_BG);
      tft.drawCentreString("<", 66, 106, 2);
      tft.fillRect(79, 104, 162, 17, THEME_BG);
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawCentreString(factoryPresets[seqSynthPresetIdx].name, 160, 106, 2);
      tft.fillRoundRect(243, 104, 22, 17, 3, THEME_BG);
      tft.drawRoundRect(243, 104, 22, 17, 3, THEME_OUTLINE);
      tft.setTextColor(THEME_PRIMARY, THEME_BG);
      tft.drawCentreString(">", 254, 106, 2);
      // LOAD button
      tft.fillRoundRect(10, 128, 300, 24, 4, THEME_PRIMARY);
      tft.drawRoundRect(10, 128, 300, 24, 4, THEME_OUTLINE);
      tft.setTextColor(THEME_BG, THEME_PRIMARY);
      tft.drawCentreString("LOAD INTO TRACK", 160, 134, 2);
      // Current patch name
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString("TRACK PATCH:", 3, 162, 2);
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawCentreString(p.name, 200, 162, 2);
      // APPLY NOW button
      tft.fillRoundRect(10, 178, 300, 16, 4, THEME_SECONDARY);
      tft.drawRoundRect(10, 178, 300, 16, 4, THEME_OUTLINE);
      tft.setTextColor(THEME_BG, THEME_SECONDARY);
      tft.drawCentreString("APPLY NOW  (LIVE PREVIEW)", 160, 181, 2);
      break;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Main draw dispatcher
// ─────────────────────────────────────────────────────────────────────────────
void zombieSeqDraw() {
  if (!zombieSeq) return;

  int  curStep = zombieSeq->getTrackStep(seqEditTrack);
  bool playing = zombieSeq->getIsPlaying();

  bool stepChanged    = (seqTab == 0) && (curStep != seqLastStep);
  bool playingChanged = (playing != seqLastPlaying);
  // SONG page: keep the playing-slot marker (*) live as the song advances.
  static int seqLastSongSlot = -1;
  bool songChanged = (seqTab == 5) && zombieSeq->songMode &&
                     (zombieSeq->songSlotIdx != seqLastSongSlot);
  if (songChanged) { seqLastSongSlot = zombieSeq->songSlotIdx; seqNeedsRedraw = true; }

  if (!seqNeedsRedraw && !stepChanged && !playingChanged) return;

  if (seqNeedsRedraw) {
    seqDrawHeader();
    seqDrawTabs();
    seqDrawBottom();
  } else if (playingChanged) {
    seqDrawBottom();
  }

  switch (seqTab) {
    case 0: seqDrawGrid();    break;
    case 1: seqDrawEdit();    break;
    case 2: seqDrawTrack();   break;
    case 3: seqDrawPattern(); break;
    case 4: seqDrawSynth();   break;
    case 5: seqDrawSong();    break;
  }

  seqNeedsRedraw  = false;
  seqLastStep     = curStep;
  seqLastPlaying  = playing;
}

// ─────────────────────────────────────────────────────────────────────────────
// Touch handlers
// ─────────────────────────────────────────────────────────────────────────────
static bool seqTouchBottom() {
  // PLAY/STOP
  if (isButtonPressed(1, 201, 88, 37)) {
    zombieSeq->getIsPlaying() ? zombieSeq->stop() : zombieSeq->play();
    seqNeedsRedraw = true; return true;
  }
  // BPM-
  if (isButtonPressed(90, 201, 43, 37)) {
    zombieSeq->setBPM(zombieSeq->getBPM() - 5.0f);
    seqNeedsRedraw = true; return true;
  }
  // BPM+ (skip display zone x=134..185)
  if (isButtonPressed(186, 201, 43, 37)) {
    zombieSeq->setBPM(zombieSeq->getBPM() + 5.0f);
    seqNeedsRedraw = true; return true;
  }
  // TAP
  if (isButtonPressed(230, 201, 43, 37)) {
    zombieSeq->tapTempo();
    seqNeedsRedraw = true; return true;
  }
  // FILL
  if (isButtonPressed(274, 201, 45, 37)) {
    zombieSeq->toggleFill();
    seqNeedsRedraw = true; return true;
  }
  return false;
}

static void seqTouchGrid() {
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);

  // Track buttons (y=73..93) – tap selected→mute, tap other→select
  for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
    if (isButtonPressed(1+t*79, 73, 78, 20)) {
      if (t == seqEditTrack) {
        zombieSeq->toggleMute(t);
      } else {
        seqEditTrack = t;
        zombieSeq->setActiveTrack(t);
      }
      seqNeedsRedraw = true; return;
    }
  }

  // Step grid (y=96..155)
  for (int s = 0; s < 16; s++) {
    int stepIdx = seqGridPage * 16 + s;
    int cx = 3 + (s % 8) * 39;
    int cy = (s < 8) ? 96 : 126;
    if (isButtonPressed(cx, cy, 37, 28)) {
      seqEditStep = stepIdx;
      if (tr) {
        zombieSeq->toggleStep(seqEditTrack, stepIdx);
        // Default note if newly activated
        if (tr->steps[stepIdx].active) {
          if (tr->steps[stepIdx].note == 0)
            tr->steps[stepIdx].note = 60;
        }
      }
      seqNeedsRedraw = true; return;
    }
  }

  // Page A/B (y=158..171)
  if (isButtonPressed(3, 158, 38, 14)) {
    seqGridPage = 0; seqNeedsRedraw = true; return;
  }
  if (tr && tr->length > 16 && isButtonPressed(44, 158, 38, 14)) {
    seqGridPage = 1; seqNeedsRedraw = true; return;
  }

  // REC arm toggle (y=184..198, x=118..168)
  if (isButtonPressed(118, 184, 50, 14)) {
    zombieSeq->setRecordArmed(!zombieSeq->getRecordArmed());
    zombieSeq->setRecordTrack(seqEditTrack);
    seqNeedsRedraw = true; return;
  }
  // CLK mode cycler (INT → EXT → OUT → INT) at x=172..213
  if (isButtonPressed(172, 184, 42, 14)) {
    int m = (int)zombieSeq->getClockMode();
    m = (m + 1) % 3;
    zombieSeq->setClockMode((ZombieSequencer::ClockMode)m);
    seqNeedsRedraw = true; return;
  }
}

static void seqTouchEdit() {
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;
  SequencerStep& step = tr->steps[seqEditStep];

  // STEP/PLOCK sub-tab toggle (x=275..317, y=73..90)
  if (isButtonPressed(275, 73, 42, 17)) {
    seqEditSubTab = (seqEditSubTab + 1) & 1;
    seqNeedsRedraw = true; return;
  }

  // PLOCK page touch handling
  if (seqEditSubTab == 1) {
    auto clampLock = [](int v) { return (v < -64) ? -64 : (v > 63 ? 63 : v); };
    // CUTOFF LOCK -/+ (y=110)
    if (isButtonPressed(50, 110, 22, 16)) {
      step.cutoffLock = (int8_t)clampLock((int)step.cutoffLock - 4);
      seqNeedsRedraw = true; return;
    }
    if (isButtonPressed(204, 110, 22, 16)) {
      step.cutoffLock = (int8_t)clampLock((int)step.cutoffLock + 4);
      seqNeedsRedraw = true; return;
    }
    // PAN LOCK -/+ (y=140)
    if (isButtonPressed(50, 140, 22, 16)) {
      step.panLock = (int8_t)clampLock((int)step.panLock - 4);
      seqNeedsRedraw = true; return;
    }
    if (isButtonPressed(204, 140, 22, 16)) {
      step.panLock = (int8_t)clampLock((int)step.panLock + 4);
      seqNeedsRedraw = true; return;
    }
    // CLEAR LOCKS (y=170)
    if (isButtonPressed(50, 170, 176, 16)) {
      step.cutoffLock = 0; step.panLock = 0;
      seqNeedsRedraw = true; return;
    }
    // STEP < / > still active on PLOCK page so user can scrub steps
    if (isButtonPressed(50, 73, 22, 17)) {
      seqEditStep = (seqEditStep - 1 + tr->length) % tr->length;
      seqNeedsRedraw = true; return;
    }
    if (isButtonPressed(130, 73, 22, 17)) {
      seqEditStep = (seqEditStep + 1) % tr->length;
      seqNeedsRedraw = true; return;
    }
    return;
  }

  // Step < (y=73, x=50)
  if (isButtonPressed(50, 73, 22, 17)) {
    seqEditStep = (seqEditStep - 1 + tr->length) % tr->length;
    seqNeedsRedraw = true; return;
  }
  // Step > (y=73, x=130)
  if (isButtonPressed(130, 73, 22, 17)) {
    seqEditStep = (seqEditStep + 1) % tr->length;
    seqNeedsRedraw = true; return;
  }
  // ON/OFF (x=160, y=73)
  if (isButtonPressed(160, 73, 60, 17)) {
    step.active = !step.active; seqNeedsRedraw = true; return;
  }
  // TIE (x=226)
  if (isButtonPressed(226, 73, 45, 17)) {
    step.tie = !step.tie; seqNeedsRedraw = true; return;
  }

  // NOTE - (x=50, y=92)
  if (isButtonPressed(50, 92, 22, 16)) {
    step.note = constrain(step.note - 1, 0, 127); seqNeedsRedraw = true; return;
  }
  // NOTE + (x=204, y=92)
  if (isButtonPressed(204, 92, 22, 16)) {
    step.note = constrain(step.note + 1, 0, 127); seqNeedsRedraw = true; return;
  }
  // NOTE OCT- (x=232, y=92)
  if (isButtonPressed(232, 92, 40, 16)) {
    step.note = constrain(step.note - 12, 0, 127); seqNeedsRedraw = true; return;
  }
  // NOTE OCT+ (x=276, y=92)
  if (isButtonPressed(276, 92, 40, 16)) {
    step.note = constrain(step.note + 12, 0, 127); seqNeedsRedraw = true; return;
  }

  // VEL - / + (y=110)
  if (isButtonPressed(50, 110, 22, 16)) {
    step.velocity = constrain(step.velocity - 5, 1, 127); seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(204, 110, 22, 16)) {
    step.velocity = constrain(step.velocity + 5, 1, 127); seqNeedsRedraw = true; return;
  }
  // GATE - / + (y=128)
  if (isButtonPressed(50, 128, 22, 16)) {
    step.gate = constrain(step.gate - 1, 1, 16); seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(204, 128, 22, 16)) {
    step.gate = constrain(step.gate + 1, 1, 16); seqNeedsRedraw = true; return;
  }
  // PROB - / + (y=146)
  if (isButtonPressed(50, 146, 22, 16)) {
    step.probability = (uint8_t)constrain((int)step.probability - 5, 0, 100); seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(204, 146, 22, 16)) {
    step.probability = (uint8_t)constrain((int)step.probability + 5, 0, 100); seqNeedsRedraw = true; return;
  }
  // RATCHETS x1..x4 (y=164)
  for (int r = 1; r <= 4; r++) {
    if (isButtonPressed(50+(r-1)*54, 164, 50, 16)) {
      step.ratchets = (uint8_t)r; seqNeedsRedraw = true; return;
    }
  }
  // CONDITION (y=182)
  for (int c = 0; c < COND_COUNT; c++) {
    if (isButtonPressed(50+c*44, 182, 42, 16)) {
      step.condition = (uint8_t)c; seqNeedsRedraw = true; return;
    }
  }
}

static void seqTouchTrack() {
  // Sub-tab bar
  for (int i = 0; i < 2; i++) {
    if (isButtonPressed(i*160, 72, 160, 14)) {
      seqTrackSubTab = i; seqNeedsRedraw = true; return;
    }
  }

  if (seqTrackSubTab == 1) {
    // MIX page
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
      int sy = 89 + t * 28;
      // Select track (label)
      if (isButtonPressed(2, sy, 36, 24)) {
        seqEditTrack = t;
        zombieSeq->setActiveTrack(t);
        seqNeedsRedraw = true; return;
      }
      // Mute
      if (isButtonPressed(40, sy, 24, 24)) {
        zombieSeq->toggleMute(t);
        seqNeedsRedraw = true; return;
      }
      // Solo
      if (isButtonPressed(66, sy, 24, 24)) {
        zombieSeq->toggleSolo(t);
        seqNeedsRedraw = true; return;
      }
      // Vol -
      if (isButtonPressed(94, sy, 20, 24)) {
        SequencerTrack* tr = zombieSeq->getTrack(t);
        if (tr) zombieSeq->setTrackVolume(t, tr->trackVolume - 0.05f);
        seqNeedsRedraw = true; return;
      }
      // Vol +
      if (isButtonPressed(188, sy, 20, 24)) {
        SequencerTrack* tr = zombieSeq->getTrack(t);
        if (tr) zombieSeq->setTrackVolume(t, tr->trackVolume + 0.05f);
        seqNeedsRedraw = true; return;
      }
      // Pan -
      if (isButtonPressed(212, sy, 20, 24)) {
        SequencerTrack* tr = zombieSeq->getTrack(t);
        if (tr) zombieSeq->setTrackPan(t, tr->trackPan - 0.1f);
        seqNeedsRedraw = true; return;
      }
      // Pan +
      if (isButtonPressed(296, sy, 20, 24)) {
        SequencerTrack* tr = zombieSeq->getTrack(t);
        if (tr) zombieSeq->setTrackPan(t, tr->trackPan + 0.1f);
        seqNeedsRedraw = true; return;
      }
    }
    return;
  }

  // SOUND page
  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;

  // LENGTH buttons (y=88)
  static const int lenVals[4] = {8, 16, 24, 32};
  int bw = (315 - 55) / 4;
  for (int i = 0; i < 4; i++) {
    if (isButtonPressed(55+i*bw, 88, bw-2, 17)) {
      tr->length = lenVals[i];
      seqEuclN = tr->length;
      seqNeedsRedraw = true; return;
    }
  }
  // DIVISION buttons (y=106)
  for (int i = 0; i < 4; i++) {
    if (isButtonPressed(55+i*bw, 106, bw-2, 17)) {
      tr->divIdx = i; seqNeedsRedraw = true; return;
    }
  }
  // SCALE < / > (y=124)
  if (isButtonPressed(55, 124, 22, 17)) {
    tr->scale = (tr->scale - 1 + 9) % 9; seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(243, 124, 22, 17)) {
    tr->scale = (tr->scale + 1) % 9; seqNeedsRedraw = true; return;
  }
  // ROOT < / > (y=142)
  if (isButtonPressed(55, 142, 22, 17)) {
    tr->rootNote = (tr->rootNote - 1 + 12) % 12; seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(243, 142, 22, 17)) {
    tr->rootNote = (tr->rootNote + 1) % 12; seqNeedsRedraw = true; return;
  }
  // OCTAVE < / > (y=160)
  if (isButtonPressed(55, 160, 22, 17)) {
    tr->octave = constrain(tr->octave - 1, 0, 6); seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(243, 160, 22, 17)) {
    tr->octave = constrain(tr->octave + 1, 0, 6); seqNeedsRedraw = true; return;
  }
  // SOUND PATCH < / > (y=178) – also copies preset into trackPatch
  if (isButtonPressed(55, 178, 22, 17)) {
    tr->soundPatchIdx = (tr->soundPatchIdx - 1 + NUM_FACTORY) % NUM_FACTORY;
    tr->trackPatch = factoryPresets[tr->soundPatchIdx];
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(243, 178, 22, 17)) {
    tr->soundPatchIdx = (tr->soundPatchIdx + 1) % NUM_FACTORY;
    tr->trackPatch = factoryPresets[tr->soundPatchIdx];
    seqNeedsRedraw = true; return;
  }
}

static void seqTouchPattern() {
  // Sub-tab bar (y=72..86)
  for (int i = 0; i < 2; i++) {
    if (isButtonPressed(i*160, 72, 160, 14)) {
      seqPtrnSubTab = i; seqArmedAction = 0; seqNeedsRedraw = true; return;
    }
  }

  if (seqPtrnSubTab == 0) {
    // ── BANK ──────────────────────────────────────────────────────────────
    for (int p = 0; p < MAX_PATTERNS; p++) {
      int col = p % 4, row = p / 4;
      int px = 4 + col*78, py = 89 + row*28;
      if (isButtonPressed(px, py, 76, 26)) {
        // Select (or queue, while playing) the pattern bank.
        if (zombieSeq->getIsPlaying()) zombieSeq->nextPattern = p;
        else                           zombieSeq->activePattern = p;
        seqArmedAction = 0; seqNeedsRedraw = true; return;
      }
    }
    // COPY active bank to clipboard
    if (isButtonPressed(4, 147, 150, 22)) {
      seqCopyPat = zombieSeq->activePattern; seqNeedsRedraw = true; return;
    }
    // PASTE clipboard into active bank
    if (isButtonPressed(166, 147, 150, 22) &&
        seqCopyPat >= 0 && seqCopyPat != zombieSeq->activePattern) {
      zombieSeq->copyPattern(seqCopyPat, zombieSeq->activePattern);
      seqCopyPat = -1; seqNeedsRedraw = true; return;
    }
    // SAVE active bank → its own NVS slot (one tap; non-destructive)
    if (isButtonPressed(4, 173, 150, 22)) {
      zombieSeq->savePattern(zombieSeq->activePattern);
      seqNeedsRedraw = true; return;
    }
    // LOAD active bank ← its NVS slot (2-tap; discards unsaved edits)
    if (isButtonPressed(166, 173, 150, 22)) {
      if (seqConfirm(SEQ_ARM_LOAD)) zombieSeq->loadPattern(zombieSeq->activePattern);
      seqNeedsRedraw = true; return;
    }
    return;
  }

  // ── TOOLS ───────────────────────────────────────────────────────────────
  if (isButtonPressed(70, 90, 30, 22)) {   // CHAIN <
    zombieSeq->nextPattern = (zombieSeq->nextPattern <= 0) ? -1 : zombieSeq->nextPattern - 1;
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(224, 90, 30, 22)) {  // CHAIN >
    zombieSeq->nextPattern = constrain(zombieSeq->nextPattern + 1, 0, MAX_PATTERNS-1);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(4, 118, 150, 22)) {  // RND TRACK
    zombieSeq->randomizeTrack(seqEditTrack); seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(166, 118, 150, 22)) {// RND ALL
    for (int t = 0; t < MAX_SEQ_TRACKS; t++) zombieSeq->randomizeTrack(t);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(4, 146, 150, 22)) {  // CLR PATTERN (2-tap)
    if (seqConfirm(SEQ_ARM_CLRPAT))
      for (int t = 0; t < MAX_SEQ_TRACKS; t++) zombieSeq->clearTrack(t);
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(4, 174, 24, 22)) {   // ARP <
    seqArpPatIdx = (seqArpPatIdx - 1 + NUM_ARP_PATTERNS) % NUM_ARP_PATTERNS;
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(200, 174, 24, 22)) { // ARP >
    seqArpPatIdx = (seqArpPatIdx + 1) % NUM_ARP_PATTERNS;
    seqNeedsRedraw = true; return;
  }
  if (isButtonPressed(228, 174, 88, 22)) { // FILL TRK
    seqDoArpFill(seqEditTrack, seqArpPatIdx);
    seqNeedsRedraw = true; return;
  }
}

static void seqTouchSynth() {
  // Track selector (y=73..86)
  for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
    if (isButtonPressed(1+t*79, 73, 78, 14)) {
      seqEditTrack = t;
      zombieSeq->setActiveTrack(t);
      seqNeedsRedraw = true; return;
    }
  }
  // Sub-tab (y=88..101)
  for (int i = 0; i < SEQ_NUM_SSTABS; i++) {
    if (isButtonPressed(i*SEQ_SSTAB_W, 88, SEQ_SSTAB_W, 14)) {
      seqSynthSubTab = i; seqNeedsRedraw = true; return;
    }
  }

  SequencerTrack* tr = zombieSeq->getTrack(seqEditTrack);
  if (!tr) return;
  SynthPatch& p = tr->trackPatch;

  // Two-speed envelope step helper
  auto envStep = [](float v, bool up) -> float {
    float s = (v < 0.05f) ? 0.005f : 0.05f;
    return constrain(up ? v + s : v - s, 0.001f, 5.0f);
  };

  switch (seqSynthSubTab) {
    case 0: { // OSC
      if (isButtonPressed(55,104,22,17)) { p.osc1Wave=(p.osc1Wave-1+5)%5; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)){ p.osc1Wave=(p.osc1Wave+1)%5;   seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,123,22,17)) { p.osc1Level=constrain(p.osc1Level-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.osc1Level=constrain(p.osc1Level+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.osc2Wave=(p.osc2Wave-1+5)%5; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.osc2Wave=(p.osc2Wave+1)%5;   seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,161,22,17)) { p.osc2Level=constrain(p.osc2Level-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,161,22,17)){ p.osc2Level=constrain(p.osc2Level+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,180,22,17)) { p.osc2Detune=constrain(p.osc2Detune-0.001f,0.0f,0.05f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,180,22,17)){ p.osc2Detune=constrain(p.osc2Detune+0.001f,0.0f,0.05f); seqNeedsRedraw=true; return; }
      break;
    }
    case 1: { // FLT
      for (int i = 0; i < 4; i++) {
        if (isButtonPressed(55+i*66,104,62,17)) { p.filterType=i; seqNeedsRedraw=true; return; }
      }
      if (isButtonPressed(55,123,22,17)) { p.filterCutoff=constrain(p.filterCutoff-0.02f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.filterCutoff=constrain(p.filterCutoff+0.02f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.filterResonance=constrain(p.filterResonance-0.05f,0.0f,0.95f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.filterResonance=constrain(p.filterResonance+0.05f,0.0f,0.95f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,161,22,17)) { p.filterEnvAmount=constrain(p.filterEnvAmount-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,161,22,17)){ p.filterEnvAmount=constrain(p.filterEnvAmount+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,180,22,17)) { p.masterVolume=constrain(p.masterVolume-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,180,22,17)){ p.masterVolume=constrain(p.masterVolume+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      break;
    }
    case 2: { // SUB osc
      if (isButtonPressed(55,104,22,17)) { p.subWave=(p.subWave-1+5)%5; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)){ p.subWave=(p.subWave+1)%5;   seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,123,22,17)) { p.subLevel=constrain(p.subLevel-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.subLevel=constrain(p.subLevel+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.subOctave=(p.subOctave<=1)?2:1; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.subOctave=(p.subOctave<=1)?2:1; seqNeedsRedraw=true; return; }
      break;
    }
    case 3: { // AMP envelope
      if (isButtonPressed(55,104,22,17)) { p.ampAttack =envStep(p.ampAttack, false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)){ p.ampAttack =envStep(p.ampAttack, true);  seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,123,22,17)) { p.ampDecay  =envStep(p.ampDecay,  false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.ampDecay  =envStep(p.ampDecay,  true);  seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.ampSustain=constrain(p.ampSustain-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.ampSustain=constrain(p.ampSustain+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,161,22,17)) { p.ampRelease=envStep(p.ampRelease,false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,161,22,17)){ p.ampRelease=envStep(p.ampRelease,true);  seqNeedsRedraw=true; return; }
      break;
    }
    case 4: { // Filter envelope
      if (isButtonPressed(55,104,22,17)) { p.filterAttack =envStep(p.filterAttack, false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)){ p.filterAttack =envStep(p.filterAttack, true);  seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,123,22,17)) { p.filterDecay  =envStep(p.filterDecay,  false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.filterDecay  =envStep(p.filterDecay,  true);  seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.filterSustain=constrain(p.filterSustain-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.filterSustain=constrain(p.filterSustain+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,161,22,17)) { p.filterRelease=envStep(p.filterRelease,false); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,161,22,17)){ p.filterRelease=envStep(p.filterRelease,true);  seqNeedsRedraw=true; return; }
      break;
    }
    case 5: { // LFO
      if (isButtonPressed(55,104,22,17)) { p.lfoWave=(p.lfoWave-1+5)%5; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)){ p.lfoWave=(p.lfoWave+1)%5;   seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,123,22,17)) { p.lfoRate=constrain(p.lfoRate-0.1f,0.0f,20.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,123,22,17)){ p.lfoRate=constrain(p.lfoRate+0.1f,0.0f,20.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,142,22,17)) { p.lfoDepth=constrain(p.lfoDepth-0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,142,22,17)){ p.lfoDepth=constrain(p.lfoDepth+0.05f,0.0f,1.0f); seqNeedsRedraw=true; return; }
      if (isButtonPressed(55,161,22,17)) { p.lfoTarget=(p.lfoTarget-1+3)%3; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,161,22,17)){ p.lfoTarget=(p.lfoTarget+1)%3; seqNeedsRedraw=true; return; }
      break;
    }
    case 6: { // FX
      SynthEngine* synth = getZombieSynth();
      if (!synth) break;
      if (isButtonPressed(55, 104, 22, 17)) {
        int d = (synth->getDelayDivision() - 1 + 4) & 3;
        synth->setDelayDivision(zombieSeq->getBPM(), d); seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(243, 104, 22, 17)) {
        int d = (synth->getDelayDivision() + 1) & 3;
        synth->setDelayDivision(zombieSeq->getBPM(), d); seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(55, 123, 22, 17)) {
        synth->setDelayMix(constrain(synth->getDelayMix() - 0.05f, 0.0f, 1.0f));
        seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(243, 123, 22, 17)) {
        synth->setDelayMix(constrain(synth->getDelayMix() + 0.05f, 0.0f, 1.0f));
        seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(55, 142, 22, 17)) {
        synth->setDelayFeedback(constrain(synth->getDelayFeedback() - 0.05f, 0.0f, 0.85f));
        seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(243, 142, 22, 17)) {
        synth->setDelayFeedback(constrain(synth->getDelayFeedback() + 0.05f, 0.0f, 0.85f));
        seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(10, 162, 300, 22)) {
        synth->setLimiterEnabled(!synth->getLimiterEnabled());
        seqNeedsRedraw = true; return;
      }
      break;
    }
    case 7: { // PRST
      if (isButtonPressed(55,104,22,17))  { seqSynthPresetIdx=(seqSynthPresetIdx-1+NUM_FACTORY)%NUM_FACTORY; seqNeedsRedraw=true; return; }
      if (isButtonPressed(243,104,22,17)) { seqSynthPresetIdx=(seqSynthPresetIdx+1)%NUM_FACTORY;             seqNeedsRedraw=true; return; }
      if (isButtonPressed(10,128,300,24)) {
        tr->trackPatch    = factoryPresets[seqSynthPresetIdx];
        tr->soundPatchIdx = seqSynthPresetIdx;
        seqNeedsRedraw = true; return;
      }
      if (isButtonPressed(10,178,300,16)) {
        SynthEngine* synth = getZombieSynth();
        if (synth) synth->applyPatch(tr->trackPatch);
        seqNeedsRedraw = true; return;
      }
      break;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Public interface
// ─────────────────────────────────────────────────────────────────────────────
void zombieSeqHandleTouch() {
  if (!touch.justPressed || !zombieSeq) return;

  // BACK
  if (isButtonPressed(5, 5, 55, 20)) { exitToMenu(); return; }

  // Tab switches (6 tabs × 53 px)
  for (int i = 0; i < SEQ_NUM_TABS; i++) {
    if (isButtonPressed(i*SEQ_TAB_W, 50, SEQ_TAB_W, 22)) {
      if (i != seqTab) {
        seqTab = i; seqNeedsRedraw = true;
        seqArmedAction = 0;   // leaving a page disarms any pending "SURE?"
        tft.fillRect(0, 72, 320, 128, THEME_BG);
      }
      return;
    }
  }

  // Bottom bar (always active)
  if (seqTouchBottom()) return;

  // Tab content
  switch (seqTab) {
    case 0: seqTouchGrid();    break;
    case 1: seqTouchEdit();    break;
    case 2: seqTouchTrack();   break;
    case 3: seqTouchPattern(); break;
    case 4: seqTouchSynth();   break;
    case 5: seqTouchSong();    break;
  }
}

void zombieSeqUpdate() {
  if (!zombieSeq) return;
  zombieSeq->update(millis());

  // Hold-to-repeat on the bottom-bar BPM-/+ buttons: after a 400 ms hold,
  // step 5 BPM every 120 ms (the initial tap is handled by seqTouchBottom).
  static unsigned long bpmHoldNextMs = 0;
  if (touch.isPressed && !touch.justPressed) {
    bool minus = isButtonPressed(90, 201, 43, 37);
    bool plus  = isButtonPressed(186, 201, 43, 37);
    if (minus || plus) {
      unsigned long now = millis();
      if (bpmHoldNextMs == 0) {
        bpmHoldNextMs = now + 400;
      } else if ((long)(now - bpmHoldNextMs) >= 0) {
        zombieSeq->setBPM(zombieSeq->getBPM() + (plus ? 5.0f : -5.0f));
        bpmHoldNextMs = now + 120;
        seqNeedsRedraw = true;
      }
    } else {
      bpmHoldNextMs = 0;
    }
  } else if (!touch.isPressed) {
    bpmHoldNextMs = 0;
  }
}

// Declared in the main .ino — emits a single MIDI byte over Serial2 TX.
extern void seqMidiSendByte(uint8_t b);

void zombieSeqInit() {
  bool firstInit = false;
  if (zombieSeq == NULL) {
    zombieSeq = new ZombieSequencer();
    zombieSeq->setSynthEngine(getZombieSynth());
    zombieSeq->setCallbacks(seqNoteOnHandler, seqNoteOffHandler);
    zombieSeq->setMidiSendByteCallback(seqMidiSendByte);
    firstInit = true;
  }

  zombieSeq->setActiveTrack(0);
  if (firstInit) {
    zombieSeq->setBPM(120.0f);
    // Try to mount SD card (silently no-op if USE_SD_STORAGE undefined)
    zombieSeq->initSDStorage();
    // Assign distinct factory presets to every track in every pattern.
    // Only on first construction so that subsequent entries preserve any
    // patterns the user has edited or loaded from NVS.
    for (int p = 0; p < MAX_PATTERNS; p++) {
      for (int t = 0; t < MAX_SEQ_TRACKS; t++) {
        SequencerTrack& tr = zombieSeq->patterns[p].tracks[t];
        int pIdx = t % NUM_FACTORY;
        tr.soundPatchIdx = pIdx;
        tr.trackPatch    = factoryPresets[pIdx];
      }
    }
  }

  seqTab            = 0;
  seqEditTrack      = 0;
  seqEditStep       = 0;
  seqGridPage       = 0;
  seqNeedsRedraw    = true;
  seqLastStep       = -1;
  seqLastPlaying    = false;
  seqCopyPat        = -1;
  seqEuclK          = 4;
  seqEuclN          = 16;
  seqArpPatIdx      = 0;
  seqSynthSubTab    = 0;
  seqSynthPresetIdx = 0;
  seqTrackSubTab    = 0;
  seqPtrnSubTab     = 0;
  seqArmedAction    = 0;
  seqPendingSongAction = false;
  songVisibleStart  = 0;

  tft.fillScreen(THEME_BG);
}

ZombieSequencer* getZombieSeq() {
  return zombieSeq;
}

#endif

// WerewolfAmyS3 — GM sequencer page (16 tracks x 16 steps, Medusa engine)
// One track per GM channel; track 10 is drums (step notes are drum keys).
#pragma once
#include "ui_common.h"
#include "gm_seq.h"
#include "gm_engine.h"
#include "clock.h"
#include "storage.h"

static bool gseqDirty = true;
static int  gseqSelStep = -1;
static bool gseqSelMode = false;
static uint8_t gseqLastDrawnStep = 255;

static const int GSEQ_GRID_X = 4, GSEQ_GRID_Y = 92;
static const int GSEQ_CELL_W = 19, GSEQ_CELL_H = 26, GSEQ_CELL_GAP = 1;

inline void gseqDrawCell(int s) {
  GTrack& t = gmSeq.trk();
  int x = GSEQ_GRID_X + s * (GSEQ_CELL_W + GSEQ_CELL_GAP);
  bool inLen = s < t.length;
  uint16_t fill = !inLen ? THEME_BG
                 : t.steps[s].on
                    ? (t.steps[s].accent ? THEME_STEP_ACC
                       : t.steps[s].slide ? THEME_SEL : THEME_STEP_ON)
                    : THEME_STEP_OFF;
  tft.fillRect(x, GSEQ_GRID_Y, GSEQ_CELL_W, GSEQ_CELL_H, fill);
  uint16_t border = (s == gseqSelStep) ? THEME_SEL
                    : (gmSeq.playing && s == gmSeq.playStep) ? THEME_CURSOR
                    : THEME_GRID;
  tft.drawRect(x, GSEQ_GRID_Y, GSEQ_CELL_W, GSEQ_CELL_H, border);
  if ((s & 3) == 0) tft.drawFastVLine(x - 1, GSEQ_GRID_Y, GSEQ_CELL_H, THEME_OUTLINE);
}

inline void gseqInit() { tft.fillScreen(THEME_BG); gseqDirty = true; gseqLastDrawnStep = 255; }

inline void gseqLoop() {
  GPattern& p = gmSeq.pat();
  GTrack& t = gmSeq.trk();
  bool drums = (gmSeq.editTrack == GM_DRUM_CHANNEL);
  bool rd = gseqDirty;
  char buf[24];
  int hit;

  if (rd) {
    gseqDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("GM SEQ", true);

    // 16 track tabs in two rows of 8
    for (int i = 0; i < GMSEQ_TRACKS; i++) {
      int x = 4 + (i % 8) * 39, y = 33 + (i / 8) * 21;
      snprintf(buf, sizeof(buf), "%d%s", i + 1, p.tracks[i].mute ? "-" : "");
      drawButton(x, y, 36, 19, buf,
                 i == GM_DRUM_CHANNEL ? THEME_ACCENT : THEME_PRIMARY,
                 (int)gmSeq.editTrack == i);
    }
    // program / name row
    tft.setTextColor(THEME_TEXT, THEME_BG);
    if (drums) tft.drawString("DRUMS (CH 10)", 8, 78 - 4, 2);
    for (int s = 0; s < GMSEQ_STEPS; s++) gseqDrawCell(s);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  // playhead refresh
  if (gmSeq.playing && gmSeq.playStep != gseqLastDrawnStep) {
    uint8_t old = gseqLastDrawnStep;
    gseqLastDrawnStep = gmSeq.playStep;
    if (old < GMSEQ_STEPS) gseqDrawCell(old);
    gseqDrawCell(gmSeq.playStep);
  }

  // track select / mute-on-reselect
  for (int i = 0; i < GMSEQ_TRACKS; i++) {
    int x = 4 + (i % 8) * 39, y = 33 + (i / 8) * 21;
    if (tapped(x, y, 36, 19)) {
      if (i == (int)gmSeq.editTrack) p.tracks[i].mute = !p.tracks[i].mute;
      else { gmSeq.editTrack = i; gseqSelStep = -1; }
      gseqDirty = true;
      return;
    }
  }

  // program spinner (melodic tracks)
  if (!drums) {
    snprintf(buf, sizeof(buf), "%03d %s", t.program, gm.programName(t.program));
    hit = spinner(4, 74, 230, 18, buf, rd);
    if (hit) {
      t.program = (t.program + hit + 128) & 127;
      router.send(SRC_GMSEQ, 0xC0 | gmSeq.editTrack, t.program, 0);
      gseqDirty = true;
    }
  }
  snprintf(buf, sizeof(buf), "P%d", gmSeq.patIdx + 1);
  hit = spinner(238, 74, 78, 18, buf, rd);
  if (hit) {
    gmSeq.patIdx = constrain((int)gmSeq.patIdx + hit, 0, GMSEQ_PATTERNS - 1);
    gmSeq.applyPrograms();
    gseqDirty = true;
    return;
  }

  // step grid
  if (touch.justPressed && touch.y >= GSEQ_GRID_Y && touch.y < GSEQ_GRID_Y + GSEQ_CELL_H) {
    int s = (touch.x - GSEQ_GRID_X) / (GSEQ_CELL_W + GSEQ_CELL_GAP);
    if (s >= 0 && s < GMSEQ_STEPS) {
      if (gseqSelMode) {
        int old = gseqSelStep; gseqSelStep = s;
        if (old >= 0) gseqDrawCell(old);
      } else {
        t.steps[s].on = !t.steps[s].on;
        gseqSelStep = s;
      }
      gseqDrawCell(s);
      gseqDirty = true;
      return;
    }
  }

  // ── step edit rows ─────────────────────────────────────────────────────────
  int y = 126;
  GStep* sel = (gseqSelStep >= 0) ? &t.steps[gseqSelStep] : nullptr;
  if (rd) drawButton(4, y, 52, 22, gseqSelMode ? "SEL" : "TOGL", THEME_WARN, gseqSelMode);
  if (tapped(4, y, 52, 22)) { gseqSelMode = !gseqSelMode; gseqDirty = true; }

  if (sel && drums) snprintf(buf, sizeof(buf), "%s", gmDrumName(sel->note));
  else if (sel) { char nb[8]; snprintf(buf, sizeof(buf), "%s", noteName(sel->note, nb)); }
  else snprintf(buf, sizeof(buf), "--");
  hit = spinner(60, y, drums ? 118 : 70, 22, buf, rd);
  if (hit && sel) { sel->note = constrain(sel->note + hit, drums ? 35 : 12, drums ? 81 : 120); gseqDirty = true; }

  snprintf(buf, sizeof(buf), "V%d", sel ? sel->vel : 0);
  hit = spinner(drums ? 182 : 134, y, 58, 22, buf, rd);
  if (hit && sel) { sel->vel = constrain(sel->vel + hit * 5, 1, 127); gseqDirty = true; }
  snprintf(buf, sizeof(buf), "G%d", sel ? sel->gate : 0);
  hit = spinner(244, y, 72, 22, buf, rd);
  if (hit && sel) { sel->gate = constrain(sel->gate + hit * 5, 5, 100); gseqDirty = true; }

  y = 152;
  if (rd) {
    drawButton(4, y, 56, 22, "ACC", THEME_ACCENT, sel && sel->accent);
    drawButton(64, y, 56, 22, "SLIDE", THEME_SEL, sel && sel->slide);
  }
  if (tapped(4, y, 56, 22) && sel) { sel->accent = !sel->accent; gseqDirty = true; }
  if (tapped(64, y, 56, 22) && sel) { sel->slide = !sel->slide; gseqDirty = true; }
  snprintf(buf, sizeof(buf), "%d%%", sel ? sel->prob : 0);
  hit = spinner(124, y, 62, 22, buf, rd);
  if (hit && sel) { sel->prob = constrain(sel->prob + hit * 10, 10, 100); gseqDirty = true; }
  snprintf(buf, sizeof(buf), "LEN%d", t.length);
  hit = spinner(190, y, 62, 22, buf, rd);
  if (hit) { t.length = constrain(t.length + hit, 1, GMSEQ_STEPS); gseqDirty = true; }
  snprintf(buf, sizeof(buf), "SW%d", p.swing);
  hit = spinner(256, y, 60, 22, buf, rd);
  if (hit) { p.swing = constrain(p.swing + hit * 5, 0, 50); gseqDirty = true; }

  // ── ops / transport / chain ───────────────────────────────────────────────
  y = 180;
  if (rd) {
    drawButton(4, y, 52, 24, "CLEAR", THEME_OUTLINE);
    drawButton(60, y, 52, 24, "RAND", THEME_OUTLINE);
    drawButton(116, y, 52, 24, "SAVE", THEME_WARN);
    drawButton(172, y, 68, 24, gmSeq.playing ? "STOP" : "PLAY",
               gmSeq.playing ? THEME_PRIMARY : THEME_GOOD, gmSeq.playing);
  }
  if (tapped(4, y, 52, 24)) { gmSeq.clearTrack(gmSeq.editTrack); gseqDirty = true; }
  if (tapped(60, y, 52, 24)) { gmSeq.randomizeTrack(gmSeq.editTrack); gseqDirty = true; }
  if (tapped(116, y, 52, 24)) storage.saveGMSeq(gmSeq);
  if (tapped(172, y, 68, 24)) { gmSeq.toggle(masterClock.tick); gseqDirty = true; }
  snprintf(buf, sizeof(buf), "%d", masterClock.bpm);
  hit = spinner(244, y, 72, 24, buf, rd);
  if (hit) { masterClock.nudgeBPM(hit); gseqDirty = true; }

  // chain editor: 4 slots of pattern x repeats  (pattern 0 = "--" end)
  y = 210;
  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("CHAIN", 4, y + 6, 2); }
  for (int c = 0; c < 4; c++) {
    GChainEntry& e = gmSeq.chain[c];
    if (e.patternIdx < 0) snprintf(buf, sizeof(buf), "--");
    else snprintf(buf, sizeof(buf), "%dx%d", e.patternIdx + 1, e.repeats);
    hit = spinner(56 + c * 66, y, 62, 24, buf, rd);
    if (hit) {
      // cycle: -- , P1x1..P8x1, then repeats via second tap direction on same pattern
      int code = (e.patternIdx < 0) ? 0 : 1 + e.patternIdx * 4 + (e.repeats - 1);
      code = constrain(code + hit, 0, GMSEQ_PATTERNS * 4);
      if (code == 0) { e.patternIdx = -1; e.repeats = 1; }
      else { e.patternIdx = (code - 1) / 4; e.repeats = 1 + (code - 1) % 4; }
      uint8_t len = 0;
      while (len < GMSEQ_CHAIN_LEN && gmSeq.chain[len].patternIdx >= 0) len++;
      gmSeq.chainLen = len;
      gseqDirty = true;
    }
  }
}

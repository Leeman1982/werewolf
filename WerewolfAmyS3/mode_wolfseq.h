// WerewolfAmyS3 — WOLF sequencer page (4 tracks x 16 steps)
// Tap a step to toggle; tap-and-hold shows nothing fancy — select a step with
// SEL mode to edit note/vel/gate with the spinners.
#pragma once
#include "ui_common.h"
#include "wolf_seq.h"
#include "clock.h"
#include "storage.h"

static bool wseqDirty = true;
static int  wseqSelStep = -1;      // selected step for editing (-1 none)
static bool wseqSelMode = false;   // false: tap toggles, true: tap selects
static uint8_t wseqLastDrawnStep = 255;

static const int WSEQ_GRID_X = 4, WSEQ_GRID_Y = 64;
static const int WSEQ_CELL_W = 19, WSEQ_CELL_H = 26, WSEQ_CELL_GAP = 1;

inline void wseqDrawCell(int s) {
  WTrack& t = wolfSeq.trk();
  int x = WSEQ_GRID_X + s * (WSEQ_CELL_W + WSEQ_CELL_GAP);
  uint16_t fill = t.steps[s].on
                    ? (t.steps[s].accent ? THEME_STEP_ACC : THEME_STEP_ON)
                    : THEME_STEP_OFF;
  tft.fillRect(x, WSEQ_GRID_Y, WSEQ_CELL_W, WSEQ_CELL_H, fill);
  uint16_t border = (s == wseqSelStep) ? THEME_SEL
                    : (wolfSeq.playing && s == wolfSeq.playStep) ? THEME_CURSOR
                    : THEME_GRID;
  tft.drawRect(x, WSEQ_GRID_Y, WSEQ_CELL_W, WSEQ_CELL_H, border);
  if ((s & 3) == 0) tft.drawFastVLine(x - 1, WSEQ_GRID_Y, WSEQ_CELL_H, THEME_OUTLINE);
}

inline void wseqInit() { tft.fillScreen(THEME_BG); wseqDirty = true; wseqLastDrawnStep = 255; }

inline void wseqLoop() {
  WPattern& p = wolfSeq.pat();
  WTrack& t = wolfSeq.trk();
  bool rd = wseqDirty;
  char buf[20];
  int hit;

  if (rd) {
    wseqDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("WOLF SEQ", true);
    // track buttons
    for (int i = 0; i < WSEQ_TRACKS; i++) {
      snprintf(buf, sizeof(buf), "T%d%s", i + 1, p.tracks[i].mute ? "-" : "");
      drawButton(4 + i * 44, 34, 40, 24, buf,
                 p.tracks[i].mute ? THEME_TEXT_DIM : THEME_PRIMARY, i == wolfSeq.editTrack);
    }
    // pattern spinner + play
    snprintf(buf, sizeof(buf), "P%d", wolfSeq.patIdx + 1);
    spinner(184, 34, 64, 24, buf, true);
    drawButton(252, 34, 64, 24, wolfSeq.playing ? "STOP" : "PLAY",
               wolfSeq.playing ? THEME_PRIMARY : THEME_GOOD, wolfSeq.playing);
    for (int s = 0; s < WSEQ_STEPS; s++) wseqDrawCell(s);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  // playhead refresh
  if (wolfSeq.playing && wolfSeq.playStep != wseqLastDrawnStep) {
    uint8_t old = wseqLastDrawnStep;
    wseqLastDrawnStep = wolfSeq.playStep;
    if (old < WSEQ_STEPS) wseqDrawCell(old);
    wseqDrawCell(wolfSeq.playStep);
  }

  // track select (tap) / mute (tap selected track again with SEL on? use shift-free: tap selected = mute)
  for (int i = 0; i < WSEQ_TRACKS; i++)
    if (tapped(4 + i * 44, 34, 40, 24)) {
      if (i == (int)wolfSeq.editTrack) p.tracks[i].mute = !p.tracks[i].mute;
      else wolfSeq.editTrack = i;
      wseqDirty = true;
      return;
    }

  snprintf(buf, sizeof(buf), "P%d", wolfSeq.patIdx + 1);
  hit = spinner(184, 34, 64, 24, buf, false);
  if (hit) {
    wolfSeq.patIdx = constrain((int)wolfSeq.patIdx + hit, 0, WSEQ_PATTERNS - 1);
    wseqDirty = true;
    return;
  }
  if (tapped(252, 34, 64, 24)) { wolfSeq.toggle(masterClock.tick); wseqDirty = true; return; }

  // step grid taps
  if (touch.justPressed && touch.y >= WSEQ_GRID_Y && touch.y < WSEQ_GRID_Y + WSEQ_CELL_H) {
    int s = (touch.x - WSEQ_GRID_X) / (WSEQ_CELL_W + WSEQ_CELL_GAP);
    if (s >= 0 && s < WSEQ_STEPS) {
      if (wseqSelMode) {
        int old = wseqSelStep;
        wseqSelStep = s;
        if (old >= 0) wseqDrawCell(old);
      } else {
        t.steps[s].on = !t.steps[s].on;
        wseqSelStep = s;
      }
      wseqDrawCell(s);
      wseqDirty = true;   // refresh step-edit readouts
      return;
    }
  }

  // ── step edit row ──────────────────────────────────────────────────────────
  int y = 100;
  WStep* sel = (wseqSelStep >= 0) ? &t.steps[wseqSelStep] : nullptr;
  if (rd) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    snprintf(buf, sizeof(buf), "STEP %d", wseqSelStep + 1);
    tft.drawString(sel ? buf : "STEP --", 4, y, 2);
    drawButton(70, y - 4, 52, 22, wseqSelMode ? "SEL" : "TOGL", THEME_WARN, wseqSelMode);
  }
  if (tapped(70, y - 4, 52, 22)) { wseqSelMode = !wseqSelMode; wseqDirty = true; }

  char nb[8];
  snprintf(buf, sizeof(buf), "%s", sel ? noteName(sel->note, nb) : "--");
  hit = spinner(128, y - 4, 62, 22, buf, rd);
  if (hit && sel) { sel->note = constrain(sel->note + hit, 12, 120); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "V%d", sel ? sel->vel : 0);
  hit = spinner(194, y - 4, 60, 22, buf, rd);
  if (hit && sel) { sel->vel = constrain(sel->vel + hit * 5, 1, 127); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "G%d%%", sel ? sel->gate : 0);
  hit = spinner(258, y - 4, 60, 22, buf, rd);
  if (hit && sel) { sel->gate = constrain(sel->gate + hit * 5, 5, 100); wseqDirty = true; }

  // accent + prob row
  if (rd) drawButton(4, y + 24, 60, 22, "ACCENT", THEME_ACCENT, sel && sel->accent);
  if (tapped(4, y + 24, 60, 22) && sel) { sel->accent = !sel->accent; wseqDirty = true; }
  snprintf(buf, sizeof(buf), "%d%%", sel ? sel->prob : 0);
  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("PROB", 70, y + 27, 2); }
  hit = spinner(110, y + 24, 64, 22, buf, rd);
  if (hit && sel) { sel->prob = constrain(sel->prob + hit * 10, 10, 100); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "CH%d", t.channel);
  hit = spinner(194, y + 24, 60, 22, buf, rd);
  if (hit) { t.channel = constrain(t.channel + hit, 1, 16); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "SW%d", p.swing);
  hit = spinner(258, y + 24, 60, 22, buf, rd);
  if (hit) { p.swing = constrain(p.swing + hit * 5, 0, 50); wseqDirty = true; }

  // ── track ops + tempo ─────────────────────────────────────────────────────
  y = 158;
  if (rd) {
    drawButton(4, y, 56, 24, "CLEAR", THEME_OUTLINE);
    drawButton(64, y, 56, 24, "RAND", THEME_OUTLINE);
    drawButton(124, y, 40, 24, "<<", THEME_OUTLINE);
    drawButton(168, y, 40, 24, ">>", THEME_OUTLINE);
    drawButton(212, y, 46, 24, "REV", THEME_OUTLINE);
    drawButton(262, y, 54, 24, "SAVE", THEME_WARN);
  }
  if (tapped(4, y, 56, 24)) { wolfSeq.clearTrack(wolfSeq.editTrack); wseqDirty = true; }
  if (tapped(64, y, 56, 24)) { wolfSeq.randomizeTrack(wolfSeq.editTrack); wseqDirty = true; }
  if (tapped(124, y, 40, 24)) { wolfSeq.shiftTrack(wolfSeq.editTrack, -1); wseqDirty = true; }
  if (tapped(168, y, 40, 24)) { wolfSeq.shiftTrack(wolfSeq.editTrack, +1); wseqDirty = true; }
  if (tapped(212, y, 46, 24)) { wolfSeq.reverseTrack(wolfSeq.editTrack); wseqDirty = true; }
  if (tapped(262, y, 54, 24)) storage.saveWolfSeq(wolfSeq);

  y = 190;
  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("BPM", 4, y + 4, 2); }
  snprintf(buf, sizeof(buf), "%d", masterClock.bpm);
  hit = spinner(40, y, 80, 24, buf, rd);
  if (hit) { masterClock.nudgeBPM(hit); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "LEN %d", p.length);
  hit = spinner(130, y, 84, 24, buf, rd);
  if (hit) { p.length = constrain(p.length + hit, 1, WSEQ_STEPS); wseqDirty = true; }
  snprintf(buf, sizeof(buf), "TR%+d", t.transpose);
  hit = spinner(220, y, 80, 24, buf, rd);
  if (hit) { t.transpose = constrain(t.transpose + hit, -24, 24); wseqDirty = true; }
}

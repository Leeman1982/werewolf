#ifndef ZOMBIE_ARP_MODE_H
#define ZOMBIE_ARP_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "arpeggiator_patterns.h"
#include "zombie_synth_mode.h"

// ZOMBIE SS Arpeggiator UI with 50 patterns + BPM controls

static Arpeggiator* zombieArp = NULL;
static int arpPatternPage = 0;
static bool arpRunning = false;
static bool arpNeedsRedraw = true;

void zombieArpInit() {
  if (zombieArp == NULL) {
    zombieArp = new Arpeggiator();
  }

  zombieArp->setBPM(120.0f);
  zombieArp->setPattern(ARP_UP);
  zombieArp->setOctaveRange(2);
  zombieArp->setGateLength(80);

  arpPatternPage = 0;
  arpRunning = false;
  arpNeedsRedraw = true;

  tft.fillScreen(THEME_BG);
}

void zombieArpDraw() {
  if (!arpNeedsRedraw) return;

  // Header
  drawZombiHeader("ARPEGGIATOR");

  // ── BPM row (y=53..83) ──────────────────────────────────────────────
  tft.fillRect(0, 53, 320, 32, THEME_BG);

  // < 10 button
  tft.fillRoundRect(5, 55, 52, 28, 4, THEME_BG);
  tft.drawRoundRect(5, 55, 52, 28, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("-10", 31, 63, 2);

  // < 1 button
  tft.fillRoundRect(62, 55, 40, 28, 4, THEME_BG);
  tft.drawRoundRect(62, 55, 40, 28, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("-1", 82, 63, 2);

  // BPM value (large, centre)
  char bpmBuf[16];
  sprintf(bpmBuf, "%.0f BPM", zombieArp->getBPM());
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.fillRect(105, 54, 110, 30, THEME_BG);
  tft.drawCentreString(bpmBuf, 160, 60, 4);

  // +1 button
  tft.fillRoundRect(218, 55, 40, 28, 4, THEME_BG);
  tft.drawRoundRect(218, 55, 40, 28, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("+1", 238, 63, 2);

  // +10 button
  tft.fillRoundRect(263, 55, 52, 28, 4, THEME_BG);
  tft.drawRoundRect(263, 55, 52, 28, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("+10", 289, 63, 2);

  // ── Status line (y=88..103) ─────────────────────────────────────────
  tft.fillRect(0, 88, 320, 16, THEME_BG);
  char statusBuf[50];
  sprintf(statusBuf, "NOTES:%d  |  %s",
          zombieArp->getNoteCount(),
          arpRunning ? "RUNNING" : "STOPPED");
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString(statusBuf, 160, 89, 2);

  // ── Pattern list - 5 patterns per page (y=107..197) ─────────────────
  tft.fillRect(0, 105, 320, 95, THEME_BG);
  int startPattern = arpPatternPage * 5;
  for (int i = 0; i < 5; i++) {
    int patternIdx = startPattern + i;
    if (patternIdx >= NUM_ARP_PATTERNS) break;

    int x = 8, y = 107 + i * 18, w = 304, h = 16;

    bool selected = (patternIdx == (int)zombieArp->getPattern());
    uint16_t bgColor  = selected ? THEME_PRIMARY : THEME_BG;
    uint16_t txtColor = selected ? THEME_BG : THEME_PRIMARY;

    tft.fillRoundRect(x, y, w, h, 2, bgColor);
    tft.drawRoundRect(x, y, w, h, 2, THEME_OUTLINE);
    tft.setTextColor(txtColor, bgColor);
    char buf[50];
    sprintf(buf, "%02d: %s", patternIdx, arpPatternNames[patternIdx]);
    tft.drawString(buf, x + 8, y + 2, 2);
  }

  // ── Bottom row: page nav + play (y=203..237) ────────────────────────
  tft.fillRect(0, 200, 320, 40, THEME_BG);

  // < PG
  tft.fillRoundRect(5, 204, 55, 30, 4, THEME_BG);
  tft.drawRoundRect(5, 204, 55, 30, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("< PG", 32, 212, 2);

  // Page indicator
  char pageBuf[20];
  sprintf(pageBuf, "PG %d/10", arpPatternPage + 1);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(pageBuf, 150, 212, 2);

  // > PG
  tft.fillRoundRect(200, 204, 55, 30, 4, THEME_BG);
  tft.drawRoundRect(200, 204, 55, 30, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("PG >", 227, 212, 2);

  // PLAY/STOP
  uint16_t playBg  = arpRunning ? THEME_PRIMARY : THEME_BG;
  uint16_t playTxt = arpRunning ? THEME_BG : THEME_PRIMARY;
  tft.fillRoundRect(260, 204, 55, 30, 4, playBg);
  tft.drawRoundRect(260, 204, 55, 30, 4, THEME_OUTLINE);
  tft.setTextColor(playTxt, playBg);
  tft.drawCentreString(arpRunning ? "STOP" : "PLAY", 287, 212, 2);

  arpNeedsRedraw = false;
}

void zombieArpHandleTouch() {
  if (!touch.justPressed) return;

  // BACK
  if (isButtonPressed(5, 5, 55, 20)) { exitToMenu(); return; }

  // BPM controls
  float bpm = zombieArp->getBPM();
  if (isButtonPressed(5, 55, 52, 28))   { zombieArp->setBPM(bpm - 10.0f); arpNeedsRedraw = true; return; }
  if (isButtonPressed(62, 55, 40, 28))  { zombieArp->setBPM(bpm -  1.0f); arpNeedsRedraw = true; return; }
  if (isButtonPressed(218, 55, 40, 28)) { zombieArp->setBPM(bpm +  1.0f); arpNeedsRedraw = true; return; }
  if (isButtonPressed(263, 55, 52, 28)) { zombieArp->setBPM(bpm + 10.0f); arpNeedsRedraw = true; return; }

  // Pattern selection
  int startPattern = arpPatternPage * 5;
  for (int i = 0; i < 5; i++) {
    int patternIdx = startPattern + i;
    if (patternIdx >= NUM_ARP_PATTERNS) break;
    if (isButtonPressed(8, 107 + i*18, 304, 16)) {
      zombieArp->setPattern((ArpPattern)patternIdx);
      arpNeedsRedraw = true;
      return;
    }
  }

  // Page navigation
  if (isButtonPressed(5, 204, 55, 30))   { arpPatternPage = (arpPatternPage - 1 + 10) % 10; arpNeedsRedraw = true; }
  if (isButtonPressed(200, 204, 55, 30)) { arpPatternPage = (arpPatternPage + 1) % 10;       arpNeedsRedraw = true; }

  // Play/Stop
  if (isButtonPressed(260, 204, 55, 30)) {
    arpRunning = !arpRunning;
    if (!arpRunning) {
      zombieArp->allNotesOff();
      if (getZombieSynth()) getZombieSynth()->allNotesOff();
    }
    arpNeedsRedraw = true;
  }
}

void zombieArpUpdate() {
  if (!zombieArp || !arpRunning) return;
  int note = zombieArp->update(millis());
  if (note >= 0 && getZombieSynth()) {
    getZombieSynth()->noteOn(note, 100);
  }
}

Arpeggiator* getZombieArp() {
  return zombieArp;
}

#endif

// WerewolfAmyS3 — arpeggiator page (50 ZombieSynth patterns)
#pragma once
#include "ui_common.h"
#include "arp_engine.h"
#include "clock.h"

static int arpPage = 0;
static bool arpDirty = true;
static const int ARP_PER_PAGE = 10;

inline void arpInit() { tft.fillScreen(THEME_BG); arpDirty = true; }

inline void arpLoop() {
  int numPages = (NUM_ARP_PATTERNS + ARP_PER_PAGE - 1) / ARP_PER_PAGE;
  bool rd = arpDirty;
  if (rd) {
    arpDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("ARPEGGIATOR", true);
    for (int i = 0; i < ARP_PER_PAGE; i++) {
      int idx = arpPage * ARP_PER_PAGE + i;
      if (idx >= NUM_ARP_PATTERNS) break;
      int x = 4 + (i % 2) * 158, y = 36 + (i / 2) * 27;
      drawButton(x, y, 154, 24, arpPatternNames[idx], THEME_PRIMARY,
                 idx == (int)arp.core.getPattern());
    }
    drawButton(4, 174, 60, 24, "< PG", THEME_OUTLINE);
    drawButton(256, 174, 60, 24, "PG >", THEME_OUTLINE);
    char pg[12]; snprintf(pg, sizeof(pg), "%d/%d", arpPage + 1, numPages);
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(pg, 160, 178, 2);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  if (tapped(4, 174, 60, 24) && arpPage > 0) { arpPage--; arpDirty = true; }
  if (tapped(256, 174, 60, 24) && arpPage < numPages - 1) { arpPage++; arpDirty = true; }

  for (int i = 0; i < ARP_PER_PAGE; i++) {
    int idx = arpPage * ARP_PER_PAGE + i;
    if (idx >= NUM_ARP_PATTERNS) break;
    int x = 4 + (i % 2) * 158, y = 36 + (i / 2) * 27;
    if (tapped(x, y, 154, 24)) {
      arp.core.setPattern((ArpPattern)idx);
      arpDirty = true;
      return;
    }
  }

  // bottom controls: ON, LATCH, DIV, OCT, GATE, BPM
  char buf[16];
  if (rd) {
    drawButton(4, 208, 48, 26, "ON", THEME_GOOD, arp.enabled);
    drawButton(56, 208, 56, 26, "LATCH", THEME_WARN, arp.latch);
  }
  if (tapped(4, 208, 48, 26)) { arp.setEnabled(!arp.enabled); arpDirty = true; }
  if (tapped(56, 208, 56, 26)) {
    arp.latch = !arp.latch;
    if (!arp.latch) arp.clearLatch();
    arpDirty = true;
  }
  int hit = spinner(116, 208, 64, 26, ARP_DIV_NAMES[arp.division], rd);
  if (hit) {
    arp.division = (ArpDiv)constrain((int)arp.division + hit, 0, NUM_ARP_DIVS - 1);
    arpDirty = true;
  }
  static int octRange = 1;
  snprintf(buf, sizeof(buf), "OCT%d", octRange);
  hit = spinner(184, 208, 62, 26, buf, rd);
  if (hit) {
    octRange = constrain(octRange + hit, 1, 4);
    arp.core.setOctaveRange(octRange);
    arpDirty = true;
  }
  snprintf(buf, sizeof(buf), "%d", masterClock.bpm);
  hit = spinner(250, 208, 68, 26, buf, rd);
  if (hit) {
    masterClock.nudgeBPM(hit * 5);
    arpDirty = true;
  }
}

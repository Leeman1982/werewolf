// WerewolfAmyS3 — MIDI routing matrix + clock settings
#pragma once
#include "ui_common.h"
#include "midi_router.h"
#include "storage.h"

static bool routeDirty = true;

static const int RM_X = 92, RM_Y = 52, RM_CW = 56, RM_RH = 20;

inline void routeInit() { tft.fillScreen(THEME_BG); routeDirty = true; }

inline void routeLoop() {
  bool rd = routeDirty;
  char buf[16];
  int hit;
  if (rd) {
    routeDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("MIDI ROUTING", true);
    // column headers
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    for (int d = 0; d < NUM_DST; d++)
      tft.drawCentreString(MIDI_DST_NAMES[d], RM_X + d * RM_CW + RM_CW / 2, RM_Y - 16, 2);
    // rows
    for (int s = 0; s < NUM_SRC; s++) {
      tft.setTextColor(THEME_TEXT, THEME_BG);
      tft.drawString(MIDI_SRC_NAMES[s], 4, RM_Y + s * RM_RH + 3, 2);
      for (int d = 0; d < NUM_DST; d++) {
        int x = RM_X + d * RM_CW + 8, y = RM_Y + s * RM_RH + 2;
        bool on = router.routed((MidiSrc)s, (MidiDst)d);
        tft.fillRoundRect(x, y, RM_CW - 16, RM_RH - 5, 3, on ? THEME_GOOD : THEME_STEP_OFF);
        tft.drawRoundRect(x, y, RM_CW - 16, RM_RH - 5, 3, THEME_GRID);
      }
    }
  }
  if (drawHeaderAndBack(nullptr, false)) { storage.saveSettings(); enterMode(MODE_MENU); return; }

  // matrix taps
  if (touch.justPressed && touch.y >= RM_Y && touch.y < RM_Y + NUM_SRC * RM_RH && touch.x >= RM_X) {
    int s = (touch.y - RM_Y) / RM_RH;
    int d = (touch.x - RM_X) / RM_CW;
    if (s >= 0 && s < NUM_SRC && d >= 0 && d < NUM_DST) {
      router.toggle((MidiSrc)s, (MidiDst)d);
      routeDirty = true;
      return;
    }
  }

  // clock + channel settings
  int y = RM_Y + NUM_SRC * RM_RH + 10;
  static const char* const CLK_NAMES[3] = { "INTERNAL", "DIN CLK", "USB CLK" };
  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("CLOCK", 4, y + 4, 2); }
  hit = spinner(56, y, 100, 24, CLK_NAMES[router.clockSource], rd);
  if (hit) {
    int c = constrain((int)router.clockSource + hit, 0, 2);
    router.setClockSource((ClockSource)c);
    routeDirty = true;
  }
  if (rd) {
    drawButton(162, y, 74, 24, "CLK>DIN", THEME_GOOD, router.clockOutDin);
    drawButton(240, y, 74, 24, "CLK>USB", THEME_GOOD, router.clockOutUsb);
  }
  if (tapped(162, y, 74, 24)) { router.clockOutDin = !router.clockOutDin; routeDirty = true; }
  if (tapped(240, y, 74, 24)) { router.clockOutUsb = !router.clockOutUsb; routeDirty = true; }

  y += 30;
  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("WOLF CH", 4, y + 4, 2); }
  if (router.wolfChannel == 0) snprintf(buf, sizeof(buf), "OMNI");
  else snprintf(buf, sizeof(buf), "%d", router.wolfChannel);
  hit = spinner(70, y, 86, 24, buf, rd);
  if (hit) { router.wolfChannel = constrain((int)router.wolfChannel + hit, 0, 16); routeDirty = true; }

  if (rd) { tft.setTextColor(THEME_TEXT_DIM, THEME_BG); tft.drawString("LOCAL CH", 162, y + 4, 2); }
  snprintf(buf, sizeof(buf), "%d", router.localChannel);
  hit = spinner(234, y, 80, 24, buf, rd);
  if (hit) { router.localChannel = constrain((int)router.localChannel + hit, 1, 16); routeDirty = true; }

  y += 30;
  if (rd) drawButton(4, y, 90, 24, "PANIC", THEME_PRIMARY);
  if (tapped(4, y, 90, 24)) router.panic();
  if (rd) drawButton(100, y, 120, 24, "SAVE SETUP", THEME_WARN);
  if (tapped(100, y, 120, 24)) storage.saveSettings();
}

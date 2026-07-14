// WerewolfAmyS3 — main menu
#pragma once
#include "ui_common.h"
#include "clock.h"
#include "gm_engine.h"

enum AppMode : uint8_t {
  MODE_MENU, MODE_WOLF, MODE_PRESETS, MODE_ARP, MODE_WSEQ,
  MODE_GMSEQ, MODE_GMMIX, MODE_ROUTE, MODE_SETUP, NUM_MODES
};
extern AppMode currentMode;
void enterMode(AppMode m);   // defined in the .ino

struct MenuIcon { const char* sym; const char* name; AppMode mode; };
static const MenuIcon MENU_ICONS[] = {
  { "WLF", "SYNTH",   MODE_WOLF },
  { "PRE", "PRESETS", MODE_PRESETS },
  { "ARP", "ARP",     MODE_ARP },
  { "WSQ", "WOLF SEQ",MODE_WSEQ },
  { "GM",  "GM SEQ",  MODE_GMSEQ },
  { "MIX", "GM MIX",  MODE_GMMIX },
  { "RTE", "ROUTING", MODE_ROUTE },
  { "SET", "SETUP",   MODE_SETUP },
};
static const int NUM_MENU_ICONS = 8;
static const int MI_W = 72, MI_H = 62, MI_GAP = 8;

inline int menuIconX(int i) { return (SCREEN_W - 4 * (MI_W + MI_GAP) + MI_GAP) / 2 + (i % 4) * (MI_W + MI_GAP); }
inline int menuIconY(int i) { return 88 + (i / 4) * (MI_H + MI_GAP); }

inline void menuInit() {
  tft.fillScreen(THEME_BG);
  tft.drawRect(0, 0, SCREEN_W, 58, THEME_OUTLINE);
  tft.drawRect(1, 1, SCREEN_W - 2, 56, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(WEREWOLF_NAME, 160, 6, 4);
  tft.drawCentreString(WEREWOLF_NAME, 161, 7, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("AMY SYNTH + GM SEQUENCER  ESP32-S3", 160, 36, 2);

  for (int i = 0; i < NUM_MENU_ICONS; i++) {
    int x = menuIconX(i), y = menuIconY(i);
    tft.drawRoundRect(x, y, MI_W, MI_H, 8, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(MENU_ICONS[i].sym, x + MI_W / 2, y + 9, 4);
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString(MENU_ICONS[i].name, x + MI_W / 2, y + 42, 2);
  }
}

inline void menuLoop() {
  // status line
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 500) {
    lastStatus = millis();
    char buf[48];
    snprintf(buf, sizeof(buf), "BPM %d   GM VOICES %d ", masterClock.bpm, gm.activeVoices());
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawCentreString(buf, 160, 66, 2);
  }
  if (touch.justPressed)
    for (int i = 0; i < NUM_MENU_ICONS; i++)
      if (inRect(menuIconX(i), menuIconY(i), MI_W, MI_H)) {
        enterMode(MENU_ICONS[i].mode);
        return;
      }
}

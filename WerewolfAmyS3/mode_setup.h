// WerewolfAmyS3 — setup / status page
#pragma once
#include "ui_common.h"
#include "gm_engine.h"
#include "clock.h"
#include "storage.h"
#include "controls.h"

static bool setupDirty = true;

inline void setupInit() { tft.fillScreen(THEME_BG); setupDirty = true; }

inline void setupLoop() {
  bool rd = setupDirty;
  char buf[48];
  if (rd) {
    setupDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("SETUP", true);

    tft.setTextColor(THEME_TEXT, THEME_BG);
    int y = 40;
    snprintf(buf, sizeof(buf), "Firmware: %s v%s", WEREWOLF_NAME, WEREWOLF_VERSION);
    tft.drawString(buf, 6, y, 2); y += 18;
    snprintf(buf, sizeof(buf), "SoundFont: %s (%d presets)",
             gm.ok() ? "loaded" : "MISSING - flash 'sf2' partition",
             gm.presetCount());
    tft.setTextColor(gm.ok() ? THEME_GOOD : THEME_PRIMARY, THEME_BG);
    tft.drawString(buf, 6, y, 2); y += 18;
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    snprintf(buf, sizeof(buf), "Free heap: %u  PSRAM: %u",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getFreePsram());
    tft.drawString(buf, 6, y, 2); y += 18;
    tft.drawString("Panel pots move WOLF params (see MIDI_MAP.md)", 6, y, 2);

    drawButton(6, 130, 150, 26, "SAVE SETTINGS", THEME_WARN);
    drawButton(162, 130, 150, 26, "SAVE ALL SEQS", THEME_WARN);
    drawButton(6, 162, 150, 26, "LOAD ALL", THEME_GOOD);
    drawButton(162, 162, 150, 26, "PANIC", THEME_PRIMARY);
    drawButton(6, 194, 306, 26, "RESYNC PANEL POTS (pickup)", THEME_OUTLINE);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  if (tapped(6, 130, 150, 26)) storage.saveSettings();
  if (tapped(162, 130, 150, 26)) { storage.saveWolfSeq(wolfSeq); storage.saveGMSeq(gmSeq); }
  if (tapped(6, 162, 150, 26)) {
    storage.loadSettings();
    storage.loadWolfSeq(&wolfSeq);
    storage.loadGMSeq(&gmSeq);
    masterClock.setBPM(masterClock.bpm);
    setupDirty = true;
  }
  if (tapped(162, 162, 150, 26)) router.panic();
  if (tapped(6, 194, 306, 26)) panel.staleAll();
}

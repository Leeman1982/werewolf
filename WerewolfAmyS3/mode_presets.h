// WerewolfAmyS3 — preset browser
//   pages of the WOLF preset table (custom + factory highlights),
//   user slots (NVS), and raw Juno/DX7 bank browsing by number.
#pragma once
#include "ui_common.h"
#include "wolf_synth.h"
#include "wolf_presets.h"
#include "storage.h"

static int presetPage = 0;
static bool presetsDirty = true;
static int userSlotSel = 0;
static int bankBrowse = 0;    // 0-255 raw AMY patch

static const int PRESETS_PER_PAGE = 8;

inline void presetsInit() { tft.fillScreen(THEME_BG); presetsDirty = true; }

inline void presetsLoop() {
  int numPages = (NUM_WOLF_PRESETS + PRESETS_PER_PAGE - 1) / PRESETS_PER_PAGE;
  bool rd = presetsDirty;
  if (rd) {
    presetsDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("PRESETS", true);
    for (int i = 0; i < PRESETS_PER_PAGE; i++) {
      int idx = presetPage * PRESETS_PER_PAGE + i;
      if (idx >= NUM_WOLF_PRESETS) break;
      int x = 4 + (i % 2) * 158, y = 38 + (i / 2) * 30;
      bool cur = (idx == wolf.currentPreset);
      drawButton(x, y, 154, 26, WOLF_PRESETS[idx].name,
                 WOLF_PRESETS[idx].factory >= 0 ? THEME_ACCENT : THEME_PRIMARY, cur);
    }
    char pg[16]; snprintf(pg, sizeof(pg), "%d/%d", presetPage + 1, numPages);
    drawButton(4, 162, 60, 24, "< PG", THEME_OUTLINE);
    drawButton(256, 162, 60, 24, "PG >", THEME_OUTLINE);
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(pg, 160, 166, 2);

    // user slots + bank browse
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString("USER SLOT", 4, 192, 2);
    tft.drawString("AMY BANK (0-127 JUNO / 128-255 DX7)", 120, 192, 2);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  if (tapped(4, 162, 60, 24) && presetPage > 0) { presetPage--; presetsDirty = true; }
  if (tapped(256, 162, 60, 24) && presetPage < numPages - 1) { presetPage++; presetsDirty = true; }

  for (int i = 0; i < PRESETS_PER_PAGE; i++) {
    int idx = presetPage * PRESETS_PER_PAGE + i;
    if (idx >= NUM_WOLF_PRESETS) break;
    int x = 4 + (i % 2) * 158, y = 38 + (i / 2) * 30;
    if (tapped(x, y, 154, 26)) {
      applyWolfPreset(wolf, idx);
      presetsDirty = true;
      return;
    }
  }

  // user slots: LOAD / SAVE
  char buf[12];
  snprintf(buf, sizeof(buf), "U%d", userSlotSel + 1);
  int hit = spinner(4, 208, 70, 24, buf, rd);
  if (hit) { userSlotSel = constrain(userSlotSel + hit, 0, NUM_USER_PRESETS - 1); presetsDirty = true; }
  if (rd) {
    drawButton(78, 208, 50, 24, "LOAD", THEME_GOOD);
    drawButton(132, 208, 50, 24, "SAVE", THEME_WARN);
  }
  if (tapped(78, 208, 50, 24)) {
    WolfParams np;
    if (storage.loadUserPreset(userSlotSel, &np)) {
      wolf.p = np;
      wolf.defineSynth();
      wolf.sendEffects();
    }
  }
  if (tapped(132, 208, 50, 24)) storage.saveUserPreset(userSlotSel, wolf.p);

  // raw AMY bank browse
  snprintf(buf, sizeof(buf), "%d", bankBrowse);
  hit = spinner(196, 208, 80, 24, buf, rd);
  if (hit) {
    bankBrowse = constrain(bankBrowse + hit, 0, 255);
    presetsDirty = true;
  }
  if (rd) drawButton(280, 208, 38, 24, "GO", THEME_PRIMARY);
  if (tapped(280, 208, 38, 24))
    wolf.loadFactoryPatch(bankBrowse, bankBrowse >= 128 ? 6 : 8);
}

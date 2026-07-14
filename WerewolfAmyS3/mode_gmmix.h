// WerewolfAmyS3 — GM mixer: 16 channel levels + mutes + programs at a glance
#pragma once
#include "ui_common.h"
#include "gm_seq.h"
#include "gm_engine.h"

static bool gmixDirty = true;

inline void gmixInit() { tft.fillScreen(THEME_BG); gmixDirty = true; }

inline void gmixLoop() {
  bool rd = gmixDirty;
  GPattern& p = gmSeq.pat();
  if (rd) {
    gmixDirty = false;
    tft.fillScreen(THEME_BG);
    drawHeaderAndBack("GM MIXER", true);
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  // two columns of 8: [ch] [mute] [level slider]
  for (int i = 0; i < GMSEQ_TRACKS; i++) {
    int col = i / 8, row = i % 8;
    int x = 4 + col * 160, y = 36 + row * 25;
    GTrack& t = p.tracks[i];
    char buf[8];
    if (rd) {
      snprintf(buf, sizeof(buf), "%d", i + 1);
      tft.setTextColor(i == GM_DRUM_CHANNEL ? THEME_ACCENT : THEME_TEXT, THEME_BG);
      tft.drawString(buf, x, y + 2, 2);
      drawButton(x + 22, y, 26, 18, "M", THEME_PRIMARY, t.mute);
    }
    if (tapped(x + 22, y, 26, 18)) {
      t.mute = !t.mute;
      drawButton(x + 22, y, 26, 18, "M", THEME_PRIMARY, t.mute);
      if (t.mute) router.send(SRC_GMSEQ, 0xB0 | i, 123, 0);
    }
    float lvl = t.level / 127.0f;
    if (hSlider(x + 54, y + 1, 100, 16, nullptr, &lvl, rd)) {
      t.level = (uint8_t)(lvl * 127.0f);
      router.send(SRC_GMSEQ, 0xB0 | i, 7, t.level);
    }
  }
}

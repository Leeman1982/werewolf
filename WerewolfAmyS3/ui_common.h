// WerewolfAmyS3 — touch input + shared UI widgets
#pragma once
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include "config.h"
#include "theme.h"

// ── Touch state ──────────────────────────────────────────────────────────────
struct TouchState {
  bool wasPressed   = false;
  bool isPressed    = false;
  bool justPressed  = false;
  bool justReleased = false;
  int  x = 0, y = 0;
};

extern XPT2046_Touchscreen ts;
extern TouchState touch;

// Runtime-adjustable calibration (SETUP page can rewrite these)
struct TouchCal { int xmin, xmax, ymin, ymax; };
extern TouchCal touchCal;

inline void updateTouch() {
  touch.wasPressed   = touch.isPressed;
  touch.isPressed    = ts.tirqTouched() && ts.touched();
  touch.justPressed  = touch.isPressed && !touch.wasPressed;
  touch.justReleased = !touch.isPressed && touch.wasPressed;

  if (touch.isPressed) {
    TS_Point p = ts.getPoint();
    touch.x = constrain(map(p.x, touchCal.xmin, touchCal.xmax, 0, SCREEN_W), 0, SCREEN_W - 1);
    touch.y = constrain(map(p.y, touchCal.ymin, touchCal.ymax, 0, SCREEN_H), 0, SCREEN_H - 1);
  }
}

inline bool inRect(int x, int y, int w, int h) {
  return touch.x >= x && touch.x < x + w && touch.y >= y && touch.y < y + h;
}
// Tap = a fresh press inside the rect this frame
inline bool tapped(int x, int y, int w, int h) {
  return touch.justPressed && inRect(x, y, w, h);
}
// Drag = held press inside the rect (for sliders)
inline bool draggedIn(int x, int y, int w, int h) {
  return touch.isPressed && inRect(x, y, w, h);
}

// ── Widgets ──────────────────────────────────────────────────────────────────
inline void drawButton(int x, int y, int w, int h, const char* text,
                       uint16_t color, bool active = false, int font = 2) {
  uint16_t bg  = active ? color : THEME_BG;
  uint16_t txt = active ? THEME_BG : color;
  tft.fillRoundRect(x, y, w, h, 5, bg);
  tft.drawRoundRect(x, y, w, h, 5, color);
  tft.setTextColor(txt, bg);
  tft.drawCentreString(text, x + w / 2, y + (h - (font == 4 ? 26 : 16)) / 2 + 1, font);
}

// Horizontal slider: returns true (and updates *value) while being dragged.
inline bool hSlider(int x, int y, int w, int h, const char* label,
                    float* value, bool redraw) {
  bool changed = false;
  if (draggedIn(x - 6, y, w + 12, h)) {
    float v = (float)(touch.x - x) / (float)w;
    v = constrain(v, 0.0f, 1.0f);
    if (fabsf(v - *value) > 0.004f) { *value = v; changed = true; }
  }
  if (redraw || changed) {
    int fill = (int)(*value * (w - 4));
    tft.drawRect(x, y, w, h, THEME_OUTLINE);
    tft.fillRect(x + 2, y + 2, w - 4, h - 4, THEME_BG);
    tft.fillRect(x + 2, y + 2, fill, h - 4, THEME_PRIMARY);
    if (label && label[0]) {
      tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
      tft.drawString(label, x, y - 14, 2);
    }
  }
  return changed;
}

// Value spinner "< value >". Returns -1/+1 on tap of arrows, 0 otherwise.
inline int spinner(int x, int y, int w, int h, const char* value, bool redraw) {
  int hit = 0;
  if (tapped(x, y, h, h)) hit = -1;
  else if (tapped(x + w - h, y, h, h)) hit = +1;
  if (redraw || hit) {
    tft.drawRect(x, y, w, h, THEME_OUTLINE);
    tft.fillRect(x + 1, y + 1, w - 2, h - 2, THEME_BG);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString("<", x + h / 2, y + (h - 16) / 2, 2);
    tft.drawCentreString(">", x + w - h / 2, y + (h - 16) / 2, 2);
    tft.setTextColor(THEME_TEXT, THEME_BG);
    tft.drawCentreString(value, x + w / 2, y + (h - 16) / 2, 2);
  }
  return hit;
}

// Branded header with BACK button. Returns true if BACK tapped.
inline bool drawHeaderAndBack(const char* subtitle, bool redraw) {
  if (redraw) {
    tft.fillRect(0, 0, SCREEN_W, 30, THEME_BG);
    tft.drawFastHLine(0, 30, SCREEN_W, THEME_OUTLINE);
    drawButton(4, 3, 52, 24, "BACK", THEME_PRIMARY);
    // Heavy wordmark: double-draw offsets
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString(WEREWOLF_NAME, 186, 3, 4);
    tft.drawCentreString(WEREWOLF_NAME, 187, 4, 4);
    if (subtitle && subtitle[0]) {
      tft.setTextColor(THEME_ACCENT, THEME_BG);
      tft.drawRightString(subtitle, SCREEN_W - 4, 34, 2);
    }
  }
  return tapped(0, 0, 60, 30);
}

// MIDI note number → name
inline const char* noteName(int note, char* buf) {
  static const char* NAMES[12] =
    { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
  sprintf(buf, "%s%d", NAMES[note % 12], note / 12 - 1);
  return buf;
}

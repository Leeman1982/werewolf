#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H

#include "common_definitions.h"

// UI function declarations
void updateTouch();
void updateStatus();
bool isButtonPressed(int x, int y, int w, int h);
void drawRoundButton(int x, int y, int w, int h, String text, uint16_t color, bool pressed = false);
void drawHeader(String title, String subtitle = "");
void exitToMenu();

// UI implementations
void updateTouch() {
  touch.wasPressed = touch.isPressed;
  touch.isPressed = ts.tirqTouched() && ts.touched();
  touch.justPressed = touch.isPressed && !touch.wasPressed;
  touch.justReleased = !touch.isPressed && touch.wasPressed;
  
  if (touch.isPressed) {
    TS_Point p = ts.getPoint();
    touch.x = map(p.x, 200, 3700, 0, 320);
    touch.y = map(p.y, 240, 3800, 0, 240);
  }
}

bool isButtonPressed(int x, int y, int w, int h) {
  return touch.x >= x && touch.x <= x + w && touch.y >= y && touch.y <= y + h;
}

void drawRoundButton(int x, int y, int w, int h, String text, uint16_t color, bool pressed) {
  uint16_t bgColor = pressed ? color : THEME_SURFACE;
  uint16_t borderColor = color;
  uint16_t textColor = pressed ? THEME_BG : color;
  
  tft.fillRoundRect(x, y, w, h, 8, bgColor);
  tft.drawRoundRect(x, y, w, h, 8, borderColor);
  tft.drawRoundRect(x+1, y+1, w-2, h-2, 7, borderColor);
  
  tft.setTextColor(textColor, bgColor);
  tft.drawCentreString(text, x + w/2, y + h/2 - 8, 2);
}

void drawHeader(String title, String subtitle) {
  tft.fillRect(0, 0, 320, 45, THEME_SURFACE);
  tft.drawFastHLine(0, 45, 320, THEME_PRIMARY);

  tft.setTextColor(THEME_TEXT, THEME_SURFACE);
  tft.drawCentreString(title, 160, 8, 4);

  if (subtitle.length() > 0) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_SURFACE);
    tft.drawCentreString(subtitle, 160, 28, 2);
  }

  drawRoundButton(10, 10, 50, 25, "BACK", THEME_ERROR);
}

// Branded header used by every ZOMBI SS mode.
// Bold "ZOMBI SS" wordmark rendered in Font 4 (full alphabet) with a
// 1-pixel x/y offset triple-draw for a heavy, bold appearance.  Subtitle
// underneath in Font 2.  BACK button on the left.
inline void drawZombiHeader(const char* subtitle) {
  tft.fillRect(0, 0, 320, 50, THEME_BG);
  tft.drawRect(0, 0, 320, 50, THEME_OUTLINE);
  tft.drawRect(1, 1, 318, 48, THEME_OUTLINE);

  // BACK button (kept at original position, behaviour unchanged)
  tft.fillRoundRect(5, 5, 55, 20, 4, THEME_PRIMARY);
  tft.drawRoundRect(5, 5, 55, 20, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_BG, THEME_PRIMARY);
  tft.drawString("BACK", 15, 8, 2);

  // Bold ZOMBI SS wordmark: triple-draw with x/y offsets thickens strokes.
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("ZOMBI SS", 190, 2, 4);
  tft.drawCentreString("ZOMBI SS", 191, 2, 4);
  tft.drawCentreString("ZOMBI SS", 190, 3, 4);

  // Subtitle line beneath the wordmark
  if (subtitle && subtitle[0]) {
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawCentreString(subtitle, 190, 34, 2);
  }
}

void updateStatus() {
  // Status bar removed - no more BLE connection alerts on every screen
}

#endif
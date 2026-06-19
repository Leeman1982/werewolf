#ifndef UI_ELEMENTS_H
#define UI_ELEMENTS_H

#include "common_definitions.h"
#include <Preferences.h>

// UI function declarations
void updateTouch();
void updateStatus();
bool isButtonPressed(int x, int y, int w, int h);
void drawRoundButton(int x, int y, int w, int h, String text, uint16_t color, bool pressed = false);
void drawHeader(String title, String subtitle = "");
void exitToMenu();
void initTouch();
void runTouchCalibration();

// ── Touch calibration (XPT2046 resistive) ────────────────────────────────────
// The raw→pixel mapping is panel-specific, so rather than hard-code numbers that
// may be wrong on YOUR 2.4" panel, a 2-tap on-screen calibration runs on the
// very first boot (and any time you HOLD THE SCREEN while powering on).  The
// result is saved to flash and reused every boot — so touch is accurate the
// first time, every time, without editing code.
//
// Sensible landscape (rotation 1) defaults — used only until calibrated:
static int  g_calXmin = 200, g_calXmax = 3700;
static int  g_calYmin = 240, g_calYmax = 3800;
static Preferences g_touchPrefs;

static void loadTouchCal() {
  g_touchPrefs.begin("cyd24_touch", true);
  if (g_touchPrefs.isKey("xmin")) {
    g_calXmin = g_touchPrefs.getInt("xmin", g_calXmin);
    g_calXmax = g_touchPrefs.getInt("xmax", g_calXmax);
    g_calYmin = g_touchPrefs.getInt("ymin", g_calYmin);
    g_calYmax = g_touchPrefs.getInt("ymax", g_calYmax);
  }
  g_touchPrefs.end();
}
static void saveTouchCal() {
  g_touchPrefs.begin("cyd24_touch", false);
  g_touchPrefs.putInt("xmin", g_calXmin);
  g_touchPrefs.putInt("xmax", g_calXmax);
  g_touchPrefs.putInt("ymin", g_calYmin);
  g_touchPrefs.putInt("ymax", g_calYmax);
  g_touchPrefs.end();
}
static bool touchCalExists() {
  g_touchPrefs.begin("cyd24_touch", true);
  bool have = g_touchPrefs.isKey("xmin");
  g_touchPrefs.end();
  return have;
}

// Wait for a CLEAN fresh tap, average ~40 reads, wait for release.
static bool calReadRaw(int &rx, int &ry) {
  while (ts.touched()) delay(10);    // release first (handles the boot-hold)
  delay(120);
  unsigned long t0 = millis();
  while (!ts.touched()) { if (millis() - t0 > 12000UL) return false; delay(10); }
  long sx = 0, sy = 0; int n = 0;
  while (ts.touched() && n < 40) { TS_Point p = ts.getPoint(); sx += p.x; sy += p.y; n++; delay(5); }
  if (n < 4) return false;
  rx = (int)(sx / n); ry = (int)(sy / n);
  while (ts.touched()) delay(10);   // wait for release
  delay(250);
  return true;
}

static void calDrawTarget(int x, int y, const char* msg) {
  tft.fillScreen(THEME_BG);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("TOUCH CALIBRATION", 160, 92, 4);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(msg, 160, 128, 2);
  // crosshair target
  tft.drawLine(x-14, y, x+14, y, THEME_ACCENT);
  tft.drawLine(x, y-14, x, y+14, THEME_ACCENT);
  tft.drawCircle(x, y, 9, THEME_PRIMARY);
}

// Two-corner calibration → linear raw↔pixel map (handles any flip/scale).
void runTouchCalibration() {
  const int ax = 16, ay = 16, bx = 303, by = 223;   // target pixel positions
  int x1, y1, x2, y2;
  calDrawTarget(ax, ay, "Tap the cross  (top-left)");
  if (!calReadRaw(x1, y1)) return;                   // timeout → keep current cal
  calDrawTarget(bx, by, "Tap the cross  (bottom-right)");
  if (!calReadRaw(x2, y2)) return;
  if (x2 == x1 || y2 == y1) return;                  // degenerate → keep current
  float sx = (float)(x2 - x1) / (float)(bx - ax);
  float sy = (float)(y2 - y1) / (float)(by - ay);
  g_calXmin = (int)(x1 - sx * ax);
  g_calXmax = (int)(x1 + sx * (320 - ax));
  g_calYmin = (int)(y1 - sy * ay);
  g_calYmax = (int)(y1 + sy * (240 - ay));
  saveTouchCal();
  tft.fillScreen(THEME_BG);
  tft.setTextColor(THEME_SUCCESS, THEME_BG);
  tft.drawCentreString("CALIBRATED!", 160, 108, 4);
  delay(900);
}

// Call once in setup() after tft.init() + ts.begin().  Calibrates on first boot
// or when the screen is held at power-up; otherwise loads the saved values.
void initTouch() {
  loadTouchCal();
  bool held = ts.touched();
  if (!touchCalExists() || held) runTouchCalibration();
}

// UI implementations
void updateTouch() {
  touch.wasPressed = touch.isPressed;
  // ts.touched() alone (no tirqTouched) — most reliable across CYD panels.
  touch.isPressed = ts.touched();
  touch.justPressed = touch.isPressed && !touch.wasPressed;
  touch.justReleased = !touch.isPressed && touch.wasPressed;

  if (touch.isPressed) {
    TS_Point p = ts.getPoint();
    touch.x = constrain((int)map(p.x, g_calXmin, g_calXmax, 0, 320), 0, 319);
    touch.y = constrain((int)map(p.y, g_calYmin, g_calYmax, 0, 240), 0, 239);
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
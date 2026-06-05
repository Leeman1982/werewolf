#ifndef ZOMBIE_KEYBOARD_INPUT_H
#define ZOMBIE_KEYBOARD_INPUT_H

#include "common_definitions.h"

// On-screen QWERTY keyboard for preset naming
// Returns true when user presses DONE

struct NameKeyboard {
  char text[15];
  int  len;
  bool active;
  bool done;

  void open(const char* initial) {
    strncpy(text, initial, 14);
    text[14] = '\0';
    len    = strlen(text);
    // Trim trailing spaces from "-- EMPTY --" etc
    while (len > 0 && text[len-1] == ' ') { text[--len] = '\0'; }
    if (strcmp(text, "--EMPTY--")==0 || strcmp(text,"-- EMPTY --")==0) {
      text[0] = '\0'; len = 0;
    }
    active = true;
    done   = false;
  }

  void draw() {
    if (!active) return;

    // Dim overlay
    tft.fillRoundRect(4, 54, 312, 186, 6, THEME_BG);
    tft.drawRoundRect(4, 54, 312, 186, 6, THEME_OUTLINE);
    tft.drawRoundRect(5, 55, 310, 184, 6, THEME_PRIMARY);

    // Title
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("NAME PRESET", 160, 60, 2);

    // Text display box
    tft.fillRoundRect(10, 74, 300, 26, 4, THEME_BG);
    tft.drawRoundRect(10, 74, 300, 26, 4, THEME_OUTLINE);
    char display[20];
    snprintf(display, sizeof(display), "%s_", text);
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawString(display, 18, 80, 2);

    // Key rows
    const char* rows[4] = {"QWERTYUIOP", "ASDFGHJKL ", "ZXCVBNM<  ", "         "};
    int rowY[4] = {106, 127, 148, 169};
    int keySizes[4] = {10, 10, 10, 0};

    for (int r = 0; r < 3; r++) {
      int numKeys = strlen(rows[r]);
      int kw = 29, kh = 18;
      int startX = (320 - numKeys * (kw+2)) / 2;
      for (int k = 0; k < numKeys; k++) {
        char ch = rows[r][k];
        int kx = startX + k * (kw+2);
        int ky = rowY[r];
        if (ch == ' ') continue;
        bool isBksp = (ch == '<');
        uint16_t bg  = isBksp ? THEME_PRIMARY : THEME_BG;
        uint16_t txt = isBksp ? THEME_BG : THEME_PRIMARY;
        tft.fillRoundRect(kx, ky, kw, kh, 3, bg);
        tft.drawRoundRect(kx, ky, kw, kh, 3, THEME_OUTLINE);
        char label[3] = {isBksp ? '<' : ch, 0};
        tft.setTextColor(txt, bg);
        tft.drawCentreString(label, kx + kw/2, ky + 4, 2);
      }
    }

    // Bottom row: SPACE, DONE
    tft.fillRoundRect(14,  169, 130, 22, 4, THEME_BG);
    tft.drawRoundRect(14,  169, 130, 22, 4, THEME_OUTLINE);
    tft.setTextColor(THEME_PRIMARY, THEME_BG);
    tft.drawCentreString("SPACE", 79, 173, 2);

    tft.fillRoundRect(150, 169, 156, 22, 4, THEME_PRIMARY);
    tft.drawRoundRect(150, 169, 156, 22, 4, THEME_OUTLINE);
    tft.setTextColor(THEME_BG, THEME_PRIMARY);
    tft.drawCentreString("DONE", 228, 173, 2);
  }

  // Returns true if UI should redraw
  bool handleTouch(int tx, int ty) {
    if (!active) return false;

    // DONE button
    if (tx >= 150 && tx <= 306 && ty >= 169 && ty <= 191) {
      done = true; active = false;
      return true;
    }
    // SPACE
    if (tx >= 14 && tx <= 144 && ty >= 169 && ty <= 191) {
      if (len < 14) { text[len++] = ' '; text[len] = '\0'; }
      return true;
    }

    // Key rows
    const char* rows[3] = {"QWERTYUIOP", "ASDFGHJKL ", "ZXCVBNM<  "};
    int rowY[3] = {106, 127, 148};
    for (int r = 0; r < 3; r++) {
      int numKeys = strlen(rows[r]);
      int kw = 29, kh = 18;
      int startX = (320 - numKeys * (kw+2)) / 2;
      for (int k = 0; k < numKeys; k++) {
        char ch = rows[r][k];
        if (ch == ' ') continue;
        int kx = startX + k * (kw+2);
        int ky = rowY[r];
        if (tx >= kx && tx <= kx+kw && ty >= ky && ty <= ky+kh) {
          if (ch == '<') {
            if (len > 0) { text[--len] = '\0'; }
          } else {
            if (len < 14) { text[len++] = ch; text[len] = '\0'; }
          }
          return true;
        }
      }
    }
    return false;
  }
};

#endif

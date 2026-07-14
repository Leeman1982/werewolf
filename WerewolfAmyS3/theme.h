// WerewolfAmyS3 — UI theme (full-moon werewolf: black night, blood red, bone
// white, moon silver, amber highlights)
#pragma once
#include <TFT_eSPI.h>

#define THEME_BG        TFT_BLACK
#define THEME_PRIMARY   0xF800            // blood red
#define THEME_ACCENT    0xFD20            // amber/orange
#define THEME_OUTLINE   0xC618            // moon silver
#define THEME_TEXT      0xFFFF            // bone white
#define THEME_TEXT_DIM  0x8410            // grey
#define THEME_GOOD      0x07E0            // green (play / active)
#define THEME_WARN      0xFFE0            // yellow
#define THEME_STEP_ON   0xF800            // active step
#define THEME_STEP_ACC  0xFD20            // accented step
#define THEME_STEP_OFF  0x2104            // dark grey cell
#define THEME_CURSOR    0xFFFF            // playhead
#define THEME_GRID      0x4208            // grid lines
#define THEME_SEL       0x001F            // selection blue

extern TFT_eSPI tft;

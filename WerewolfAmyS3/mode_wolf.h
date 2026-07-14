// WerewolfAmyS3 — WOLF synth editor
// Tabs: OSC (waves/tuning) · MIX (levels/PW/glide/vol) · FILT · ENV · LFO
// Bottom: one-octave touch keyboard (events go through the router as LOCAL).
#pragma once
#include "ui_common.h"
#include "wolf_synth.h"
#include "wolf_presets.h"
#include "midi_router.h"
#include "arp_engine.h"

static const char* const WAVE_NAMES_UI[6] = { "SIN", "SQR", "SAW", "SAW+", "TRI", "NSE" };
static const uint8_t WAVE_CYCLE[6] = { SINE, PULSE, SAW_DOWN, SAW_UP, TRIANGLE, NOISE };
static const char* const FILT_NAMES_UI[5] = { "OFF", "LP", "BP", "HP", "LP24" };

inline const char* waveNameUI(uint8_t w) {
  for (int i = 0; i < 6; i++) if (WAVE_CYCLE[i] == w) return WAVE_NAMES_UI[i];
  return "?";
}
inline uint8_t nextWave(uint8_t w) {
  for (int i = 0; i < 6; i++) if (WAVE_CYCLE[i] == w) return WAVE_CYCLE[(i + 1) % 6];
  return SAW_DOWN;
}

// ── layout ──────────────────────────────────────────────────────────────────
static const int WTAB_Y = 32, WTAB_H = 22;
static const int WBODY_Y = 58, WBODY_H = 148;
static const int WKEY_Y = 208, WKEY_H = 32;
static uint8_t wolfTab = 0;      // 0 OSC 1 MIX 2 FILT 3 ENV 4 LFO
static bool wolfDirty = true;
static int8_t kbHeld = -1;       // key index currently held
static uint8_t kbOctave = 4;     // keyboard octave (C4 default)

static const char* const WOLF_TABS[5] = { "OSC", "MIX", "FILT", "ENV", "LFO" };

inline void wolfInit() {
  tft.fillScreen(THEME_BG);
  wolfDirty = true;
}

// small labelled spinner row helper
inline void wolfSpin(int x, int y, const char* label, const char* val, int* hitOut, bool redraw) {
  if (redraw) {
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawString(label, x, y - 14, 2);
  }
  *hitOut = spinner(x, y, 74, 22, val, redraw);
}

inline void drawWolfKeyboard() {
  static const bool BLACK[12] = { 0,1,0,1,0,0,1,0,1,0,1,0 };
  int w = SCREEN_W / 14;
  // 12 semitone keys + octave up/down at the ends
  drawButton(0, WKEY_Y, w, WKEY_H, "-", THEME_ACCENT);
  drawButton(SCREEN_W - w - 2, WKEY_Y, w, WKEY_H, "+", THEME_ACCENT);
  for (int i = 0; i < 12; i++) {
    int x = w + i * w;
    uint16_t bg = BLACK[i] ? THEME_BG : THEME_TEXT;
    tft.fillRect(x + 1, WKEY_Y, w - 2, WKEY_H, bg);
    tft.drawRect(x, WKEY_Y, w, WKEY_H, THEME_OUTLINE);
  }
}

inline void wolfKeyboardTouch() {
  int w = SCREEN_W / 14;
  if (touch.justPressed && inRect(0, WKEY_Y, w, WKEY_H) && kbOctave > 1) kbOctave--;
  if (touch.justPressed && inRect(SCREEN_W - w - 2, WKEY_Y, w, WKEY_H) && kbOctave < 7) kbOctave++;

  int key = -1;
  if (touch.isPressed && touch.y >= WKEY_Y) {
    int i = (touch.x - w) / w;
    if (i >= 0 && i < 12) key = i;
  }
  if (key != kbHeld) {
    uint8_t ch = router.localChannel - 1;
    if (kbHeld >= 0) {
      uint8_t oldNote = 12 * (kbOctave + 1) + kbHeld;
      if (arp.enabled) arp.noteOff(oldNote);
      else router.noteOff(SRC_LOCAL, ch, oldNote);
    }
    if (key >= 0) {
      uint8_t note = 12 * (kbOctave + 1) + key;
      if (arp.enabled) arp.noteOn(note);
      else router.noteOn(SRC_LOCAL, ch, note, 110);
    }
    kbHeld = key;
  }
}

inline void wolfLoop() {
  WolfParams& p = wolf.p;
  bool rd = wolfDirty || wolf.needsRedraw;
  if (rd) {
    wolf.needsRedraw = false;
    wolfDirty = false;
    tft.fillRect(0, 0, SCREEN_W, WKEY_Y, THEME_BG);
    if (drawHeaderAndBack(nullptr, true)) {}
    // preset name top right
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawRightString(WOLF_PRESETS[wolf.currentPreset].name, SCREEN_W - 4, 8, 2);
    // tabs
    for (int i = 0; i < 5; i++)
      drawButton(4 + i * 63, WTAB_Y, 58, WTAB_H, WOLF_TABS[i], THEME_PRIMARY, i == wolfTab);
    drawWolfKeyboard();
  }
  if (drawHeaderAndBack(nullptr, false)) { enterMode(MODE_MENU); return; }

  for (int i = 0; i < 5; i++)
    if (tapped(4 + i * 63, WTAB_Y, 58, WTAB_H) && wolfTab != i) {
      wolfTab = i; wolfDirty = true; return;
    }

  char buf[24];
  int hit;
  switch (wolfTab) {
    case 0: {  // ── OSC: per-osc wave + oct/semi/fine ─────────────────────────
      for (int o = 0; o < 3; o++) {
        int y = WBODY_Y + 14 + o * 46;
        if (rd) {
          snprintf(buf, sizeof(buf), "OSC%d", o + 1);
          tft.setTextColor(THEME_PRIMARY, THEME_BG);
          tft.drawString(buf, 4, y + 3, 2);
        }
        if (rd) drawButton(46, y, 48, 22, waveNameUI(p.osc[o].wave), THEME_ACCENT);
        if (tapped(46, y, 48, 22)) {
          wolf.setOscWave(o, nextWave(p.osc[o].wave));
          drawButton(46, y, 48, 22, waveNameUI(p.osc[o].wave), THEME_ACCENT);
        }
        snprintf(buf, sizeof(buf), "%+d", p.osc[o].octave);
        wolfSpin(100, y, o == 0 ? "OCT" : "", buf, &hit, rd);
        if (hit) { p.osc[o].octave = constrain(p.osc[o].octave + hit, -2, 2); wolf.setOscTune(o); wolfDirty = true; }
        snprintf(buf, sizeof(buf), "%+d", p.osc[o].semis);
        wolfSpin(178, y, o == 0 ? "SEMI" : "", buf, &hit, rd);
        if (hit) { p.osc[o].semis = constrain(p.osc[o].semis + hit, -12, 12); wolf.setOscTune(o); wolfDirty = true; }
        snprintf(buf, sizeof(buf), "%+.0fc", p.osc[o].fine);
        wolfSpin(256, y, o == 0 ? "FINE" : "", buf, &hit, rd);
        if (hit) { p.osc[o].fine = constrain(p.osc[o].fine + hit * 2, -50.f, 50.f); wolf.setOscTune(o); wolfDirty = true; }
      }
      break;
    }
    case 1: {  // ── MIX: levels, PW, glide, volume ────────────────────────────
      static float lv[3], pw, gl, vol;
      lv[0] = p.osc[0].level; lv[1] = p.osc[1].level; lv[2] = p.osc[2].level;
      pw = (p.osc[0].duty - 0.02f) / 0.96f;
      gl = p.glideMs / 500.0f; vol = p.volume;
      for (int o = 0; o < 3; o++) {
        snprintf(buf, sizeof(buf), "OSC%d LEVEL", o + 1);
        if (hSlider(10, WBODY_Y + 16 + o * 34, 140, 16, buf, &lv[o], rd))
          wolf.setOscLevel(o, lv[o]);
      }
      if (hSlider(10, WBODY_Y + 16 + 3 * 34, 140, 16, "PULSE WIDTH", &pw, rd)) {
        float d = 0.02f + pw * 0.96f;
        for (int o = 0; o < 3; o++) wolf.setOscDuty(o, d);
      }
      if (hSlider(170, WBODY_Y + 16, 140, 16, "GLIDE", &gl, rd)) wolf.setGlide((uint16_t)(gl * 500));
      if (hSlider(170, WBODY_Y + 50, 140, 16, "VOLUME", &vol, rd)) wolf.setVolume(vol);
      static float rev, cho;
      rev = p.reverb; cho = p.chorus;
      if (hSlider(170, WBODY_Y + 84, 140, 16, "REVERB", &rev, rd)) { p.reverb = rev; wolf.sendEffects(); }
      if (hSlider(170, WBODY_Y + 118, 140, 16, "CHORUS", &cho, rd)) { p.chorus = cho; wolf.sendEffects(); }
      break;
    }
    case 2: {  // ── FILTER ────────────────────────────────────────────────────
      if (rd) drawButton(10, WBODY_Y + 6, 70, 24, FILT_NAMES_UI[p.filterType], THEME_ACCENT);
      if (tapped(10, WBODY_Y + 6, 70, 24)) {
        p.filterType = (p.filterType + 1) % 5;
        wolf.setFilter();
        drawButton(10, WBODY_Y + 6, 70, 24, FILT_NAMES_UI[p.filterType], THEME_ACCENT);
      }
      static float cut, res, env, kt;
      cut = log10f(p.cutoff / 40.0f) / log10f(400.0f);
      res = (p.resonance - 0.1f) / 7.9f;
      env = (p.envAmount + 6.0f) / 12.0f;
      kt  = p.keyTrack;
      if (hSlider(10, WBODY_Y + 60, 300, 18, "CUTOFF", &cut, rd)) {
        p.cutoff = 40.0f * powf(400.0f, cut); wolf.setFilter();
      }
      if (hSlider(10, WBODY_Y + 98, 140, 16, "RESONANCE", &res, rd)) {
        p.resonance = 0.1f + res * 7.9f; wolf.setFilter();
      }
      if (hSlider(170, WBODY_Y + 98, 140, 16, "ENV AMT", &env, rd)) {
        p.envAmount = env * 12.0f - 6.0f; wolf.setFilter();
      }
      if (hSlider(10, WBODY_Y + 132, 140, 16, "KEY TRACK", &kt, rd)) {
        p.keyTrack = kt; wolf.setFilter();
      }
      break;
    }
    case 3: {  // ── ENVELOPES: amp + filter ADSR ──────────────────────────────
      static float a[8];
      a[0] = sqrtf(p.ampEnv.a / 4000);  a[1] = sqrtf(p.ampEnv.d / 4000);
      a[2] = p.ampEnv.s;                a[3] = sqrtf(p.ampEnv.r / 6000);
      a[4] = sqrtf(p.filtEnv.a / 4000); a[5] = sqrtf(p.filtEnv.d / 4000);
      a[6] = p.filtEnv.s;               a[7] = sqrtf(p.filtEnv.r / 6000);
      static const char* const L[8] = { "AMP A", "AMP D", "AMP S", "AMP R",
                                        "FLT A", "FLT D", "FLT S", "FLT R" };
      bool ampCh = false, fltCh = false;
      for (int i = 0; i < 8; i++) {
        int col = i & 1, row = i >> 1;
        bool ch = hSlider(10 + col * 160, WBODY_Y + 16 + row * 34, 140, 15, L[i], &a[i], rd);
        if (ch) (i < 4 ? ampCh : fltCh) = true;
      }
      if (ampCh) {
        p.ampEnv = WolfEnv(a[0]*a[0]*4000, a[1]*a[1]*4000, a[2], a[3]*a[3]*6000);
        wolf.setAmpEnv();
      }
      if (fltCh) {
        p.filtEnv = WolfEnv(a[4]*a[4]*4000, a[5]*a[5]*4000, a[6], a[7]*a[7]*6000);
        wolf.setFiltEnv();
      }
      break;
    }
    case 4: {  // ── LFO ───────────────────────────────────────────────────────
      if (rd) drawButton(10, WBODY_Y + 6, 70, 24, waveNameUI(p.lfoWave), THEME_ACCENT);
      if (tapped(10, WBODY_Y + 6, 70, 24)) {
        p.lfoWave = nextWave(p.lfoWave);
        wolf.setLFO();
        drawButton(10, WBODY_Y + 6, 70, 24, waveNameUI(p.lfoWave), THEME_ACCENT);
      }
      static float rate, f, pi, am, pwm;
      rate = sqrtf(p.lfoRate / 25.0f);
      f = p.lfoToFilter; pi = p.lfoToPitch; am = p.lfoToAmp; pwm = p.lfoToPwm;
      if (hSlider(100, WBODY_Y + 10, 210, 16, "RATE", &rate, rd)) {
        p.lfoRate = fmaxf(0.05f, rate * rate * 25.0f); wolf.setLFO();
      }
      if (hSlider(10, WBODY_Y + 60, 140, 16, "> FILTER", &f, rd))  { p.lfoToFilter = f;  wolf.setLFO(); }
      if (hSlider(170, WBODY_Y + 60, 140, 16, "> PITCH", &pi, rd)) { p.lfoToPitch = pi;  wolf.setLFO(); }
      if (hSlider(10, WBODY_Y + 98, 140, 16, "> AMP", &am, rd))    { p.lfoToAmp = am;    wolf.setLFO(); }
      if (hSlider(170, WBODY_Y + 98, 140, 16, "> PWM", &pwm, rd))  { p.lfoToPwm = pwm;   wolf.setLFO(); }
      break;
    }
  }
  wolfKeyboardTouch();
}

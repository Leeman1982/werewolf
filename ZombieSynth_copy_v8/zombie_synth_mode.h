#ifndef ZOMBIE_SYNTH_MODE_H
#define ZOMBIE_SYNTH_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "synth_engine.h"
#include "zombie_lfo.h"

// ZOMBIE SS Prophet-8 Style Synthesizer UI
// Black background, red text, white outlines

static SynthEngine* zombieSynth = NULL;

struct ZombieSynthParams {
  int   osc1Wave;
  float osc1Level;
  int   osc2Wave;
  float osc2Level;
  int   filterType;
  float filterCutoff;
  float filterResonance;
  float filterEnvAmount;
  float ampAttack;
  float ampDecay;
  float ampSustain;
  float ampRelease;
  float filterAttack;
  float filterDecay;
  float filterSustain;
  float filterRelease;
  float masterVolume;
  // FX
  float fxSatDrive;
  float fxSatAmount;
  float fxDelayMix;
  float fxDelayFeedback;
  int   fxDelayDiv;     // 0..3 → 1/4, 1/8d, 1/8, 1/16  (at 120 BPM reference)
  int   currentPage;
  bool  needsRedraw;
  int   activeSlider;
};

static ZombieSynthParams synthParams;

// Exposed for other modules
extern LFOEngine  globalLFO;
extern int        lastPlayedMidiNote;

const char* waveNames[]   = {"SAW","SQR","TRI","SIN","PLS","SSQ","COS"};
const char* filterNames[] = {"LP","HP","BP","NOTCH"};

// Returns "C4", "D#3" etc. for a MIDI note number
static const char* midiNoteToName(int note) {
  if (note < 0) return "--";
  static char buf[6];
  const char* noteNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
  int octave = note / 12 - 1;
  snprintf(buf, sizeof(buf), "%s%d", noteNames[note % 12], octave);
  return buf;
}

void zombieSynthInit() {
  if (zombieSynth == NULL) {
    zombieSynth = new SynthEngine();
    zombieSynth->init();
  }

  synthParams.osc1Wave = WAVE_SAW;  synthParams.osc1Level = 0.5f;
  synthParams.osc2Wave = WAVE_SAW;  synthParams.osc2Level = 0.5f;
  synthParams.filterType = FILTER_LOWPASS;
  synthParams.filterCutoff     = 0.8f;
  synthParams.filterResonance  = 0.3f;
  synthParams.filterEnvAmount  = 0.5f;
  synthParams.ampAttack   = 0.01f; synthParams.ampDecay   = 0.3f;
  synthParams.ampSustain  = 0.7f;  synthParams.ampRelease  = 0.5f;
  synthParams.filterAttack  = 0.01f; synthParams.filterDecay  = 0.3f;
  synthParams.filterSustain = 0.5f;  synthParams.filterRelease = 0.3f;
  synthParams.masterVolume = 0.7f;
  // FX defaults (all off)
  synthParams.fxSatDrive      = 0.0f;
  synthParams.fxSatAmount     = 0.0f;
  synthParams.fxDelayMix      = 0.0f;
  synthParams.fxDelayFeedback = 0.35f;
  synthParams.fxDelayDiv      = 2;     // 1/8
  synthParams.currentPage  = 0;
  synthParams.needsRedraw  = true;
  synthParams.activeSlider = -1;

  zombieSynth->setOsc1Waveform((WaveformType)synthParams.osc1Wave);
  zombieSynth->setOsc2Waveform((WaveformType)synthParams.osc2Wave);
  zombieSynth->setOsc1Level(synthParams.osc1Level);
  zombieSynth->setOsc2Level(synthParams.osc2Level);
  zombieSynth->setFilterType((FilterType)synthParams.filterType);
  zombieSynth->setFilterCutoff(synthParams.filterCutoff);
  zombieSynth->setFilterResonance(synthParams.filterResonance);
  zombieSynth->setFilterEnvAmount(synthParams.filterEnvAmount);
  zombieSynth->setAmpEnvelope(synthParams.ampAttack, synthParams.ampDecay,
                               synthParams.ampSustain, synthParams.ampRelease);
  zombieSynth->setFilterEnvelope(synthParams.filterAttack, synthParams.filterDecay,
                                  synthParams.filterSustain, synthParams.filterRelease);
  zombieSynth->setMasterVolume(synthParams.masterVolume);
  zombieSynth->setSaturationDrive(synthParams.fxSatDrive);
  zombieSynth->setSaturationAmount(synthParams.fxSatAmount);
  // Delay is a shared master FX (also driven by the sequencer) — reflect the
  // engine's current state instead of overwriting it on every mode entry.
  synthParams.fxDelayMix      = zombieSynth->getDelayMix();
  synthParams.fxDelayFeedback = zombieSynth->getDelayFeedback();
  synthParams.fxDelayDiv      = zombieSynth->getDelayDivision();

  tft.fillScreen(THEME_BG);
}

// ─── Header ──────────────────────────────────────────────────────────────────
// 7 tabs: OSC FLTR AMP F.ENV LFO FX DLX
// Each tab: w=44, gap=1 → 7×45=315, startX=2
static const int TAB_W = 44, TAB_H = 22, TAB_Y = 55;
static const int TAB_X[] = {2, 47, 92, 137, 182, 227, 272};
static const char* TAB_NAMES[] = {"OSC", "FLTR", "AMP", "F.ENV", "LFO", "FX", "DLX"};
static const int NUM_TABS = 7;

void drawZombieHeader() {
  drawZombiHeader("PROPHET SYNTH");

  // Note name display (top-right corner)
  tft.fillRect(270, 33, 48, 14, THEME_BG);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  char noteBuf[8];
  snprintf(noteBuf, sizeof(noteBuf), " %s", midiNoteToName(lastPlayedMidiNote));
  tft.drawRightString(noteBuf, 316, 34, 2);

  // Voice count
  if (zombieSynth) {
    char buf[8];
    sprintf(buf, "%d/8V", zombieSynth->getActiveVoiceCount());
    tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
    tft.drawRightString(buf, 316, 49, 2);
  }

  // 6 page tabs
  for (int i = 0; i < NUM_TABS; i++) {
    bool sel = (i == synthParams.currentPage);
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_TEXT_DIM;
    tft.fillRoundRect(TAB_X[i], TAB_Y, TAB_W, TAB_H, 3, bg);
    tft.drawRoundRect(TAB_X[i], TAB_Y, TAB_W, TAB_H, 3, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(TAB_NAMES[i], TAB_X[i] + TAB_W/2, TAB_Y + 5, 2);
  }
}

// ─── Slider helper ───────────────────────────────────────────────────────────
void drawVerticalSlider(int x, int y, int w, int h, const char* label, float value,
                        const char* valueText = NULL) {
  tft.fillRoundRect(x, y, w, h, 4, THEME_BG);
  tft.drawRoundRect(x, y, w, h, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(label, x + w/2, y + 4, 2);

  int trackX = x + w/2 - 8;
  int trackY = y + 24;
  int trackW = 16;
  int trackH = h - 48;
  tft.fillRect(trackX, trackY, trackW, trackH, THEME_BG);
  tft.drawRect(trackX, trackY, trackW, trackH, THEME_OUTLINE);

  int fillH = (int)(trackH * value);
  if (fillH > 0)
    tft.fillRect(trackX+1, trackY + trackH - fillH, trackW-2, fillH, THEME_PRIMARY);

  tft.setTextColor(THEME_ACCENT, THEME_BG);
  if (valueText) {
    tft.drawCentreString(valueText, x + w/2, y + h - 14, 2);
  } else {
    char buf[8];
    sprintf(buf, "%d", (int)(value * 100));
    tft.drawCentreString(buf, x + w/2, y + h - 14, 2);
  }
}

void drawButton(int x, int y, int w, int h, const char* text, bool selected = false) {
  uint16_t bg  = selected ? THEME_PRIMARY : THEME_BG;
  uint16_t txt = selected ? THEME_BG : THEME_PRIMARY;
  tft.fillRoundRect(x, y, w, h, 4, bg);
  tft.drawRoundRect(x, y, w, h, 4, THEME_OUTLINE);
  tft.setTextColor(txt, bg);
  tft.drawCentreString(text, x + w/2, y + h/2 - 8, 2);
}

bool handleSliderTouch(int sx, int sy, int sw, int sh, float& value) {
  if (touch.isPressed && touch.x >= sx && touch.x <= sx+sw &&
      touch.y >= sy && touch.y <= sy+sh) {
    int trackY = sy + 24;
    int trackH = sh - 48;
    value = 1.0f - (float)(touch.y - trackY) / (float)trackH;
    value = constrain(value, 0.0f, 1.0f);
    return true;
  }
  return false;
}

// ─── OSC page ────────────────────────────────────────────────────────────────
void zombieSynthDrawOscPage() {
  drawVerticalSlider(10, 83, 65, 150, "OSC1", synthParams.osc1Level);
  drawButton(80, 93, 50, 24, waveNames[synthParams.osc1Wave], true);
  drawButton(80, 122, 23, 24, "<", false);
  drawButton(107, 122, 23, 24, ">", false);
  drawVerticalSlider(140, 83, 65, 150, "OSC2", synthParams.osc2Level);
  drawButton(210, 93, 50, 24, waveNames[synthParams.osc2Wave], true);
  drawButton(210, 122, 23, 24, "<", false);
  drawButton(237, 122, 23, 24, ">", false);
  drawVerticalSlider(268, 83, 48, 150, "VOL", synthParams.masterVolume);
}

// ─── Filter page ─────────────────────────────────────────────────────────────
void zombieSynthDrawFilterPage() {
  drawVerticalSlider(10, 83, 65, 150, "CUTOFF", synthParams.filterCutoff);
  drawVerticalSlider(80, 83, 65, 150, "RESO",   synthParams.filterResonance);
  drawVerticalSlider(150, 83, 65, 150, "ENV",   synthParams.filterEnvAmount);
  drawButton(225, 93, 85, 28, filterNames[synthParams.filterType], true);
  drawButton(225, 128, 40, 24, "<", false);
  drawButton(270, 128, 40, 24, ">", false);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("TYPE", 267, 160, 2);
}

// ─── Amp envelope page ───────────────────────────────────────────────────────
void zombieSynthDrawAmpEnvPage() {
  char buf[20];
  sprintf(buf, "%.2fs", synthParams.ampAttack);
  drawVerticalSlider(10, 83, 70, 150, "ATK", synthParams.ampAttack/2.0f, buf);
  sprintf(buf, "%.2fs", synthParams.ampDecay);
  drawVerticalSlider(85, 83, 70, 150, "DEC", synthParams.ampDecay, buf);
  drawVerticalSlider(160, 83, 70, 150, "SUS", synthParams.ampSustain);
  sprintf(buf, "%.2fs", synthParams.ampRelease);
  drawVerticalSlider(235, 83, 70, 150, "REL", synthParams.ampRelease, buf);
}

// ─── Filter envelope page ────────────────────────────────────────────────────
void zombieSynthDrawFilterEnvPage() {
  char buf[20];
  sprintf(buf, "%.2fs", synthParams.filterAttack);
  drawVerticalSlider(10, 83, 70, 150, "ATK", synthParams.filterAttack/2.0f, buf);
  sprintf(buf, "%.2fs", synthParams.filterDecay);
  drawVerticalSlider(85, 83, 70, 150, "DEC", synthParams.filterDecay, buf);
  drawVerticalSlider(160, 83, 70, 150, "SUS", synthParams.filterSustain);
  sprintf(buf, "%.2fs", synthParams.filterRelease);
  drawVerticalSlider(235, 83, 70, 150, "REL", synthParams.filterRelease, buf);
}

// ─── LFO page ────────────────────────────────────────────────────────────────
void zombieSynthDrawLFOPage() {
  // Wave type row (y=83..108)
  for (int i = 0; i < 5; i++) {
    bool sel = (i == (int)globalLFO.wave);
    int wx = 5 + i * 62;
    drawButton(wx, 83, 59, 25, lfoWaveNames[i], sel);
  }

  // Rate slider (0-20 Hz)
  char rateBuf[12];
  sprintf(rateBuf, "%.1fHz", globalLFO.rate);
  drawVerticalSlider(12, 113, 65, 108, "RATE", globalLFO.rate / 20.0f, rateBuf);

  // Depth slider
  char depBuf[8];
  sprintf(depBuf, "%d%%", (int)(globalLFO.depth * 100));
  drawVerticalSlider(85, 113, 65, 108, "DEPTH", globalLFO.depth, depBuf);

  // Enable/Disable button
  bool en = globalLFO.enabled;
  tft.fillRoundRect(160, 113, 150, 30, 4, en ? THEME_PRIMARY : THEME_BG);
  tft.drawRoundRect(160, 113, 150, 30, 4, THEME_OUTLINE);
  tft.setTextColor(en ? THEME_BG : THEME_PRIMARY, en ? THEME_PRIMARY : THEME_BG);
  tft.drawCentreString(en ? "LFO: ON" : "LFO: OFF", 235, 121, 2);

  // Target row (FILTER / PITCH / AMP)
  const char* tnames[] = {"FILTER", "PITCH", "AMP"};
  for (int i = 0; i < 3; i++) {
    bool sel = (i == (int)globalLFO.target);
    tft.fillRoundRect(160 + i*50, 150, 47, 24, 3, sel ? THEME_PRIMARY : THEME_BG);
    tft.drawRoundRect(160 + i*50, 150, 47, 24, 3, THEME_OUTLINE);
    tft.setTextColor(sel ? THEME_BG : THEME_PRIMARY, sel ? THEME_PRIMARY : THEME_BG);
    tft.drawCentreString(tnames[i], 160 + i*50 + 23, 156, 2);
  }

  // Target label
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("TARGET", 235, 178, 2);
}

// ─── FX page ─────────────────────────────────────────────────────────────────
// SAT DRV | SAT AMT  ||  DLY MIX | DLY FBK | DLY TIME
// Chorus was removed; the freed slots now host the master tape delay.
static const char* DLY_DIV_NAMES[4] = {"1/4","1/8d","1/8","1/16"};
void zombieSynthDrawFXPage() {
  drawVerticalSlider(5,   83, 58, 140, "SAT DRV", synthParams.fxSatDrive);
  drawVerticalSlider(68,  83, 58, 140, "SAT AMT", synthParams.fxSatAmount);

  // Visual divider between saturation and delay sections
  tft.drawLine(131, 86, 131, 220, THEME_TEXT_DIM);

  drawVerticalSlider(135, 83, 58, 140, "DLY MIX", synthParams.fxDelayMix);
  drawVerticalSlider(198, 83, 58, 140, "DLY FBK", synthParams.fxDelayFeedback);

  // Delay time selector (button stack instead of a slider) – x=261..316
  tft.fillRoundRect(261, 83, 56, 140, 4, THEME_BG);
  tft.drawRoundRect(261, 83, 56, 140, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString("DLY", 289, 87, 2);
  drawButton(266, 108, 46, 24, "<>", false);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(DLY_DIV_NAMES[constrain(synthParams.fxDelayDiv, 0, 3)], 289, 150, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("TIME", 289, 175, 2);

  // Section labels (fully visible: y=225 + 14 px font-2 = 239 < 240)
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("SATURATION", 63, 225, 2);
  tft.drawCentreString("DELAY", 226, 225, 2);
}

// ─── DLX page (SYNTHWAVE DELUXE) ──────────────────────────────────────────────
// Top row: OSC1/OSC2 octave spinners + VEL curve.  Sliders: GLIDE PWM COMP GATE OUT.
static const char* VELCURVE_NAMES[3] = {"LIN","SOFT","HARD"};
static void dlxSpin(int x, const char* lbl, const char* val) {
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString(lbl, x+33, 84, 2);
  drawButton(x,    97, 20, 20, "-", false);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(val, x+33, 100, 2);
  drawButton(x+46, 97, 20, 20, "+", false);
}
void zombieSynthDrawDLXPage() {
  if (!zombieSynth) return;
  char b[8];
  // Octave spinners + vel curve (y=83..117)
  snprintf(b, sizeof(b), "%+d", zombieSynth->getOsc1Octave());
  dlxSpin(6, "O1 OCT", b);
  snprintf(b, sizeof(b), "%+d", zombieSynth->getOsc2Octave());
  dlxSpin(82, "O2 OCT", b);
  // VEL curve button (x=170..250)
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("VEL", 210, 84, 2);
  drawButton(170, 97, 80, 20, VELCURVE_NAMES[constrain(zombieSynth->getVelCurve(),0,2)], true);
  // SUB level spinner (reuse) x=256
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("SUB", 289, 84, 2);
  snprintf(b, sizeof(b), "%d", (int)(zombieSynth->getSubLevel()*100));
  drawButton(256, 97, 20, 20, "-", false);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString(b, 289, 100, 2);
  drawButton(302, 97, 14, 20, "+", false);

  // 5 sliders (y=122..230, h=108)
  char gv[10];
  snprintf(gv, sizeof(gv), "%.2fs", zombieSynth->getGlideTime());
  drawVerticalSlider(4,   122, 60, 108, "GLIDE", zombieSynth->getGlideTime()/0.6f, gv);
  drawVerticalSlider(67,  122, 60, 108, "PWM",   zombieSynth->getPWMDepth()/0.45f);
  drawVerticalSlider(130, 122, 60, 108, "COMP",  zombieSynth->getCompAmount());
  drawVerticalSlider(193, 122, 60, 108, "GATE",  zombieSynth->getGateThresh()/0.1f);
  drawVerticalSlider(256, 122, 60, 108, "OUT",   zombieSynth->getOutGain()/2.0f);
}

// ─── Main draw ───────────────────────────────────────────────────────────────
void zombieSynthDraw() {
  if (!synthParams.needsRedraw) return;

  drawZombieHeader();
  tft.fillRect(0, 80, 320, 160, THEME_BG);

  switch (synthParams.currentPage) {
    case 0: zombieSynthDrawOscPage();       break;
    case 1: zombieSynthDrawFilterPage();    break;
    case 2: zombieSynthDrawAmpEnvPage();    break;
    case 3: zombieSynthDrawFilterEnvPage(); break;
    case 4: zombieSynthDrawLFOPage();       break;
    case 5: zombieSynthDrawFXPage();        break;
    case 6: zombieSynthDrawDLXPage();       break;
  }

  synthParams.needsRedraw = false;
}

// ─── Touch handler ────────────────────────────────────────────────────────────
void zombieSynthHandleTouch() {
  if (!touch.justPressed && !touch.isPressed) return;

  if (touch.justPressed && isButtonPressed(5, 5, 55, 20)) { exitToMenu(); return; }

  // Page tabs
  if (touch.justPressed) {
    for (int i = 0; i < NUM_TABS; i++) {
      if (isButtonPressed(TAB_X[i], TAB_Y, TAB_W, TAB_H)) {
        synthParams.currentPage = i;
        synthParams.needsRedraw = true;
        return;
      }
    }
  }

  bool changed = false;

  switch (synthParams.currentPage) {
    case 0: {
      if (handleSliderTouch(10, 83, 65, 150, synthParams.osc1Level))  { zombieSynth->setOsc1Level(synthParams.osc1Level);  changed = true; }
      if (handleSliderTouch(140, 83, 65, 150, synthParams.osc2Level)) { zombieSynth->setOsc2Level(synthParams.osc2Level);  changed = true; }
      if (handleSliderTouch(268, 83, 48, 150, synthParams.masterVolume)) {
        zombieSynth->setMasterVolume(synthParams.masterVolume);
        changed = true;
      }
      if (touch.justPressed) {
        if (isButtonPressed(80, 122, 23, 24)) { synthParams.osc1Wave = (synthParams.osc1Wave-1+NUM_WAVEFORMS)%NUM_WAVEFORMS; zombieSynth->setOsc1Waveform((WaveformType)synthParams.osc1Wave); changed = true; }
        if (isButtonPressed(107,122, 23, 24)) { synthParams.osc1Wave = (synthParams.osc1Wave+1)%NUM_WAVEFORMS;   zombieSynth->setOsc1Waveform((WaveformType)synthParams.osc1Wave); changed = true; }
        if (isButtonPressed(210,122, 23, 24)) { synthParams.osc2Wave = (synthParams.osc2Wave-1+NUM_WAVEFORMS)%NUM_WAVEFORMS; zombieSynth->setOsc2Waveform((WaveformType)synthParams.osc2Wave); changed = true; }
        if (isButtonPressed(237,122, 23, 24)) { synthParams.osc2Wave = (synthParams.osc2Wave+1)%NUM_WAVEFORMS;   zombieSynth->setOsc2Waveform((WaveformType)synthParams.osc2Wave); changed = true; }
      }
      break;
    }
    case 1: {
      if (handleSliderTouch(10, 83, 65, 150, synthParams.filterCutoff))    { zombieSynth->setFilterCutoff(synthParams.filterCutoff);       changed = true; }
      if (handleSliderTouch(80, 83, 65, 150, synthParams.filterResonance)) { zombieSynth->setFilterResonance(synthParams.filterResonance); changed = true; }
      if (handleSliderTouch(150,83, 65, 150, synthParams.filterEnvAmount)) { zombieSynth->setFilterEnvAmount(synthParams.filterEnvAmount); changed = true; }
      if (touch.justPressed) {
        if (isButtonPressed(225,128, 40, 24)) { synthParams.filterType=(synthParams.filterType-1+4)%4; zombieSynth->setFilterType((FilterType)synthParams.filterType); changed=true; }
        if (isButtonPressed(270,128, 40, 24)) { synthParams.filterType=(synthParams.filterType+1)%4;   zombieSynth->setFilterType((FilterType)synthParams.filterType); changed=true; }
      }
      break;
    }
    case 2: {
      if (handleSliderTouch(10, 83, 70, 150, synthParams.ampAttack))  { synthParams.ampAttack*=2.0f; zombieSynth->setAmpEnvelope(synthParams.ampAttack,synthParams.ampDecay,synthParams.ampSustain,synthParams.ampRelease); changed=true; }
      if (handleSliderTouch(85, 83, 70, 150, synthParams.ampDecay))   { zombieSynth->setAmpEnvelope(synthParams.ampAttack,synthParams.ampDecay,synthParams.ampSustain,synthParams.ampRelease); changed=true; }
      if (handleSliderTouch(160,83, 70, 150, synthParams.ampSustain)) { zombieSynth->setAmpEnvelope(synthParams.ampAttack,synthParams.ampDecay,synthParams.ampSustain,synthParams.ampRelease); changed=true; }
      if (handleSliderTouch(235,83, 70, 150, synthParams.ampRelease)) { zombieSynth->setAmpEnvelope(synthParams.ampAttack,synthParams.ampDecay,synthParams.ampSustain,synthParams.ampRelease); changed=true; }
      break;
    }
    case 3: {
      if (handleSliderTouch(10, 83, 70, 150, synthParams.filterAttack))  { synthParams.filterAttack*=2.0f; zombieSynth->setFilterEnvelope(synthParams.filterAttack,synthParams.filterDecay,synthParams.filterSustain,synthParams.filterRelease); changed=true; }
      if (handleSliderTouch(85, 83, 70, 150, synthParams.filterDecay))   { zombieSynth->setFilterEnvelope(synthParams.filterAttack,synthParams.filterDecay,synthParams.filterSustain,synthParams.filterRelease); changed=true; }
      if (handleSliderTouch(160,83, 70, 150, synthParams.filterSustain)) { zombieSynth->setFilterEnvelope(synthParams.filterAttack,synthParams.filterDecay,synthParams.filterSustain,synthParams.filterRelease); changed=true; }
      if (handleSliderTouch(235,83, 70, 150, synthParams.filterRelease)) { zombieSynth->setFilterEnvelope(synthParams.filterAttack,synthParams.filterDecay,synthParams.filterSustain,synthParams.filterRelease); changed=true; }
      break;
    }
    case 4: { // LFO page
      if (touch.justPressed) {
        // Wave type buttons
        for (int i = 0; i < 5; i++) {
          if (isButtonPressed(5 + i*62, 83, 59, 25)) {
            globalLFO.wave = (LFOWave)i;
            changed = true;
          }
        }
        // Enable/disable toggle
        if (isButtonPressed(160, 113, 150, 30)) {
          globalLFO.enabled = !globalLFO.enabled;
          changed = true;
        }
        // Target buttons
        for (int i = 0; i < 3; i++) {
          if (isButtonPressed(160 + i*50, 150, 47, 24)) {
            globalLFO.target = (LFOTarget)i;
            changed = true;
          }
        }
      }
      // Rate slider
      float rateNorm = globalLFO.rate / 20.0f;
      if (handleSliderTouch(12, 113, 65, 108, rateNorm)) {
        globalLFO.rate = rateNorm * 20.0f;
        changed = true;
      }
      // Depth slider
      if (handleSliderTouch(85, 113, 65, 108, globalLFO.depth)) {
        globalLFO.enabled = (globalLFO.depth > 0.001f);
        changed = true;
      }
      break;
    }
    case 5: { // FX page
      if (handleSliderTouch(5,  83, 58, 140, synthParams.fxSatDrive))      { zombieSynth->setSaturationDrive(synthParams.fxSatDrive);        changed = true; }
      if (handleSliderTouch(68, 83, 58, 140, synthParams.fxSatAmount))     { zombieSynth->setSaturationAmount(synthParams.fxSatAmount);      changed = true; }
      if (handleSliderTouch(135,83, 58, 140, synthParams.fxDelayMix))      { zombieSynth->setDelayMix(synthParams.fxDelayMix);               changed = true; }
      if (handleSliderTouch(198,83, 58, 140, synthParams.fxDelayFeedback)) { zombieSynth->setDelayFeedback(synthParams.fxDelayFeedback);     changed = true; }
      if (touch.justPressed && isButtonPressed(266, 108, 46, 24)) {
        synthParams.fxDelayDiv = (synthParams.fxDelayDiv + 1) & 3;
        zombieSynth->setDelayDivision(120.0f, synthParams.fxDelayDiv);
        changed = true;
      }
      break;
    }
    case 6: { // DLX page
      // Sliders (continuous)
      float gl = zombieSynth->getGlideTime()/0.6f;
      if (handleSliderTouch(4,  122, 60, 108, gl))  { zombieSynth->setGlideTime(gl*0.6f);   changed = true; }
      float pw = zombieSynth->getPWMDepth()/0.45f;
      if (handleSliderTouch(67, 122, 60, 108, pw))  { zombieSynth->setPWMDepth(pw*0.45f);
                                                      if (zombieSynth->getPWMRate()<0.1f) zombieSynth->setPWMRate(4.0f);
                                                      changed = true; }
      float cp = zombieSynth->getCompAmount();
      if (handleSliderTouch(130,122, 60, 108, cp))  { zombieSynth->setCompAmount(cp);       changed = true; }
      float ga = zombieSynth->getGateThresh()/0.1f;
      if (handleSliderTouch(193,122, 60, 108, ga))  { zombieSynth->setGateThresh(ga*0.1f);  changed = true; }
      float og = zombieSynth->getOutGain()/2.0f;
      if (handleSliderTouch(256,122, 60, 108, og))  { zombieSynth->setOutGain(og*2.0f);     changed = true; }
      // Buttons (justPressed)
      if (touch.justPressed) {
        if (isButtonPressed(6,  97, 20, 20)) { zombieSynth->setOsc1Octave(zombieSynth->getOsc1Octave()-1); changed=true; }
        if (isButtonPressed(52, 97, 20, 20)) { zombieSynth->setOsc1Octave(zombieSynth->getOsc1Octave()+1); changed=true; }
        if (isButtonPressed(82, 97, 20, 20)) { zombieSynth->setOsc2Octave(zombieSynth->getOsc2Octave()-1); changed=true; }
        if (isButtonPressed(128,97, 20, 20)) { zombieSynth->setOsc2Octave(zombieSynth->getOsc2Octave()+1); changed=true; }
        if (isButtonPressed(170,97, 80, 20)) { zombieSynth->setVelCurve((zombieSynth->getVelCurve()+1)%3); changed=true; }
        if (isButtonPressed(256,97, 20, 20)) { zombieSynth->setSubLevel(zombieSynth->getSubLevel()-0.05f); changed=true; }
        if (isButtonPressed(302,97, 14, 20)) { zombieSynth->setSubLevel(zombieSynth->getSubLevel()+0.05f); changed=true; }
      }
      break;
    }
  }

  if (changed) synthParams.needsRedraw = true;
}

void zombieSynthUpdate() {
  // Refresh note-name display in header without triggering full redraw
  static int prevNote = -2;
  if (lastPlayedMidiNote != prevNote) {
    prevNote = lastPlayedMidiNote;
    tft.fillRect(270, 33, 48, 14, THEME_BG);
    char noteBuf[8];
    snprintf(noteBuf, sizeof(noteBuf), " %s", midiNoteToName(lastPlayedMidiNote));
    tft.setTextColor(THEME_ACCENT, THEME_BG);
    tft.drawRightString(noteBuf, 316, 34, 2);
  }
}

SynthEngine* getZombieSynth() {
  return zombieSynth;
}

#endif

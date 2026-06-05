#ifndef ZOMBIE_PRESETS_MODE_H
#define ZOMBIE_PRESETS_MODE_H

#include "common_definitions.h"
#include "ui_elements.h"
#include "zombie_presets.h"
#include "zombie_keyboard_input.h"
#include "zombie_synth_mode.h"
#include "zombie_lfo.h"

// Forward declarations
extern LFOEngine globalLFO;
void applySynthPatch(const SynthPatch& p);
void collectSynthPatch(SynthPatch& p, const char* name);

static PresetManager presetMgr;
static int   presetSelected   = 0;
static int   presetPage       = 0;   // 0=factory, 1=user
static bool  presetNeedsRedraw= true;
static bool  presetsInited    = false;

static NameKeyboard nameKb;

void zombiePresetsInit() {
  if (!presetsInited) {
    presetMgr.init();
    presetsInited = true;
  }
  presetSelected  = 0;
  presetPage      = 0;
  presetNeedsRedraw = true;
  nameKb.active   = false;
  tft.fillScreen(THEME_BG);
}

void zombiePresetsDraw() {
  if (!presetNeedsRedraw) return;

  // Header
  drawZombiHeader("PRESETS");

  // Tab row: FACTORY / USER
  const char* tabs[] = {"FACTORY", "USER"};
  for (int t = 0; t < 2; t++) {
    bool sel = (t == presetPage);
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(8 + t*156, 55, 150, 22, 4, bg);
    tft.drawRoundRect(8 + t*156, 55, 150, 22, 4, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    tft.drawCentreString(tabs[t], 8 + t*156 + 75, 60, 2);
  }

  // Preset list - 8 visible slots
  int startSlot = presetPage * NUM_FACTORY;
  int count     = (presetPage == 0) ? NUM_FACTORY : (NUM_PRESETS - NUM_FACTORY);
  tft.fillRect(0, 82, 320, 120, THEME_BG);
  for (int i = 0; i < min(count, 8); i++) {
    int slot  = startSlot + i;
    bool sel  = (slot == presetSelected);
    int  x=8, y = 83 + i*15, w=304, h=14;
    uint16_t bg  = sel ? THEME_PRIMARY : THEME_BG;
    uint16_t txt = sel ? THEME_BG : THEME_PRIMARY;
    tft.fillRoundRect(x, y, w, h, 2, bg);
    tft.drawRoundRect(x, y, w, h, 2, THEME_OUTLINE);
    tft.setTextColor(txt, bg);
    char line[30];
    snprintf(line, sizeof(line), "%2d: %s", slot+1, presetMgr.getName(slot));
    tft.drawString(line, x+5, y+2, 2);
  }

  // Action buttons
  tft.fillRect(0, 205, 320, 35, THEME_BG);

  // LOAD
  tft.fillRoundRect(8,  207, 90, 28, 4, THEME_PRIMARY);
  tft.drawRoundRect(8,  207, 90, 28, 4, THEME_OUTLINE);
  tft.setTextColor(THEME_BG, THEME_PRIMARY);
  tft.drawCentreString("LOAD", 53, 215, 2);

  // SAVE TO SLOT (user only)
  bool canSave = (presetSelected >= NUM_FACTORY);
  uint16_t saveBg = canSave ? THEME_BG : THEME_SURFACE;
  tft.fillRoundRect(102, 207, 108, 28, 4, saveBg);
  tft.drawRoundRect(102, 207, 108, 28, 4, canSave ? THEME_OUTLINE : THEME_TEXT_DIM);
  tft.setTextColor(canSave ? THEME_PRIMARY : THEME_TEXT_DIM, saveBg);
  tft.drawCentreString("SAVE HERE", 156, 215, 2);

  // RENAME (user only)
  tft.fillRoundRect(214, 207, 100, 28, 4, canSave ? THEME_BG : THEME_SURFACE);
  tft.drawRoundRect(214, 207, 100, 28, 4, canSave ? THEME_OUTLINE : THEME_TEXT_DIM);
  tft.setTextColor(canSave ? THEME_PRIMARY : THEME_TEXT_DIM, canSave ? THEME_BG : THEME_SURFACE);
  tft.drawCentreString("RENAME", 264, 215, 2);

  // Show keyboard if active
  if (nameKb.active) nameKb.draw();

  presetNeedsRedraw = false;
}

void zombiePresetsHandleTouch() {
  if (!touch.justPressed) return;

  // Keyboard overlay eats all touches
  if (nameKb.active) {
    if (nameKb.handleTouch(touch.x, touch.y)) {
      if (nameKb.done) {
        // Save with new name
        SynthPatch p;
        collectSynthPatch(p, nameKb.text);
        presetMgr.saveUserPreset(presetSelected, p);
        tft.fillScreen(THEME_BG);
      }
      presetNeedsRedraw = true;
    }
    return;
  }

  // BACK
  if (isButtonPressed(5, 5, 55, 20)) { exitToMenu(); return; }

  // Tabs
  if (isButtonPressed(8,  55, 150, 22)) { presetPage = 0; presetSelected = 0; presetNeedsRedraw = true; return; }
  if (isButtonPressed(164, 55, 150, 22)) { presetPage = 1; presetSelected = NUM_FACTORY; presetNeedsRedraw = true; return; }

  // List items
  int startSlot = presetPage * NUM_FACTORY;
  int count     = (presetPage == 0) ? NUM_FACTORY : (NUM_PRESETS - NUM_FACTORY);
  for (int i = 0; i < min(count, 8); i++) {
    if (isButtonPressed(8, 83 + i*15, 304, 14)) {
      presetSelected = startSlot + i;
      presetNeedsRedraw = true;
      return;
    }
  }

  // LOAD
  if (isButtonPressed(8, 207, 90, 28)) {
    applySynthPatch(*presetMgr.getPatch(presetSelected));
    tft.fillScreen(THEME_BG);
    presetNeedsRedraw = true;
    return;
  }

  // SAVE HERE
  if (isButtonPressed(102, 207, 108, 28) && presetSelected >= NUM_FACTORY) {
    nameKb.open(presetMgr.getName(presetSelected));
    presetNeedsRedraw = true;
    return;
  }

  // RENAME
  if (isButtonPressed(214, 207, 100, 28) && presetSelected >= NUM_FACTORY) {
    nameKb.open(presetMgr.getName(presetSelected));
    presetNeedsRedraw = true;
    return;
  }
}

void zombiePresetsUpdate() {}

// Apply a patch to the live synth engine + update synthParams
void applySynthPatch(const SynthPatch& p) {
  SynthEngine* synth = getZombieSynth();
  if (!synth) return;

  // Update synthParams struct so UI shows new values
  synthParams.osc1Wave      = p.osc1Wave;
  synthParams.osc1Level     = p.osc1Level;
  synthParams.osc2Wave      = p.osc2Wave;
  synthParams.osc2Level     = p.osc2Level;
  synthParams.filterType    = p.filterType;
  synthParams.filterCutoff  = p.filterCutoff;
  synthParams.filterResonance= p.filterResonance;
  synthParams.filterEnvAmount= p.filterEnvAmount;
  synthParams.ampAttack     = p.ampAttack;
  synthParams.ampDecay      = p.ampDecay;
  synthParams.ampSustain    = p.ampSustain;
  synthParams.ampRelease    = p.ampRelease;
  synthParams.filterAttack  = p.filterAttack;
  synthParams.filterDecay   = p.filterDecay;
  synthParams.filterSustain = p.filterSustain;
  synthParams.filterRelease = p.filterRelease;
  synthParams.masterVolume  = p.masterVolume;
  synthParams.needsRedraw   = true;

  // LFO
  globalLFO.wave    = (LFOWave)p.lfoWave;
  globalLFO.rate    = p.lfoRate;
  globalLFO.depth   = p.lfoDepth;
  globalLFO.target  = (LFOTarget)p.lfoTarget;
  globalLFO.enabled = (p.lfoDepth > 0.001f);

  // Push to engine
  synth->setOsc1Waveform((WaveformType)p.osc1Wave);
  synth->setOsc2Waveform((WaveformType)p.osc2Wave);
  synth->setOsc1Level(p.osc1Level);
  synth->setOsc2Level(p.osc2Level);
  synth->setOsc2Detune(p.osc2Detune);
  synth->setFilterType((FilterType)p.filterType);
  synth->setFilterCutoff(p.filterCutoff);
  synth->setFilterResonance(p.filterResonance);
  synth->setFilterEnvAmount(p.filterEnvAmount);
  synth->setAmpEnvelope(p.ampAttack, p.ampDecay, p.ampSustain, p.ampRelease);
  synth->setFilterEnvelope(p.filterAttack, p.filterDecay, p.filterSustain, p.filterRelease);
  synth->setMasterVolume(p.masterVolume);
}

// Collect current synth state into a patch struct
void collectSynthPatch(SynthPatch& p, const char* name) {
  strncpy(p.name, name, PRESET_NAME_LEN);
  p.name[PRESET_NAME_LEN] = '\0';
  p.osc1Wave       = synthParams.osc1Wave;
  p.osc1Level      = synthParams.osc1Level;
  p.osc2Wave       = synthParams.osc2Wave;
  p.osc2Level      = synthParams.osc2Level;
  p.osc2Detune     = getZombieSynth() ? getZombieSynth()->getOsc2Detune() : 0.005f;
  p.filterType     = synthParams.filterType;
  p.filterCutoff   = synthParams.filterCutoff;
  p.filterResonance= synthParams.filterResonance;
  p.filterEnvAmount= synthParams.filterEnvAmount;
  p.ampAttack      = synthParams.ampAttack;
  p.ampDecay       = synthParams.ampDecay;
  p.ampSustain     = synthParams.ampSustain;
  p.ampRelease     = synthParams.ampRelease;
  p.filterAttack   = synthParams.filterAttack;
  p.filterDecay    = synthParams.filterDecay;
  p.filterSustain  = synthParams.filterSustain;
  p.filterRelease  = synthParams.filterRelease;
  p.lfoWave        = (int)globalLFO.wave;
  p.lfoRate        = globalLFO.rate;
  p.lfoDepth       = globalLFO.depth;
  p.lfoTarget      = (int)globalLFO.target;
  p.masterVolume   = synthParams.masterVolume;
}

#endif

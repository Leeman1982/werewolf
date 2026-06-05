#ifndef ZOMBIE_PRESETS_H
#define ZOMBIE_PRESETS_H

#include <Preferences.h>
#include "synth_engine.h"

#define NUM_PRESETS  20
#define NUM_FACTORY  10

// SynthPatch struct is defined in synth_patch.h (included via synth_engine.h).

// ── 10 factory presets – calibrated for log filter formula ───────────────────
// filterCutoff reference: 0.44≈418Hz, 0.52≈726Hz, 0.58≈1.1kHz, 0.62≈1.5kHz,
//   0.68≈2.2kHz, 0.70≈2.5kHz, 0.72≈2.9kHz, 0.77≈4.1kHz, WAVE_SAW=0 SQR=1 TRI=2 SIN=3 PLS=4
const SynthPatch factoryPresets[NUM_FACTORY] = {

  // Trailing 3 fields per preset: subWave, subLevel, subOctave (1=-1oct, 2=-2oct)
  // 0: JUNO POLY – Roland Juno-106 inspired pad
  {"JUNO POLY",  WAVE_SAW, 0.75f, WAVE_SAW, 0.55f, 0.007f,
   FILTER_LOWPASS, 0.52f, 0.30f, 0.20f,
   0.06f, 0.30f, 0.75f, 0.45f,
   0.05f, 0.30f, 0.50f, 0.35f,
   0, 0.45f, 0.10f, 0, 0.75f,
   WAVE_SQUARE, 0.0f, 1},

  // 1: MOOG BASS – Minimoog Model D bass + sub squarewave −1 oct
  {"MOOG BASS",  WAVE_SAW, 0.85f, WAVE_SAW, 0.55f, 0.003f,
   FILTER_LOWPASS, 0.44f, 0.58f, 0.52f,
   0.003f, 0.22f, 0.0f, 0.08f,
   0.003f, 0.18f, 0.0f, 0.10f,
   0, 0.0f, 0.0f, 0, 0.85f,
   WAVE_SQUARE, 0.45f, 1},

  // 2: MINI LEAD
  {"MINI LEAD",  WAVE_SAW, 0.75f, WAVE_SQUARE, 0.55f, 0.012f,
   FILTER_LOWPASS, 0.68f, 0.40f, 0.32f,
   0.006f, 0.10f, 0.85f, 0.12f,
   0.005f, 0.10f, 0.65f, 0.10f,
   0, 5.5f, 0.06f, 1, 0.80f,
   WAVE_SQUARE, 0.0f, 1},

  // 3: STAB PLUCK
  {"STAB PLUCK", WAVE_SAW, 0.90f, WAVE_SQUARE, 0.40f, 0.004f,
   FILTER_LOWPASS, 0.72f, 0.62f, 0.92f,
   0.002f, 0.28f, 0.0f, 0.12f,
   0.002f, 0.16f, 0.0f, 0.10f,
   0, 0.0f, 0.0f, 0, 0.82f,
   WAVE_SQUARE, 0.0f, 1},

  // 4: STRING PAD
  {"STRING PAD", WAVE_SAW, 0.65f, WAVE_SAW, 0.65f, 0.011f,
   FILTER_LOWPASS, 0.58f, 0.15f, 0.25f,
   0.35f, 0.40f, 0.80f, 0.80f,
   0.25f, 0.40f, 0.55f, 0.50f,
   0, 3.5f, 0.10f, 0, 0.70f,
   WAVE_SQUARE, 0.0f, 1},

  // 5: BRASS STAB
  {"BRASS STAB", WAVE_SAW, 0.80f, WAVE_PULSE, 0.60f, 0.005f,
   FILTER_LOWPASS, 0.62f, 0.38f, 0.72f,
   0.04f, 0.18f, 0.70f, 0.20f,
   0.03f, 0.18f, 0.50f, 0.15f,
   0, 0.0f, 0.0f, 0, 0.80f,
   WAVE_SQUARE, 0.0f, 1},

  // 6: METAL BELL
  {"METAL BELL", WAVE_SINE, 0.75f, WAVE_TRIANGLE, 0.55f, 0.013f,
   FILTER_BANDPASS, 0.62f, 0.65f, 0.22f,
   0.003f, 1.20f, 0.05f, 1.50f,
   0.003f, 0.60f, 0.05f, 1.00f,
   0, 0.0f, 0.0f, 0, 0.65f,
   WAVE_SQUARE, 0.0f, 1},

  // 7: HAMMOND ORG
  {"HAMMOND ORG",WAVE_SQUARE, 0.65f, WAVE_TRIANGLE, 0.55f, 0.002f,
   FILTER_LOWPASS, 0.77f, 0.12f, 0.10f,
   0.005f, 0.0f, 1.0f, 0.05f,
   0.005f, 0.0f, 0.40f, 0.05f,
   0, 6.0f, 0.10f, 2, 0.75f,
   WAVE_SQUARE, 0.0f, 1},

  // 8: REESE BASS – heavy sub-sine −1 oct for jungle low end
  {"REESE BASS", WAVE_SAW, 0.70f, WAVE_SAW, 0.70f, 0.018f,
   FILTER_LOWPASS, 0.43f, 0.52f, 0.18f,
   0.005f, 0.40f, 0.70f, 0.20f,
   0.005f, 0.20f, 0.40f, 0.20f,
   0, 0.80f, 0.14f, 0, 0.85f,
   WAVE_SINE, 0.60f, 1},

  // 9: SYNC LEAD – sub triangle −1 oct for body
  {"SYNC LEAD",  WAVE_PULSE, 0.78f, WAVE_SAW, 0.45f, 0.009f,
   FILTER_LOWPASS, 0.70f, 0.45f, 0.58f,
   0.01f, 0.10f, 0.88f, 0.15f,
   0.008f, 0.12f, 0.60f, 0.10f,
   0, 5.0f, 0.05f, 1, 0.78f,
   WAVE_TRIANGLE, 0.35f, 1},
};

class PresetManager {
private:
  Preferences prefs;
  SynthPatch userPresets[NUM_PRESETS - NUM_FACTORY];

  String slotKey(int slot) {
    return "slot_" + String(slot - NUM_FACTORY);
  }

public:
  void init() {
    prefs.begin("zombie_ss", false);
    // Legacy preset blobs (before sub-osc was added) were sizeof(SynthPatch)
    // minus 3 trailing fields (subWave/subLevel/subOctave): 2 ints + 1 float = 12 bytes.
    const size_t legacySize = sizeof(SynthPatch) - (sizeof(int) * 2 + sizeof(float));
    const size_t newSize    = sizeof(SynthPatch);

    for (int i = NUM_FACTORY; i < NUM_PRESETS; i++) {
      String key  = slotKey(i);
      SynthPatch& p = userPresets[i - NUM_FACTORY];
      if (prefs.isKey(key.c_str())) {
        size_t storedLen = prefs.getBytesLength(key.c_str());
        if (storedLen == newSize) {
          prefs.getBytes(key.c_str(), &p, newSize);
        } else if (storedLen == legacySize) {
          // Legacy blob: load old fields, then default the sub-osc fields.
          prefs.getBytes(key.c_str(), &p, legacySize);
          p.subWave   = WAVE_SQUARE;
          p.subLevel  = 0.0f;
          p.subOctave = 1;
        } else {
          // Unknown size – best effort, then zero the sub fields.
          prefs.getBytes(key.c_str(), &p, storedLen < newSize ? storedLen : newSize);
          p.subWave   = WAVE_SQUARE;
          p.subLevel  = 0.0f;
          p.subOctave = 1;
        }
      } else {
        // Empty slot defaults
        memset(&p, 0, sizeof(SynthPatch));
        strcpy(p.name, "-- EMPTY --");
        p.masterVolume = 0.7f;
        p.filterCutoff = 0.8f;
        p.osc1Wave = WAVE_SAW;
        p.osc1Level = 0.7f;
        p.ampSustain = 0.7f;
        p.subWave   = WAVE_SQUARE;
        p.subLevel  = 0.0f;
        p.subOctave = 1;
      }
    }
  }

  const char* getName(int slot) {
    if (slot < NUM_FACTORY) return factoryPresets[slot].name;
    return userPresets[slot - NUM_FACTORY].name;
  }

  const SynthPatch* getPatch(int slot) {
    if (slot < NUM_FACTORY) return &factoryPresets[slot];
    return &userPresets[slot - NUM_FACTORY];
  }

  bool isFactory(int slot) { return slot < NUM_FACTORY; }

  bool saveUserPreset(int slot, const SynthPatch& patch) {
    if (slot < NUM_FACTORY || slot >= NUM_PRESETS) return false;
    userPresets[slot - NUM_FACTORY] = patch;
    String key = slotKey(slot);
    prefs.putBytes(key.c_str(), &patch, sizeof(SynthPatch));
    return true;
  }

  void buildPatchFromParams(SynthPatch& p, const char* name) {
    strncpy(p.name, name, PRESET_NAME_LEN);
    p.name[PRESET_NAME_LEN] = '\0';
    // Caller fills remaining fields
  }
};

#endif

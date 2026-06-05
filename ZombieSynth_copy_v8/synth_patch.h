#ifndef SYNTH_PATCH_H
#define SYNTH_PATCH_H

// Shared synth patch data structure.
// Included by zombie_presets.h AND zombie_step_sequencer.h so both can use
// SynthPatch without a circular dependency.

#define PRESET_NAME_LEN 14

struct SynthPatch {
  char  name[PRESET_NAME_LEN + 1];
  // Oscillators
  int   osc1Wave;
  float osc1Level;
  int   osc2Wave;
  float osc2Level;
  float osc2Detune;   // ratio, e.g. 0.007 = 0.7 % = ~12 cents
  // Filter
  int   filterType;
  float filterCutoff;    // 0-1 log-mapped to 20-20000 Hz in Filter struct
  float filterResonance; // 0-1 (0=no reso, 1=near self-oscillation)
  float filterEnvAmount; // 0-1 added to cutoff at filter-env peak
  // Amp Envelope (seconds)
  float ampAttack;
  float ampDecay;
  float ampSustain;   // 0-1 level
  float ampRelease;
  // Filter Envelope (seconds)
  float filterAttack;
  float filterDecay;
  float filterSustain;
  float filterRelease;
  // LFO
  int   lfoWave;    // LFOWave enum value
  float lfoRate;    // Hz
  float lfoDepth;   // 0-1
  int   lfoTarget;  // LFOTarget enum value
  // Master
  float masterVolume; // 0-1
  // Sub-oscillator (3rd osc, fixed octaves below note)
  // Appended at end of struct so legacy NVS blobs (sizeof old SynthPatch)
  // can still be loaded via size-based migration in PresetManager::init().
  int   subWave;      // WaveformType
  float subLevel;     // 0-1, 0 = silent
  int   subOctave;    // 1 or 2 octaves below note
};

#endif

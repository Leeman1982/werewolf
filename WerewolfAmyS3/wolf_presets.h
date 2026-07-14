// WerewolfAmyS3 — preset bank for the WEREWOLF synth.
//
// Two kinds of preset:
//   factory >= 0 : an AMY factory patch (0-127 Juno-106, 128-255 DX7 — the
//                  whole banks are also browsable by number from the UI)
//   factory <  0 : a full custom 3-oscillator werewolf voice
#pragma once
#include "wolf_synth.h"

struct WolfPresetDef {
  const char* name;
  int16_t factory;
  // custom voice fields (ignored for factory presets):
  uint8_t w[3]; float lvl[3]; int8_t oct[3]; int8_t semi[3]; float fine[3];
  float duty;
  uint8_t ftype; float cut; float res; float envamt;
  float aa, ad, as, ar;        // amp ADSR (ms/ms/lvl/ms)
  float fa, fd, fs, fr;        // filter ADSR
  uint8_t lfoW; float lfoR, lfoF, lfoP, lfoA, lfoPwm;
  uint16_t glide; float vol, rev, cho;
};

#define SAW SAW_DOWN
#define SQR PULSE
#define TRI TRIANGLE
#define SIN SINE
#define NOI NOISE
#define LP  FILTER_LPF
#define LP4 FILTER_LPF24
#define BP  FILTER_BPF
#define HP  FILTER_HPF

static const WolfPresetDef WOLF_PRESETS[] = {
  // ── Custom werewolf voices ─────────────────────────────────────────────────
  //  name           fac  waves          levels          oct        semi       fine           duty ftyp cut    res  env   ampADSR                filtADSR              lfoW rate  f    p    a    pwm  gld  vol  rev  cho
  { "Full Moon Bass", -1, {SAW,SAW,SQR}, {1.0,0.8,0.7},  {-1,-1,-2},{0,0,0},   {0,-7,0},      0.5, LP4, 750,  2.2, 2.5,  3,180,0.5,160,   1,220,0.15,200,TRI, 4.5, 0,   0,   0,   0,   0,   0.9, 0.08,0.0 },
  { "Howl Lead",      -1, {SAW,SQR,SAW}, {1.0,0.7,0.5},  {0,0,1},   {0,0,0},   {6,-6,2},      0.4, LP4, 3200, 1.8, 1.5,  2,120,0.8,180,   4,250,0.4,220,  TRI, 5.5, 0.1, 0.25,0,   0,   45,  0.85,0.2, 0.2 },
  { "Lycan Strings",  -1, {SAW,SAW,SAW}, {0.8,0.8,0.6},  {0,0,-1},  {0,0,0},   {8,-8,3},      0.5, LP,  1400, 1.0, 1.0,  350,600,0.8,900, 400,800,0.5,900,TRI, 0.6, 0.2, 0,   0,   0,   0,   0.75,0.35,0.5 },
  { "Moonlit Pad",    -1, {TRI,SAW,SQR}, {0.8,0.5,0.4},  {0,0,-1},  {0,0,0},   {5,-5,0},      0.6, LP,  1000, 0.8, 1.2,  600,900,0.75,1400,700,1200,0.4,1500,TRI,0.35,0.3, 0,   0,   0.3, 0,   0.7, 0.45,0.6 },
  { "Silver Bullet",  -1, {SQR,SQR,SQR}, {0.9,0.7,0.5},  {0,0,1},   {0,0,7},   {4,-4,0},      0.25,BP,  2500, 3.0, 2.0,  1,90,0.3,120,    1,140,0.2,150, SQR, 6.0, 0.15,0,   0,   0.4, 0,   0.8, 0.15,0.0 },
  { "Beast Growl",    -1, {SAW,SAW,NOI}, {1.0,0.9,0.15}, {-2,-2,0}, {0,0,0},   {0,10,0},      0.5, LP4, 420,  3.5, 3.0,  4,220,0.6,200,   1,320,0.1,250, TRI, 7.0, 0.35,0,   0,   0,   0,   0.9, 0.1, 0.0 },
  { "Claw Pluck",     -1, {SQR,SAW,TRI}, {0.8,0.7,0.6},  {0,0,1},   {0,0,0},   {3,-3,0},      0.35,LP4, 5000, 1.5, 4.0,  1,180,0.0,180,   1,160,0.0,160, TRI, 4.0, 0,   0,   0,   0,   0,   0.85,0.25,0.2 },
  { "Night Stalker",  -1, {SAW,SQR,SIN}, {0.9,0.6,0.8},  {-1,-1,-2},{0,0,0},   {0,5,0},       0.45,LP4, 620,  2.8, 2.2,  2,250,0.45,240,  1,300,0.15,260,TRI, 5.0, 0.1, 0,   0,   0.25,60,  0.9, 0.12,0.0 },
  { "Transylvania",   -1, {SQR,SQR,SAW}, {0.8,0.7,0.6},  {0,-1,-1}, {0,0,0},   {6,-6,0},      0.15,LP,  1800, 1.2, 1.0,  80,400,0.7,500,  100,500,0.4,600,TRI,0.8, 0.15,0,   0,   0.5, 0,   0.75,0.4, 0.6 },
  { "Wolf Organ",     -1, {SQR,SQR,SQR}, {0.9,0.6,0.45}, {0,1,2},   {0,0,7},   {0,0,0},       0.5, LP,  6000, 0.6, 0.0,  2,60,1.0,120,    1,60,1.0,120,  TRI, 6.5, 0,   0,   0.2, 0,   0,   0.7, 0.25,0.4 },
  { "Blood Moon Acid",-1, {SAW,SAW,SAW}, {1.0,0.0,0.0},  {-1,0,0},  {0,0,0},   {0,0,0},       0.5, LP4, 500,  5.5, 4.5,  1,140,0.0,110,   1,150,0.0,140, TRI, 5.0, 0,   0,   0,   0,   70,  0.85,0.05,0.0 },
  { "Feral Sync",     -1, {SAW,SQR,SQR}, {0.9,0.8,0.6},  {0,0,0},   {0,7,12},  {0,8,-8},      0.3, BP,  1900, 2.4, 2.8,  1,200,0.5,220,   1,260,0.3,240, SQR, 3.2, 0.2, 0,   0,   0.5, 0,   0.8, 0.18,0.2 },
  { "Graveyard Keys", -1, {SIN,TRI,SIN}, {1.0,0.5,0.35}, {0,0,2},   {0,0,0},   {0,3,0},       0.5, LP,  4500, 0.5, 1.2,  2,350,0.4,420,   2,450,0.3,500, TRI, 4.8, 0,   0,   0.15,0,   0,   0.85,0.3, 0.35 },
  { "Alpha Unison",   -1, {SAW,SAW,SAW}, {1.0,0.9,0.9},  {0,0,0},   {0,0,0},   {0,12,-12},    0.5, LP4, 2600, 1.4, 1.8,  15,220,0.75,300, 20,300,0.5,350,TRI, 5.6, 0.08,0,   0,   0,   0,   0.8, 0.22,0.45 },
  { "Cursed Bells",   -1, {SIN,SIN,SIN}, {1.0,0.6,0.4},  {0,1,2},   {0,7,2},   {0,4,-3},      0.5, HP,  900,  1.0, 0.0,  1,900,0.2,1600,  1,900,0.2,1600,TRI, 0.4, 0,   0.1, 0,   0,   0,   0.8, 0.5, 0.3 },
  { "Wolf Whistle",   -1, {SIN,TRI,SIN}, {1.0,0.3,0.0},  {1,2,0},   {0,0,0},   {0,0,0},       0.5, LP,  8000, 0.3, 0.5,  60,150,0.9,200,  60,150,0.9,200,TRI, 5.5, 0,   0.35,0,   0,   120, 0.75,0.3, 0.0 },
  { "Doom Drone",     -1, {SAW,SAW,SQR}, {0.9,0.9,0.7},  {-2,-2,-1},{0,1,0},   {0,-10,5},     0.5, LP,  300,  1.6, 0.6,  1500,2000,0.85,3000,1800,2500,0.5,3500,TRI,0.15,0.4,0,  0,   0.3, 0,   0.85,0.55,0.5 },
  { "Snarl Brass",    -1, {SAW,SAW,SQR}, {1.0,0.85,0.5}, {0,0,-1},  {0,0,0},   {4,-4,0},      0.5, LP4, 1500, 1.3, 3.2,  40,260,0.7,240,  30,320,0.45,280,TRI,5.2, 0,   0,   0,   0,   0,   0.85,0.2, 0.25 },
  { "Poly Fang",      -1, {SQR,SAW,SQR}, {0.85,0.8,0.6}, {0,0,-1},  {0,0,0},   {5,-5,0},      0.42,LP4, 2100, 1.1, 1.6,  6,300,0.65,420,  8,380,0.4,460, TRI, 4.4, 0.12,0,   0,   0.3, 0,   0.8, 0.3, 0.5 },
  { "Shapeshifter",   -1, {TRI,SQR,SAW}, {0.9,0.65,0.55},{0,-1,1},  {0,0,0},   {0,6,-6},      0.5, BP,  1200, 2.0, 2.4,  200,500,0.6,700, 250,600,0.35,750,SIN,0.5, 0.45,0.15,0.2,0.45,0,  0.78,0.4, 0.55 },
  // ── AMY factory highlights (Juno-106 bank 0-127) ──────────────────────────
  { "Juno Brass",       0 }, { "Juno Trumpet",     2 }, { "Juno Flutes",      3 },
  { "Moving Strings",   4 }, { "Juno Choir",       6 }, { "Juno Piano",       7 },
  { "Juno Organ",       8 }, { "Combo Organ",     10 }, { "Donald Pluck",    12 },
  { "Celeste",         13 }, { "Elec Piano",      14 },
  // ── AMY factory highlights (DX7 bank 128-255) ──────────────────────────────
  { "DX E.Piano",     128 }, { "DX Bells",       129 }, { "DX Marimba",     130 },
  { "DX Strings",     135 }, { "DX Bass",        143 }, { "DX Brass",       150 },
  { "DX Organ",       160 }, { "DX Koto",        175 }, { "DX Voice",       190 },
  { "DX Lead",        200 }, { "DX Pad",         210 }, { "DX Wurly",       220 },
};
static const int NUM_WOLF_PRESETS = sizeof(WOLF_PRESETS) / sizeof(WOLF_PRESETS[0]);

#undef SAW
#undef SQR
#undef TRI
#undef SIN
#undef NOI
#undef LP
#undef LP4
#undef BP
#undef HP

// Apply a preset from the table to the synth.
inline void applyWolfPreset(WolfSynth& s, int idx) {
  if (idx < 0 || idx >= NUM_WOLF_PRESETS) return;
  const WolfPresetDef& d = WOLF_PRESETS[idx];
  s.currentPreset = idx;
  if (d.factory >= 0) {
    s.loadFactoryPatch(d.factory, d.factory >= 128 ? 6 : WOLF_VOICES);
    return;
  }
  WolfParams np;
  for (int i = 0; i < 3; i++) {
    np.osc[i].wave = d.w[i];   np.osc[i].level = d.lvl[i];
    np.osc[i].octave = d.oct[i]; np.osc[i].semis = d.semi[i];
    np.osc[i].fine = d.fine[i]; np.osc[i].duty = d.duty;
  }
  np.filterType = d.ftype; np.cutoff = d.cut; np.resonance = d.res;
  np.envAmount = d.envamt;
  np.ampEnv  = { d.aa, d.ad, d.as, d.ar };
  np.filtEnv = { d.fa, d.fd, d.fs, d.fr };
  np.lfoWave = d.lfoW; np.lfoRate = d.lfoR;
  np.lfoToFilter = d.lfoF; np.lfoToPitch = d.lfoP;
  np.lfoToAmp = d.lfoA; np.lfoToPwm = d.lfoPwm;
  np.glideMs = d.glide; np.volume = d.vol;
  np.reverb = d.rev; np.chorus = d.cho;
  s.p = np;
  s.defineSynth();
  s.sendEffects();
}

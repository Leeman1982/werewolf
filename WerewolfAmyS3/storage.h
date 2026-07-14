// WerewolfAmyS3 — persistence (ESP32 NVS via Preferences)
//
//   wolf user presets : 8 slots of WolfParams
//   wolf seq patterns : all 8 patterns in one blob
//   gm seq patterns   : all 8 patterns + chain in one blob
//   settings          : router matrix, clock, touch calibration, BPM
#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "config.h"
#include "wolf_synth.h"
#include "wolf_seq.h"
#include "gm_seq.h"
#include "midi_router.h"
#include "clock.h"
#include "ui_common.h"

#define NUM_USER_PRESETS 8

class Storage {
public:
  void begin() { /* namespaces opened per-op to limit handle lifetime */ }

  // ── Wolf user presets ─────────────────────────────────────────────────────
  bool saveUserPreset(int slot, const WolfParams& p) {
    if (slot < 0 || slot >= NUM_USER_PRESETS) return false;
    Preferences prefs;
    if (!prefs.begin("wolfpre", false)) return false;
    char key[8]; snprintf(key, sizeof(key), "u%d", slot);
    size_t n = prefs.putBytes(key, &p, sizeof(p));
    prefs.end();
    return n == sizeof(p);
  }
  bool loadUserPreset(int slot, WolfParams* out) {
    if (slot < 0 || slot >= NUM_USER_PRESETS) return false;
    Preferences prefs;
    if (!prefs.begin("wolfpre", true)) return false;
    char key[8]; snprintf(key, sizeof(key), "u%d", slot);
    bool ok = prefs.getBytes(key, out, sizeof(WolfParams)) == sizeof(WolfParams);
    prefs.end();
    return ok;
  }

  // ── Sequencer patterns ────────────────────────────────────────────────────
  bool saveWolfSeq(const WolfSeq& s) {
    Preferences prefs;
    if (!prefs.begin("wseq", false)) return false;
    bool ok = prefs.putBytes("pats", s.patterns, sizeof(s.patterns)) == sizeof(s.patterns);
    prefs.end();
    return ok;
  }
  bool loadWolfSeq(WolfSeq* s) {
    Preferences prefs;
    if (!prefs.begin("wseq", true)) return false;
    bool ok = prefs.getBytes("pats", s->patterns, sizeof(s->patterns)) == sizeof(s->patterns);
    prefs.end();
    return ok;
  }
  bool saveGMSeq(const GMSeq& s) {
    Preferences prefs;
    if (!prefs.begin("gseq", false)) return false;
    bool ok = prefs.putBytes("pats", s.patterns, sizeof(s.patterns)) == sizeof(s.patterns);
    ok &= prefs.putBytes("chain", s.chain, sizeof(s.chain)) > 0;
    prefs.putUChar("chlen", s.chainLen);
    prefs.end();
    return ok;
  }
  bool loadGMSeq(GMSeq* s) {
    Preferences prefs;
    if (!prefs.begin("gseq", true)) return false;
    bool ok = prefs.getBytes("pats", s->patterns, sizeof(s->patterns)) == sizeof(s->patterns);
    if (ok) {
      prefs.getBytes("chain", s->chain, sizeof(s->chain));
      s->chainLen = prefs.getUChar("chlen", 0);
    }
    prefs.end();
    return ok;
  }

  // ── Global settings ───────────────────────────────────────────────────────
  void saveSettings() {
    Preferences prefs;
    if (!prefs.begin("wwset", false)) return;
    prefs.putBytes("routes", router.routes, sizeof(router.routes));
    prefs.putUChar("wolfch", router.wolfChannel);
    prefs.putUChar("localch", router.localChannel);
    prefs.putUChar("clksrc", (uint8_t)router.clockSource);
    prefs.putBool("clkdin", router.clockOutDin);
    prefs.putBool("clkusb", router.clockOutUsb);
    prefs.putUShort("bpm", masterClock.bpm);
    prefs.putBytes("tcal", &touchCal, sizeof(touchCal));
    prefs.end();
  }
  void loadSettings() {
    Preferences prefs;
    if (!prefs.begin("wwset", true)) return;
    if (prefs.getBytes("routes", router.routes, sizeof(router.routes)) != sizeof(router.routes))
      router.defaults();
    router.wolfChannel  = prefs.getUChar("wolfch", 0);
    router.localChannel = prefs.getUChar("localch", 1);
    router.setClockSource((ClockSource)prefs.getUChar("clksrc", CLK_INTERNAL));
    router.clockOutDin = prefs.getBool("clkdin", false);
    router.clockOutUsb = prefs.getBool("clkusb", false);
    masterClock.bpm = prefs.getUShort("bpm", BPM_DEFAULT);
    prefs.getBytes("tcal", &touchCal, sizeof(touchCal));
    prefs.end();
  }
};

extern Storage storage;

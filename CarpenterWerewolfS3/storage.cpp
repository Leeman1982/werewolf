#include "storage.h"
#include <FFat.h>
#include <stdio.h>
#include <string.h>

volatile bool g_diskMode = false;

// Mount only — never format here.  Formatting a ~10 MB FAT partition takes far
// longer than any sane boot-time watchdog, and doing it inside setup() means a
// fresh board reboots mid-format forever with a blank screen.  A missing
// filesystem is not fatal: the machine runs entirely from RAM, and SETTINGS ->
// FORMAT FS does the job later as a deliberate, on-screen action.
bool Storage::begin() {
    if (!FFat.begin(false)) { _mounted = false; return false; }
    _mounted = true;
    FFat.mkdir("/patterns");
    FFat.mkdir("/presets");
    FFat.mkdir("/songs");
    FFat.mkdir("/banks");    // DX7 .syx cartridges -- see voicebank.h
    return true;
}

void Storage::patternPath(uint8_t track, uint8_t idx, char* buf, size_t n) {
    snprintf(buf, n, "/patterns/t%up%u.bin", (unsigned)(track % NUM_TRACKS),
             (unsigned)(idx % NUM_PATTERNS));
}
// A preset is only meaningful for the engine it was saved under (see the
// comment on UserPreset in storage.h), so the engine id is part of the path,
// not just a field inside the file — a Minimoog patch and a CS-80 patch in
// the same numbered slot are two different files, never one overwriting
// the other across an engine switch.
void Storage::presetPath(uint8_t engineId, uint8_t slot, char* buf, size_t n) {
    snprintf(buf, n, "/presets/e%u_u%02u.bin", (unsigned)(engineId % NUM_ENGINES),
             (unsigned)(slot % USER_PRESET_SLOTS));
}
void Storage::songPath(uint8_t slot, char* buf, size_t n) {
    snprintf(buf, n, "/songs/s%03u.bin", (unsigned)(slot % SONG_SLOTS));
}

// ─── Patterns ───────────────────────────────────────────────────────────────
bool Storage::savePattern(uint8_t track, uint8_t idx, const Pattern& p) {
    if (!_mounted || g_diskMode) return false;
    char path[40]; patternPath(track, idx, path, sizeof(path));
    audioFlashBegin();
    File f = FFat.open(path, "w");
    bool ok = false;
    if (f) { ok = (f.write((const uint8_t*)&p, sizeof(Pattern)) == sizeof(Pattern)); f.close(); }
    audioFlashEnd();
    return ok;
}

bool Storage::loadPattern(uint8_t track, uint8_t idx, Pattern& p) {
    if (!_mounted || g_diskMode) return false;
    char path[40]; patternPath(track, idx, path, sizeof(path));
    File f = FFat.open(path, "r");
    if (!f) return false;
    bool ok = (f.read((uint8_t*)&p, sizeof(Pattern)) == (int)sizeof(Pattern));
    f.close();
    // A pattern with a zero or over-long length would hang the step advance.
    if (ok && (p.length == 0 || p.length > NUM_STEPS)) p.length = DEFAULT_STEPS;
    return ok;
}

bool Storage::patternExists(uint8_t track, uint8_t idx) {
    if (!_mounted || g_diskMode) return false;
    char path[40]; patternPath(track, idx, path, sizeof(path));
    return FFat.exists(path);
}

// ─── Settings ───────────────────────────────────────────────────────────────
bool Storage::saveSettings(const GlobalSettings& s) {
    if (!_mounted || g_diskMode) return false;
    audioFlashBegin();
    File f = FFat.open("/settings.bin", "w");
    bool ok = false;
    if (f) { ok = (f.write((const uint8_t*)&s, sizeof(s)) == sizeof(s)); f.close(); }
    audioFlashEnd();
    return ok;
}

bool Storage::loadSettings(GlobalSettings& s) {
    if (!_mounted || g_diskMode) return false;
    File f = FFat.open("/settings.bin", "r");
    if (!f) return false;
    GlobalSettings tmp;
    bool ok = (f.read((uint8_t*)&tmp, sizeof(tmp)) == (int)sizeof(tmp));
    f.close();
    // Reject anything written by a different build rather than restoring a
    // struct whose fields have since moved.
    if (!ok || tmp.magic != STORAGE_MAGIC || tmp.version != STORAGE_VERSION) return false;
    s = tmp;
    if (s.bpm < BPM_MIN || s.bpm > BPM_MAX) s.bpm = BPM_DEFAULT;
    return true;
}

// ─── User presets ───────────────────────────────────────────────────────────
bool Storage::saveUserPreset(uint8_t engineId, uint8_t slot, const UserPreset& p) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; presetPath(engineId, slot, path, sizeof(path));
    audioFlashBegin();
    File f = FFat.open(path, "w");
    bool ok = false;
    if (f) { ok = (f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p)); f.close(); }
    audioFlashEnd();
    return ok;
}

bool Storage::loadUserPreset(uint8_t engineId, uint8_t slot, UserPreset& p) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; presetPath(engineId, slot, path, sizeof(path));
    File f = FFat.open(path, "r");
    if (!f) return false;
    bool ok = (f.read((uint8_t*)&p, sizeof(p)) == (int)sizeof(p));
    f.close();
    if (ok) p.name[sizeof(p.name) - 1] = 0;
    return ok;
}

bool Storage::userPresetExists(uint8_t engineId, uint8_t slot) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; presetPath(engineId, slot, path, sizeof(path));
    return FFat.exists(path);
}

// ─── Songs ──────────────────────────────────────────────────────────────────
bool Storage::saveSong(uint8_t slot, const Song& s) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; songPath(slot, path, sizeof(path));
    audioFlashBegin();
    File f = FFat.open(path, "w");
    bool ok = false;
    if (f) { ok = (f.write((const uint8_t*)&s, sizeof(s)) == sizeof(s)); f.close(); }
    audioFlashEnd();
    return ok;
}

bool Storage::loadSong(uint8_t slot, Song& s) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; songPath(slot, path, sizeof(path));
    File f = FFat.open(path, "r");
    if (!f) return false;
    Song tmp;
    bool ok = (f.read((uint8_t*)&tmp, sizeof(tmp)) == (int)sizeof(tmp));
    f.close();
    // Same rule as settings: reject anything a different build's struct
    // layout wrote rather than misinterpret it.
    if (!ok || tmp.magic != STORAGE_MAGIC || tmp.version != STORAGE_VERSION) return false;
    tmp.name[sizeof(tmp.name) - 1] = 0;
    s = tmp;
    return true;
}

bool Storage::songExists(uint8_t slot) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; songPath(slot, path, sizeof(path));
    return FFat.exists(path);
}

bool Storage::deleteSong(uint8_t slot) {
    if (!_mounted || g_diskMode) return false;
    char path[32]; songPath(slot, path, sizeof(path));
    return FFat.remove(path);
}

// Deliberately does NOT require an existing mount: this is the recovery path
// for a board whose FS partition has never been formatted, which is precisely
// the case where _mounted is false.  Takes as long as it takes — there is no
// watchdog to outrun.
void Storage::formatAll() {
    if (g_diskMode) return;
    audioFlashBegin();
    if (_mounted) FFat.end();
    _mounted = false;
    bool ok = FFat.format() && FFat.begin(false);
    audioFlashEnd();
    _mounted = ok;
    if (_mounted) {
        FFat.mkdir("/patterns");
        FFat.mkdir("/presets");
        FFat.mkdir("/songs");
        FFat.mkdir("/banks");
    }
}

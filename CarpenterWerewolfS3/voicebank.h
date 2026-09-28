#pragma once
#include <Arduino.h>
#include "config.h"
#include "dx7_rom.h"   // DX7_PACKED_VOICE, DX7_VOICES_PER_BANK

// ============================================================================
//  voicebank.h — DX7 .syx cartridges on the filesystem: extra banks on top of
//  the 8 built into flash (dx7_rom.h).
//
//  Dropped into /banks by a computer over the exported USB drive (SETTINGS ->
//  USB DRIVE) and picked back up on eject. This module knows nothing about
//  the ROM banks — dx7.cpp already owns those directly (dx7_rom.h/.cpp) — so
//  numbering here is 0-based over just the filesystem banks found; dx7.cpp
//  adds the ROM count back on before showing a bank number on the panel (see
//  dxExtBankCount/dxExtBankName/dxExtGetVoice, wired up once at boot in
//  CarpenterSynth.ino's setup(), after storage mounts).
//
//  That wiring is a function-pointer indirection, not a direct call, and
//  deliberately so: dx7.cpp's ROM-only voice path is exercised by
//  tests/run.sh's host build, which links neither this file (it needs the
//  real FatFS) nor the sketch that would set the pointers. Left null, those
//  three pointers make "no filesystem-extended banks available" the exact
//  same safe behaviour this engine already had before this module existed —
//  dxInstallVoice() in dx7.cpp falls back to "keep the current sound" — not a
//  build break.
//
//  Accepted files in /banks:
//    *.syx   4104 bytes — standard DX7 32-voice cartridge dump (6-byte SysEx
//                         header, 4096 bytes of voice data, checksum, 0xF7)
//    *.syx   4096 bytes — raw headerless voice data
// ============================================================================

// Scan /banks. Safe to call repeatedly (e.g. after the USB drive is
// ejected) — does nothing if the filesystem is unmounted or the host
// currently owns it (g_diskMode, storage.h).
void vbRescan();

// How many banks vbRescan() found last time (0 if none, or no filesystem).
uint8_t vbBankCount();

// Display name for filesystem bank `bank` (0-based, < vbBankCount()): the
// file's own name, minus its .syx extension. Left untouched (not
// blanked) if `bank` is out of range, so a stale caller shows stale text
// instead of garbage.
void vbBankName(uint8_t bank, char* out, size_t outSize);

// Fetch one packed voice from filesystem bank `bank`, program `prog`
// (0..DX7_VOICES_PER_BANK-1). Returns false — leaving `out` untouched — if
// the bank/program is out of range, the file vanished, or the filesystem is
// currently unavailable; callers keep whatever voice was already loaded
// rather than install garbage.
bool vbGetVoice(uint8_t bank, uint8_t prog, uint8_t out[DX7_PACKED_VOICE]);

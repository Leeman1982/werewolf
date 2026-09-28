#include "voicebank.h"
#include <FatFS.h>
#include <string.h>

// The USB drive and the Pico cannot both own the filesystem; the sketch sets
// this (see CarpenterSynth.ino) while the PC has the drive mounted. Every
// function here refuses to touch FatFS while it's set, matching every
// function in storage.cpp.
extern volatile bool g_diskMode;

#define BANK_DIR "/banks"

struct FsBank {
    char     file[28];   // full path, e.g. "/banks/mypatches.syx"
    char     name[14];   // display name (basename, no extension)
    uint16_t offset;      // where voice data starts (6 for SysEx, 0 for raw)
};

static FsBank  _fs[MAX_FS_BANKS];
static uint8_t _fsCount = 0;

// ─── Scanning ────────────────────────────────────────────────────────────────

static bool endsWithSyx(const char* n) {
    size_t l = strlen(n);
    if (l < 4) return false;
    const char* e = n + l - 4;
    return (e[0] == '.') &&
           (e[1] == 's' || e[1] == 'S') &&
           (e[2] == 'y' || e[2] == 'Y') &&
           (e[3] == 'x' || e[3] == 'X');
}

void vbRescan() {
    _fsCount = 0;
    if (g_diskMode) return;              // PC owns the filesystem right now

    Dir dir = FatFS.openDir(BANK_DIR);
    while (_fsCount < MAX_FS_BANKS && dir.next()) {
        if (dir.isDirectory()) continue;
        String fn = dir.fileName();
        if (!endsWithSyx(fn.c_str())) continue;

        size_t sz = dir.fileSize();
        uint16_t off;
        if      (sz >= (size_t)DX7_PACKED_VOICE * DX7_VOICES_PER_BANK + 8) off = 6;  // SysEx-wrapped
        else if (sz >= (size_t)DX7_PACKED_VOICE * DX7_VOICES_PER_BANK)     off = 0;  // raw voice data
        else continue;                                                     // too small to be a bank

        FsBank& b = _fs[_fsCount];
        snprintf(b.file, sizeof(b.file), "%s/%s", BANK_DIR, fn.c_str());
        b.offset = off;

        // Display name: filename minus the .syx extension.
        size_t n = fn.length() >= 4 ? fn.length() - 4 : fn.length();
        if (n >= sizeof(b.name)) n = sizeof(b.name) - 1;
        memcpy(b.name, fn.c_str(), n);
        b.name[n] = '\0';

        _fsCount++;
    }
}

// ─── Queries ─────────────────────────────────────────────────────────────────

uint8_t vbBankCount() { return _fsCount; }

void vbBankName(uint8_t bank, char* out, size_t outSize) {
    if (!out || !outSize || bank >= _fsCount) return;
    snprintf(out, outSize, "%s", _fs[bank].name);
}

bool vbGetVoice(uint8_t bank, uint8_t prog, uint8_t out[DX7_PACKED_VOICE]) {
    if (!out || bank >= _fsCount || prog >= DX7_VOICES_PER_BANK) return false;
    if (g_diskMode) return false;        // filesystem belongs to the PC

    File f = FatFS.open(_fs[bank].file, "r");
    if (!f) return false;
    bool ok = f.seek(_fs[bank].offset + (uint32_t)prog * DX7_PACKED_VOICE, SeekSet) &&
              f.read(out, DX7_PACKED_VOICE) == DX7_PACKED_VOICE;
    f.close();
    return ok;
}

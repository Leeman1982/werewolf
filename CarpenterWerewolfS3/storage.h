#pragma once
#include <Arduino.h>
#include "config.h"
#include "sequencer.h"
#include "engine.h"

// ============================================================================
//  storage.h — FatFS-backed persistence on the 16 MB part.
//
//  FatFS rather than LittleFS so the same partition can be exported to a host
//  over USB mass storage, which is how patterns and user patches get off the
//  box.  Pick a Tools -> Flash Size layout that reserves an FS partition
//  (e.g. 8 MB sketch / 8 MB FS).
// ============================================================================

// Implemented in the sketch.  On RP2350 a flash erase stalls the XIP cache, so
// the audio core must not be fetching instructions from flash while core0
// writes.  These park core1 in a RAM-resident spin at a clean block boundary
// instead of letting it be frozen mid-render.
void audioFlashBegin();
void audioFlashEnd();

struct GlobalSettings {
    uint16_t magic         = STORAGE_MAGIC;
    uint16_t version       = STORAGE_VERSION;
    uint16_t bpm           = BPM_DEFAULT;
    uint8_t  lastTrack     = 0;
    uint8_t  lastPattern[NUM_TRACKS] {};
    uint8_t  trackMute[NUM_TRACKS] {};
    uint8_t  midiClockOut  = 0;
    uint8_t  extSync       = 0;
    uint8_t  midiInChannel = 0;      // 0 = omni
    uint8_t  midiOutEnable = 1;
    uint8_t  midiThru      = 0;
    // Whether an external MIDI keyboard on MIDI IN plays the internal synth
    // engine live (see onMidiNoteOn() in the sketch) -- "Local Control" in
    // conventional synth terms. Defaults on: this is exactly what MIDI IN
    // already did unconditionally before this setting existed, so a stock
    // build's behaviour doesn't change until someone turns it off. Note-off
    // is never gated by this -- only whether a NEW note-on sounds the synth,
    // so toggling it mid-note can never strand a voice on.
    uint8_t  midiKbdPlay   = 1;
    uint8_t  currentPreset = 0;
    uint8_t  contrast      = 200;
    uint8_t  metronome     = 0;
    uint8_t  engineId      = ENGINE_CS80;        // which synth engine was live
    uint8_t  params[MAX_ENGINE_PARAMS] {};       // the live patch, restored at boot
    uint8_t  chainLen[NUM_TRACKS] {};
    ChainEntry chain[NUM_TRACKS][CHAIN_LEN] {};
    uint8_t  defaultTimeSigIdx = 0;   // TIME_SIGS index new/cleared patterns start on
};

// One preset is only meaningful for the engine it was saved under — hence
// engineId here, and why presetPath() (storage.cpp) namespaces the file by
// engine as well as slot.
struct UserPreset {
    char    name[14];
    uint8_t engineId;
    uint8_t params[MAX_ENGINE_PARAMS];
};

// A song is deliberately lightweight: each track's chain plus which pattern
// it's sitting on when the song loads.  It references pattern *slots*, not
// copies of their contents — same trade-off the chain feature already makes
// day to day, and it's what keeps 200 of these cheap.  Editing a pattern
// changes it for every song that points at that slot; that's a feature for a
// live set (fix a pattern once, every song using it is fixed), not a bug, but
// it does mean a song is not a self-contained archive of "the way it sounded
// when you saved it" if you keep editing patterns afterward.
struct SongTrack {
    uint8_t    activePattern = 0;
    uint8_t    chainLen      = 0;
    ChainEntry chain[CHAIN_LEN] {};
};
struct Song {
    uint16_t  magic   = STORAGE_MAGIC;
    uint16_t  version = STORAGE_VERSION;
    char      name[14] = {0};
    uint16_t  bpm = BPM_DEFAULT;
    SongTrack tracks[NUM_TRACKS];
};

class Storage {
public:
    bool begin();
    bool mounted() const { return _mounted; }

    bool savePattern(uint8_t track, uint8_t idx, const Pattern& p);
    bool loadPattern(uint8_t track, uint8_t idx, Pattern& p);
    bool patternExists(uint8_t track, uint8_t idx);

    bool saveSettings(const GlobalSettings& s);
    bool loadSettings(GlobalSettings& s);

    bool saveUserPreset(uint8_t engineId, uint8_t slot, const UserPreset& p);
    bool loadUserPreset(uint8_t engineId, uint8_t slot, UserPreset& p);
    bool userPresetExists(uint8_t engineId, uint8_t slot);

    bool saveSong(uint8_t slot, const Song& s);
    bool loadSong(uint8_t slot, Song& s);
    bool songExists(uint8_t slot);
    bool deleteSong(uint8_t slot);

    void formatAll();

private:
    bool _mounted = false;
    static void patternPath(uint8_t track, uint8_t idx, char* buf, size_t n);
    static void presetPath(uint8_t engineId, uint8_t slot, char* buf, size_t n);
    static void songPath(uint8_t slot, char* buf, size_t n);
};

// Set while the host owns the USB drive: every FatFS access is refused, because
// FAT allows exactly one writer and the host's own housekeeping would corrupt
// anything we wrote underneath it.
extern volatile bool g_diskMode;

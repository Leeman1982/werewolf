// WerewolfAmyS3 — General MIDI engine
//
// TinySoundFont (patched: TSF_INT16_SAMPLES) playing a full GM SoundFont that
// lives in its own flash partition ("sf2") and is memory-mapped, so the ~7.5MB
// of sample data costs zero RAM. 16 MIDI channels, channel 10 = drums.
//
// Thread model: note/CC events can arrive from the UI, the sequencer task and
// the MIDI router; tsf itself is not thread-safe against its render call, so
// everything is pushed through a lock-free ring buffer and drained inside
// render(), which runs in the audio task.
#pragma once
#include <Arduino.h>
#include "config.h"

extern "C" {
#include "esp_partition.h"
#include "amy.h"   // AMY_BLOCK_SIZE / AMY_SAMPLE_RATE / AMY_NCHANS
}

#define TSF_IMPLEMENTATION
#define TSF_INT16_SAMPLES
#define TSF_NO_STDIO
#include "tsf.h"

class GMEngine {
public:
  // Find, map and load the SoundFont. Returns false (engine disabled, rest of
  // the firmware still works) if the partition is missing or not an SF2.
  bool begin() {
    const esp_partition_t* part = esp_partition_find_first(
        (esp_partition_type_t)0x40, (esp_partition_subtype_t)0x00, SF2_PARTITION_LABEL);
    if (!part) {
      Serial.println("GM: no 'sf2' partition found");
      return false;
    }
    const void* base = nullptr;
    if (esp_partition_mmap(part, 0, part->size, ESP_PARTITION_MMAP_DATA,
                           &base, &_mmapHandle) != ESP_OK) {
      Serial.println("GM: mmap failed");
      return false;
    }
    const uint8_t* b = (const uint8_t*)base;
    if (memcmp(b, "RIFF", 4) != 0) {
      Serial.println("GM: partition does not contain a SoundFont (flash one with scripts/flash_soundfont.sh)");
      return false;
    }
    uint32_t riffSize; memcpy(&riffSize, b + 4, 4);
    uint32_t sf2Size = riffSize + 8;
    if (sf2Size > part->size) sf2Size = part->size;

    _font = tsf_load_memory_external(base, sf2Size);
    if (!_font) {
      Serial.println("GM: SoundFont parse failed");
      return false;
    }
    tsf_set_output(_font, TSF_STEREO_INTERLEAVED, AMY_SAMPLE_RATE, -3.0f);
    tsf_set_max_voices(_font, GM_MAX_VOICES);
    for (int ch = 0; ch < GM_CHANNELS; ch++)
      tsf_channel_set_presetnumber(_font, ch, 0, ch == GM_DRUM_CHANNEL);
    Serial.printf("GM: SoundFont loaded, %d presets, %lu bytes mapped\n",
                  tsf_get_presetcount(_font), (unsigned long)sf2Size);
    return true;
  }

  bool ok() const { return _font != nullptr; }
  int presetCount() { return _font ? tsf_get_presetcount(_font) : 0; }
  const char* presetName(int idx) {
    return _font ? tsf_get_presetname(_font, idx) : "";
  }
  // Name for a GM program number on a melodic channel (bank 0), from the font
  // itself, falling back to the standard GM table.
  const char* programName(int prog);   // defined after GM_PROGRAM_NAMES below
  int activeVoices() { return _font ? tsf_active_voice_count(_font) : 0; }

  // ── Event input (any task) ────────────────────────────────────────────────
  void noteOn(uint8_t ch, uint8_t note, uint8_t vel) { push(0x90 | ch, note, vel); }
  void noteOff(uint8_t ch, uint8_t note)             { push(0x80 | ch, note, 0);   }
  void programChange(uint8_t ch, uint8_t prog)       { push(0xC0 | ch, prog, 0);   }
  void controlChange(uint8_t ch, uint8_t cc, uint8_t v) { push(0xB0 | ch, cc, v);  }
  void pitchBend(uint8_t ch, uint8_t lsb, uint8_t msb)  { push(0xE0 | ch, lsb, msb); }
  void allNotesOff(uint8_t ch)  { push(0xB0 | ch, 123, 0); }
  void allSoundOff() { for (int ch = 0; ch < GM_CHANNELS; ch++) push(0xB0 | ch, 120, 0); }

  // Raw MIDI from the router
  void handleMidi(uint8_t status, uint8_t d1, uint8_t d2) { push(status, d1, d2); }

  // Per-channel level for the mixer page (0..1), goes out as CC7
  void setChannelLevel(uint8_t ch, float v) { controlChange(ch, 7, (uint8_t)(v * 127.0f)); }
  float channelLevel(uint8_t ch) { return _font ? tsf_channel_get_volume(_font, ch) : 0; }
  int channelProgram(uint8_t ch) { return _font ? tsf_channel_get_preset_number(_font, ch) : 0; }

  // ── Audio (audio task only) ───────────────────────────────────────────────
  // Render one AMY block and saturating-mix into `inout`.
  void render(int16_t* inout) {
    if (!_font) return;
    drainEvents();
    static int16_t scratch[AMY_BLOCK_SIZE * AMY_NCHANS];
    tsf_render_short(_font, scratch, AMY_BLOCK_SIZE, 0);
    for (int i = 0; i < AMY_BLOCK_SIZE * AMY_NCHANS; i++) {
      int32_t s = (int32_t)inout[i] + (int32_t)scratch[i];
      if (s > 32767) s = 32767; else if (s < -32768) s = -32768;
      inout[i] = (int16_t)s;
    }
  }

private:
  struct Ev { uint8_t status, d1, d2; };
  static const int QLEN = 256;             // power of two
  volatile uint16_t _qHead = 0, _qTail = 0;
  Ev _queue[QLEN];
  tsf* _font = nullptr;
  esp_partition_mmap_handle_t _mmapHandle = 0;

  void push(uint8_t status, uint8_t d1, uint8_t d2) {
    uint16_t h = _qHead;
    uint16_t next = (h + 1) & (QLEN - 1);
    if (next == _qTail) return;            // full: drop rather than block audio
    _queue[h] = { status, d1, d2 };
    _qHead = next;
  }

  void drainEvents() {
    while (_qTail != _qHead) {
      Ev e = _queue[_qTail];
      _qTail = (_qTail + 1) & (QLEN - 1);
      uint8_t ch = e.status & 0x0F;
      switch (e.status & 0xF0) {
        case 0x90:
          if (e.d2) tsf_channel_note_on(_font, ch, e.d1, e.d2 / 127.0f);
          else      tsf_channel_note_off(_font, ch, e.d1);
          break;
        case 0x80: tsf_channel_note_off(_font, ch, e.d1); break;
        case 0xC0: tsf_channel_set_presetnumber(_font, ch, e.d1, ch == GM_DRUM_CHANNEL); break;
        case 0xB0: tsf_channel_midi_control(_font, ch, e.d1, e.d2); break;
        case 0xE0: tsf_channel_set_pitchwheel(_font, ch, ((int)e.d2 << 7) | e.d1); break;
        default: break;
      }
    }
  }
};

extern GMEngine gm;

// Standard GM program names (bank 0) for UI display
static const char* const GM_PROGRAM_NAMES[128] = {
  "Grand Piano", "Bright Piano", "El. Grand", "Honky-Tonk", "E.Piano 1", "E.Piano 2",
  "Harpsichord", "Clavinet", "Celesta", "Glockenspiel", "Music Box", "Vibraphone",
  "Marimba", "Xylophone", "Tubular Bell", "Dulcimer", "Drawbar Org", "Perc. Organ",
  "Rock Organ", "Church Org", "Reed Organ", "Accordion", "Harmonica", "Tango Acc.",
  "Nylon Gtr", "Steel Gtr", "Jazz Gtr", "Clean Gtr", "Muted Gtr", "Overdrive Gtr",
  "Dist. Gtr", "Gtr Harmonics", "Acoustic Bs", "Finger Bass", "Pick Bass", "Fretless Bs",
  "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2", "Violin", "Viola",
  "Cello", "Contrabass", "Trem. Strings", "Pizzicato", "Harp", "Timpani",
  "Strings 1", "Strings 2", "Syn Strings 1", "Syn Strings 2", "Choir Aahs", "Voice Oohs",
  "Synth Voice", "Orchestra Hit", "Trumpet", "Trombone", "Tuba", "Muted Trumpet",
  "French Horn", "Brass Sect.", "Syn Brass 1", "Syn Brass 2", "Soprano Sax", "Alto Sax",
  "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
  "Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi",
  "Whistle", "Ocarina", "Square Lead", "Saw Lead", "Calliope", "Chiff Lead",
  "Charang", "Voice Lead", "Fifths Lead", "Bass+Lead", "New Age Pad", "Warm Pad",
  "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
  "Rain FX", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins",
  "Echoes", "Sci-Fi", "Sitar", "Banjo", "Shamisen", "Koto",
  "Kalimba", "Bagpipe", "Fiddle", "Shanai", "Tinkle Bell", "Agogo",
  "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Rev. Cymbal",
  "Gtr Fret Nse", "Breath Noise", "Seashore", "Bird Tweet", "Telephone", "Helicopter",
  "Applause", "Gunshot"
};

inline const char* GMEngine::programName(int prog) {
  prog &= 127;
  if (_font) {
    const char* n = tsf_bank_get_presetname(_font, 0, prog);
    if (n && n[0]) return n;
  }
  return GM_PROGRAM_NAMES[prog];
}

// GM drum note names (channel 10), notes 35-81
static const char* gmDrumName(int note) {
  static const char* const D[] = {
    "Kick 2", "Kick 1", "Sidestick", "Snare 1", "Clap", "Snare 2", "Lo Tom 2", "Cl HiHat",
    "Lo Tom 1", "Pedal HH", "Mid Tom 2", "Open HH", "Mid Tom 1", "Hi Tom 2", "Crash 1",
    "Hi Tom 1", "Ride 1", "China", "Ride Bell", "Tambourine", "Splash", "Cowbell",
    "Crash 2", "Vibraslap", "Ride 2", "Hi Bongo", "Lo Bongo", "Mute Conga", "Open Conga",
    "Lo Conga", "Hi Timbale", "Lo Timbale", "Hi Agogo", "Lo Agogo", "Cabasa", "Maracas",
    "Whistle S", "Whistle L", "Guiro S", "Guiro L", "Claves", "Hi Woodblk", "Lo Woodblk",
    "Mute Cuica", "Open Cuica", "Mute Tri", "Open Tri"
  };
  if (note < 35 || note > 81) return "---";
  return D[note - 35];
}

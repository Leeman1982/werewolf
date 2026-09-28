#pragma once
#include <Arduino.h>

// ============================================================================
//  dx7_rom.h -- the Yamaha DX7 factory ROM cartridges, 8 banks x 32 voices.
//
//  The original 1983 factory sound library: ROM1A/1B ("Master") and ROM2A/2B
//  through ROM4A/4B, exactly the sysex the machines shipped with, in DX7
//  PACKED voice format (128 bytes per voice -- the cartridge format, not the
//  155-byte unpacked one; Dexed::decodeVoice() unpacks it).  Bytes 118..127
//  of each voice are its 10-character name.
//
//  Sourced from the MedusaDexed project (this author's own Medusa sequencer,
//  branch claude/medusa-pico2dexed-engine-21fjk7), which generated it from
//  the .syx files with its own sysex2c.py.  Carried over byte-for-byte; the
//  only change is `const`.  That matters: PROGMEM is a no-op on RP2350 (one
//  flat address space), so the non-const array it arrived as would have been
//  32 KB of the chip's SRAM.  const puts it back in flash where it belongs.
// ============================================================================

#define DX7_ROM_BANKS        8
#define DX7_VOICES_PER_BANK  32
#define DX7_PACKED_VOICE     128    // bytes per packed voice
#define DX7_VOICE_NAME_OFF   118    // 10 chars of name, at this offset

extern const char* const DX7_ROM_BANK_NAMES[DX7_ROM_BANKS];

// One definition, in dx7_rom.cpp -- a `const` array defined in a header has
// internal linkage in C++, so every translation unit that included it would
// carry its own 32 KB copy.
extern const uint8_t DX7_ROM[DX7_ROM_BANKS][DX7_VOICES_PER_BANK][DX7_PACKED_VOICE];

// WerewolfAmyS3 — MIDI router
//
// Every MIDI-ish event in the box flows through one place, so any source can
// reach any destination:
//
//   sources:  DIN IN (UART/optocoupler)   destinations:  WOLF synth (AMY)
//             USB IN (native USB device)                 GM synth (SoundFont)
//             WOLF SEQ                                   DIN OUT
//             GM SEQ                                     USB OUT
//             ARP
//             LOCAL (touch keyboard / chord pad)
//
// The 6x4 routing matrix is fully editable from the ROUTE page and persisted.
// Also handles clock: internal (AMY sequencer) or external (DIN/USB MIDI
// clock), with clock/transport transmit toggles per output.
#pragma once
#include <Arduino.h>
#include "config.h"
#include "wolf_synth.h"
#include "gm_engine.h"

extern "C" {
#include "amy.h"
#include "amy_midi.h"     // midi_out() → DIN UART
#include "sequencer.h"    // sequencer_midi_* for external USB clock
}

#ifndef WW_ENABLE_USB_MIDI
#define WW_ENABLE_USB_MIDI 1
#endif

#if WW_ENABLE_USB_MIDI && (defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0)
#include "USB.h"
#include "USBMIDI.h"
#define WW_USB_MIDI_AVAILABLE 1
extern USBMIDI usbMIDI;
#else
#define WW_USB_MIDI_AVAILABLE 0
#endif

enum MidiSrc : uint8_t { SRC_DIN, SRC_USB, SRC_WOLFSEQ, SRC_GMSEQ, SRC_ARP, SRC_LOCAL, NUM_SRC };
enum MidiDst : uint8_t { DST_WOLF, DST_GM, DST_DIN, DST_USB, NUM_DST };

static const char* const MIDI_SRC_NAMES[NUM_SRC] =
  { "DIN IN", "USB IN", "WOLF SEQ", "GM SEQ", "ARP", "LOCAL" };
static const char* const MIDI_DST_NAMES[NUM_DST] =
  { "WOLF", "GM", "DIN OUT", "USB OUT" };

enum ClockSource : uint8_t { CLK_INTERNAL, CLK_EXT_DIN, CLK_EXT_USB };

class MidiRouter {
public:
  // routes[src] is a bitmask of (1 << dst)
  uint8_t routes[NUM_SRC];
  uint8_t wolfChannel   = 0;      // 0 = omni, 1-16 = only that channel
  uint8_t localChannel  = 1;      // channel stamped on LOCAL/ARP/WOLFSEQ events
  ClockSource clockSource = CLK_INTERNAL;
  bool clockOutDin = false, clockOutUsb = false;
  bool transportOut = true;       // send 0xFA/0xFC with clock out

  void defaults() {
    routes[SRC_DIN]     = bit(DST_WOLF) | bit(DST_GM);
    routes[SRC_USB]     = bit(DST_WOLF) | bit(DST_GM);
    routes[SRC_WOLFSEQ] = bit(DST_WOLF);
    routes[SRC_GMSEQ]   = bit(DST_GM);
    routes[SRC_ARP]     = bit(DST_WOLF);
    routes[SRC_LOCAL]   = bit(DST_WOLF);
  }

  void begin() {
    defaults();
#if WW_USB_MIDI_AVAILABLE
    usbMIDI.begin();
    USB.begin();
#endif
  }

  bool routed(MidiSrc s, MidiDst d) const { return routes[s] & bit(d); }
  void toggle(MidiSrc s, MidiDst d) { routes[s] ^= bit(d); }

  // ── Event entry point ──────────────────────────────────────────────────────
  // status includes channel nibble for channel messages.
  void send(MidiSrc src, uint8_t status, uint8_t d1 = 0, uint8_t d2 = 0) {
    uint8_t mask = routes[src];
    if (mask & bit(DST_WOLF)) toWolf(status, d1, d2);
    if (mask & bit(DST_GM))   gm.handleMidi(status, d1, d2);
    if (mask & bit(DST_DIN))  dinWrite(status, d1, d2);
    if (mask & bit(DST_USB))  usbWrite(status, d1, d2);
  }

  // Convenience wrappers used by sequencers/arp (channel from settings/track)
  void noteOn(MidiSrc src, uint8_t ch, uint8_t note, uint8_t vel) {
    send(src, 0x90 | (ch & 15), note, vel);
  }
  void noteOff(MidiSrc src, uint8_t ch, uint8_t note) {
    send(src, 0x80 | (ch & 15), note, 0);
  }

  // ── Incoming: DIN (called from AMY's midi input hook) ─────────────────────
  void receiveDin(uint8_t* data, uint16_t len, uint8_t is_sysex) {
    if (is_sysex || len == 0) return;
    uint8_t st = data[0];
    if (st >= 0xF8) return;  // realtime handled by AMY's clock plumbing
    send(SRC_DIN, st, len > 1 ? data[1] : 0, len > 2 ? data[2] : 0);
  }

  // ── Incoming: USB (poll from the main loop) ────────────────────────────────
  void pollUsb() {
#if WW_USB_MIDI_AVAILABLE
    midiEventPacket_t p;
    while (usbMIDI.readPacket(&p)) {
      uint8_t cin = p.header & 0x0F;
      switch (cin) {
        case 0x08: case 0x09: case 0x0A: case 0x0B: case 0x0E:  // 3-byte ch msgs
          send(SRC_USB, p.byte1, p.byte2, p.byte3);
          break;
        case 0x0C: case 0x0D:                                    // 2-byte ch msgs
          send(SRC_USB, p.byte1, p.byte2, 0);
          break;
        case 0x0F:                                               // single byte (realtime)
          if (clockSource == CLK_EXT_USB) {
            if (p.byte1 == 0xF8) sequencer_midi_clock_tick();
            else if (p.byte1 == 0xFA) sequencer_midi_start();
            else if (p.byte1 == 0xFC) sequencer_midi_stop();
          }
          break;
        default: break;
      }
    }
#endif
  }

  // ── Clock / transport output (called from the clock task) ────────────────
  void sendClockTick() {
    if (clockOutDin) { uint8_t b = 0xF8; midi_out(&b, 1); }
    if (clockOutUsb) usbRealtime(0xF8);
  }
  void sendStart() {
    if (!transportOut) return;
    if (clockOutDin) { uint8_t b = 0xFA; midi_out(&b, 1); }
    if (clockOutUsb) usbRealtime(0xFA);
  }
  void sendStop() {
    if (!transportOut) return;
    if (clockOutDin) { uint8_t b = 0xFC; midi_out(&b, 1); }
    if (clockOutUsb) usbRealtime(0xFC);
  }

  void setClockSource(ClockSource cs) {
    clockSource = cs;
    // AMY follows DIN 0xF8 only when external sync is enabled:
    amy_external_midi_sync(cs == CLK_EXT_DIN ? 1 : 0);
  }

  void panic() {
    wolf.allNotesOff();
    gm.allSoundOff();
    for (uint8_t ch = 0; ch < 16; ch++) {
      dinWrite(0xB0 | ch, 123, 0);
      usbWrite(0xB0 | ch, 123, 0);
    }
  }

private:
  static uint8_t bit(uint8_t d) { return 1 << d; }

  void toWolf(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t type = status & 0xF0;
    uint8_t ch = (status & 0x0F) + 1;
    if (wolfChannel != 0 && ch != wolfChannel && type != 0xF0) return;
    switch (type) {
      case 0x90: if (d2) wolf.noteOn(d1, d2); else wolf.noteOff(d1); break;
      case 0x80: wolf.noteOff(d1); break;
      case 0xB0:
        if (d1 == 123 || d1 == 120) wolf.allNotesOff();
        else wolf.handleCC(d1, d2);
        break;
      case 0xE0: wolf.pitchBend((((int16_t)d2 << 7) | d1) - 8192); break;
      default: break;
    }
  }

  void dinWrite(uint8_t status, uint8_t d1, uint8_t d2) {
    uint8_t buf[3] = { status, d1, d2 };
    midi_out(buf, msgLen(status));
  }

  void usbWrite(uint8_t status, uint8_t d1, uint8_t d2) {
#if WW_USB_MIDI_AVAILABLE
    midiEventPacket_t p;
    uint8_t type = status & 0xF0;
    p.header = (type >> 4);           // CIN == status nibble for channel msgs
    p.byte1 = status; p.byte2 = d1; p.byte3 = d2;
    usbMIDI.writePacket(&p);
#else
    (void)status; (void)d1; (void)d2;
#endif
  }

  void usbRealtime(uint8_t b) {
#if WW_USB_MIDI_AVAILABLE
    midiEventPacket_t p;
    p.header = 0x0F; p.byte1 = b; p.byte2 = 0; p.byte3 = 0;
    usbMIDI.writePacket(&p);
#else
    (void)b;
#endif
  }

  static uint8_t msgLen(uint8_t status) {
    uint8_t t = status & 0xF0;
    if (t == 0xC0 || t == 0xD0) return 2;
    return 3;
  }
};

extern MidiRouter router;

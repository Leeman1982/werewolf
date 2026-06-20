#ifndef MIDI_INPUT_H
#define MIDI_INPUT_H

#include <Arduino.h>
#include <HardwareSerial.h>

// MIDI input handling for 5-pin DIN
// GPIO 35 is input-only — perfect for MIDI RX (no signal contention possible)

#define MIDI_SERIAL_RX 35  // 5-pin DIN MIDI input (input-only pin)
#define MIDI_BAUD_RATE 31250
// MIDI TX is opt-in. Define MIDI_SERIAL_TX in your build (or before including
// this header) to enable MIDI clock OUT, e.g. -DMIDI_SERIAL_TX=16.
// -1 = TX disabled (default, no clock output).
#ifndef MIDI_SERIAL_TX
#define MIDI_SERIAL_TX -1
#endif

class MIDIInput {
private:
  HardwareSerial* midiSerial;

  // MIDI parser state
  uint8_t midiStatus;
  uint8_t midiData1;
  uint8_t midiData2;
  uint8_t midiDataCount;
  bool runningStatus;

  // Callback function pointers
  void (*noteOnCallback)(uint8_t channel, uint8_t note, uint8_t velocity);
  void (*noteOffCallback)(uint8_t channel, uint8_t note, uint8_t velocity);
  void (*ccCallback)(uint8_t channel, uint8_t cc, uint8_t value);
  void (*pitchBendCallback)(uint8_t channel, int16_t bend);
  // Real-time / transport (24 PPQN clock, start, continue, stop)
  void (*clockTickCallback)();
  void (*clockStartCallback)();
  void (*clockContinueCallback)();
  void (*clockStopCallback)();

  void processMIDIByte(uint8_t byte) {
    if (byte >= 0xF8) {
      // System real-time messages — single-byte, can occur mid-message.
      switch (byte) {
        case 0xF8: if (clockTickCallback)     clockTickCallback();     break;
        case 0xFA: if (clockStartCallback)    clockStartCallback();    break;
        case 0xFB: if (clockContinueCallback) clockContinueCallback(); break;
        case 0xFC: if (clockStopCallback)     clockStopCallback();     break;
        default: break;  // active sense / reset — ignore
      }
      return;
    }

    if (byte >= 0x80) {
      // Status byte
      midiStatus = byte;
      midiDataCount = 0;
      runningStatus = true;

      // Determine expected data bytes
      uint8_t msgType = midiStatus & 0xF0;
      if (msgType == 0xC0 || msgType == 0xD0) {
        // Program change and channel pressure: 1 data byte
        midiDataCount = 0;
      }
    } else {
      // Data byte
      if (!runningStatus) return;

      if (midiDataCount == 0) {
        midiData1 = byte;
        midiDataCount = 1;

        uint8_t msgType = midiStatus & 0xF0;
        if (msgType == 0xC0 || msgType == 0xD0) {
          // Single data byte messages - process immediately
          midiDataCount = 0;
        }
      } else if (midiDataCount == 1) {
        midiData2 = byte;
        midiDataCount = 0;

        // Process complete message
        processCompleteMessage();
      }
    }
  }

  void processCompleteMessage() {
    uint8_t msgType = midiStatus & 0xF0;
    uint8_t channel = midiStatus & 0x0F;

    switch (msgType) {
      case 0x80: // Note Off
        if (noteOffCallback) {
          noteOffCallback(channel, midiData1, midiData2);
        }
        break;

      case 0x90: // Note On
        if (midiData2 == 0) {
          // Note on with velocity 0 is note off
          if (noteOffCallback) {
            noteOffCallback(channel, midiData1, midiData2);
          }
        } else {
          if (noteOnCallback) {
            noteOnCallback(channel, midiData1, midiData2);
          }
        }
        break;

      case 0xB0: // Control Change
        if (ccCallback) {
          ccCallback(channel, midiData1, midiData2);
        }
        break;

      case 0xE0: // Pitch Bend
        if (pitchBendCallback) {
          int16_t bend = ((int16_t)midiData2 << 7) | midiData1;
          bend -= 8192; // Center at 0
          pitchBendCallback(channel, bend);
        }
        break;
    }
  }

public:
  MIDIInput() {
    midiSerial = NULL;
    midiStatus = 0;
    midiData1 = 0;
    midiData2 = 0;
    midiDataCount = 0;
    runningStatus = false;

    noteOnCallback = NULL;
    noteOffCallback = NULL;
    ccCallback = NULL;
    pitchBendCallback = NULL;
    clockTickCallback = NULL;
    clockStartCallback = NULL;
    clockContinueCallback = NULL;
    clockStopCallback = NULL;
  }

  void init() {
    // 5-pin DIN MIDI on Serial2.  TX pin set from MIDI_SERIAL_TX (-1 disables).
    midiSerial = new HardwareSerial(2);
    midiSerial->begin(MIDI_BAUD_RATE, SERIAL_8N1, MIDI_SERIAL_RX, MIDI_SERIAL_TX);

    Serial.println("MIDI initialized (5-pin DIN RX=GPIO35)");
  }

  // ── MIDI output (for clock sync) ────────────────────────────────────────────
  // Called by the sequencer at 24 PPQN, plus start/stop transport events.
  void sendByte(uint8_t b) {
    if (midiSerial != NULL && MIDI_SERIAL_TX >= 0) midiSerial->write(b);
  }
  void sendClockTick()  { sendByte(0xF8); }
  void sendClockStart() { sendByte(0xFA); }
  void sendClockStop()  { sendByte(0xFC); }

  void update() {
    // Process 5-pin DIN MIDI
    if (midiSerial != NULL) {
      while (midiSerial->available()) {
        uint8_t byte = midiSerial->read();
        processMIDIByte(byte);
      }
    }
  }

  // Set callbacks
  void setNoteOnCallback(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    noteOnCallback = callback;
  }

  void setNoteOffCallback(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    noteOffCallback = callback;
  }

  void setCCCallback(void (*callback)(uint8_t, uint8_t, uint8_t)) {
    ccCallback = callback;
  }

  void setPitchBendCallback(void (*callback)(uint8_t, int16_t)) {
    pitchBendCallback = callback;
  }

  // Clock sync callbacks (24 PPQN tick, start, continue, stop)
  void setClockTickCallback(void (*cb)())     { clockTickCallback     = cb; }
  void setClockStartCallback(void (*cb)())    { clockStartCallback    = cb; }
  void setClockContinueCallback(void (*cb)()) { clockContinueCallback = cb; }
  void setClockStopCallback(void (*cb)())     { clockStopCallback     = cb; }
};

#endif

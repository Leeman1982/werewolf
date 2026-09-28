#include "midi.h"

#if MIDI_USE_PIO_UART
  #include <SerialPIO.h>
  // TX = GP1 (the MIDI OUT jack), RX = GP0.  Set PIN_MIDI_RX to -1 in config.h
  // to give GP0 back — the port then transmits only.
  #if PIN_MIDI_RX >= 0
    static SerialPIO midiPort(PIN_MIDI_TX, PIN_MIDI_RX, 64);
  #else
    static SerialPIO midiPort(PIN_MIDI_TX, SerialPIO::NOPIN, 64);
  #endif
  #define MIDI_PORT midiPort
#else
  #define MIDI_PORT Serial1
#endif

void MidiIO::begin() {
#if MIDI_USE_PIO_UART
    MIDI_PORT.begin(MIDI_BAUD, SERIAL_8N1);
#else
    Serial1.setTX(PIN_MIDI_TX);
  #if PIN_MIDI_RX >= 0
    Serial1.setRX(PIN_MIDI_RX);
  #endif
    Serial1.begin(MIDI_BAUD, SERIAL_8N1);
#endif
}

// ─── Transmit ───────────────────────────────────────────────────────────────
// No running-status compression on output: at 31250 baud a 16th note at 300 BPM
// is 12.5 ms and fits ~39 bytes, so the wire is never the bottleneck, and full
// status bytes keep the stream robust for anything downstream that resyncs.
void MidiIO::write1(uint8_t b) {
    if (!_outEnabled) return;
    MIDI_PORT.write(b);
    _actOut = true;
}
void MidiIO::write2(uint8_t a, uint8_t b) {
    if (!_outEnabled) return;
    uint8_t buf[2] = {a, b};
    MIDI_PORT.write(buf, 2);
    _actOut = true;
}
void MidiIO::write3(uint8_t a, uint8_t b, uint8_t c) {
    if (!_outEnabled) return;
    uint8_t buf[3] = {a, b, c};
    MIDI_PORT.write(buf, 3);
    _actOut = true;
}

void MidiIO::noteOn(uint8_t ch, uint8_t note, uint8_t vel) {
    ch = constrain(ch, 1, 16);
    write3(0x90 | (ch - 1), note & 0x7F, vel & 0x7F);
}
void MidiIO::noteOff(uint8_t ch, uint8_t note) {
    ch = constrain(ch, 1, 16);
    write3(0x80 | (ch - 1), note & 0x7F, 0);
}
void MidiIO::controlChange(uint8_t ch, uint8_t cc, uint8_t val) {
    ch = constrain(ch, 1, 16);
    write3(0xB0 | (ch - 1), cc & 0x7F, val & 0x7F);
}
void MidiIO::programChange(uint8_t ch, uint8_t prog) {
    ch = constrain(ch, 1, 16);
    write2(0xC0 | (ch - 1), prog & 0x7F);
}
void MidiIO::pitchBend(uint8_t ch, int16_t value) {
    ch = constrain(ch, 1, 16);
    uint16_t v = (uint16_t)(constrain((int)value, -8192, 8191) + 8192);
    write3(0xE0 | (ch - 1), v & 0x7F, (v >> 7) & 0x7F);
}
void MidiIO::allNotesOff(uint8_t ch) { controlChange(ch, 123, 0); }

void MidiIO::clock()       { write1(MIDI_RT_CLOCK); }
void MidiIO::start()       { write1(MIDI_RT_START); }
void MidiIO::stop()        { write1(MIDI_RT_STOP); }
void MidiIO::continueMsg() { write1(MIDI_RT_CONTINUE); }

// ─── Receive ────────────────────────────────────────────────────────────────
void MidiIO::poll() {
#if PIN_MIDI_RX >= 0
    // Bounded per call so a flood of clock bytes can never starve the UI loop.
    for (int guard = 0; guard < 64 && MIDI_PORT.available(); guard++) {
        uint8_t b = (uint8_t)MIDI_PORT.read();

        if (b >= 0xF8) {                       // real-time: interleaves anywhere,
            if (_onRT) _onRT(b);               // never disturbs running status
            _actIn = true;
            continue;
        }
        if (b >= 0x80) {                       // status
            if (b >= 0xF0) { _status = 0; _dataNeeded = 0; continue; }  // ignore SysEx/common
            _status  = b;
            _dataIdx = 0;
            uint8_t hi = b & 0xF0;
            _dataNeeded = (hi == 0xC0 || hi == 0xD0) ? 1 : 2;
            continue;
        }
        if (_status == 0) continue;            // data before any status
        _data[_dataIdx++] = b;
        if (_dataIdx >= _dataNeeded) {
            _dataIdx = 0;                      // running status: stay armed
            _actIn = true;
            dispatch();
        }
    }
#endif
}

void MidiIO::dispatch() {
    uint8_t ch = (_status & 0x0F) + 1;
    if (_inChannel != 0 && ch != _inChannel) return;

    switch (_status & 0xF0) {
    case 0x80:
        if (_onNoteOff) _onNoteOff(ch, _data[0], _data[1]);
        break;
    case 0x90:
        // Velocity 0 is the running-status idiom for note-off.
        if (_data[1] == 0) { if (_onNoteOff) _onNoteOff(ch, _data[0], 0); }
        else               { if (_onNoteOn)  _onNoteOn(ch, _data[0], _data[1]); }
        break;
    case 0xB0:
        if (_onCC) _onCC(ch, _data[0], _data[1]);
        break;
    case 0xC0:
        if (_onPC) _onPC(ch, _data[0]);
        break;
    case 0xE0:
        if (_onBend) _onBend(ch, (int16_t)(((int)_data[1] << 7 | _data[0]) - 8192));
        break;
    case 0xA0:
        // Polyphonic key pressure — the expression channel a CS-80 lives on.
        if (_onPolyAT) _onPolyAT(ch, _data[0], _data[1]);
        break;
    case 0xD0:
        if (_onChanAT) _onChanAT(ch, _data[0]);
        break;
    default:
        break;
    }
}

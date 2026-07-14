# WerewolfAmyS3 — Control & MIDI Map

## Panel pots (MUX A) → WOLF synth + MIDI CC out (LOCAL channel)

| Pot | Parameter | CC |
|---|---|---|
| P1 | OSC1 level | 12 |
| P2 | OSC2 level | 13 |
| P3 | OSC3 level | 14 |
| P4 | OSC1 pulse width | 15 |
| P5 | Filter cutoff | 74 |
| P6 | Filter resonance | 71 |
| P7 | Filter env amount | 70 |
| P8 | Glide | 5 |
| P9 | Amp attack | 73 |
| P10 | Amp decay | 75 |
| P11 | Amp sustain | 79 |
| P12 | Amp release | 72 |
| P13 | LFO rate | 76 |
| P14 | LFO → filter | 77 |
| P15 | Reverb send | 91 |
| P16 | Master volume | 7 |

Pots use **soft takeover**: after a parameter changes elsewhere (touch UI,
MIDI, preset load — use SETUP → "RESYNC PANEL POTS"), a pot re-engages only
when it crosses the current value.

The same CC numbers are accepted on MIDI input routed to WOLF, plus:
CC 78 = LFO→pitch, CC 93 = chorus send, CC 120/123 = all sound/notes off.

## Panel switches (MUX B)

| Switch | Action |
|---|---|
| SW1 | OSC1 wave (cycle) |
| SW2 | OSC2 wave (cycle) |
| SW3 | OSC3 wave (cycle) |
| SW4 | Filter type (cycle) |
| SW5 | LFO wave (cycle) |
| SW6 | Arp on/off |
| SW7 | Arp latch |
| SW8 | WOLF SEQ play/stop |
| SW9 | GM SEQ play/stop |
| SW10 | Tap tempo |
| SW11 | Preset − |
| SW12 | Preset + |
| SW13 | Jump to SYNTH page |
| SW14 | Jump to GM SEQ page |
| SW15 | SHIFT (reserved modifier) |
| SW16 | PANIC (all notes off, everywhere) |

## MIDI routing matrix (ROUTE page)

Sources → Destinations, all combinations allowed:

|            | WOLF | GM | DIN OUT | USB OUT |
|---|---|---|---|---|
| **DIN IN**   | ● default | ● default | soft-thru | bridge |
| **USB IN**   | ● default | ● default | bridge | soft-thru |
| **WOLF SEQ** | ● default | drive GM from wolf seq | sequence ext. gear | " |
| **GM SEQ**   | layer wolf | ● default | sequence ext. gear | " |
| **ARP**      | ● default | arpeggiate GM sounds | arpeggiate ext. gear | " |
| **LOCAL** (touch keys + pots) | ● default | play GM live | controller mode | " |

- **WOLF channel filter**: omni or a single channel (ROUTE page)
- **GM** consumes all 16 channels as a normal GM module (ch 10 = drums);
  program changes, CC7/10/64/121/123 and pitch bend are honoured
- **Clock**: INTERNAL / DIN CLK / USB CLK; clock + start/stop transmit
  toggles per output port
- Wolf-seq tracks and the arp are stamped with their own channel
  (track setting / LOCAL channel), so external gear can be driven per track.

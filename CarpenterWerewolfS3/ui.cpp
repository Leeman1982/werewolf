#include "ui.h"
#include <Wire.h>
#include "arps.h"
#include "minotaur_splash.h"
#include "midi.h"
#include "dsp_tables.h"
#include <stdio.h>
#include <string.h>

// ─── Hooks into the sketch ──────────────────────────────────────────────────
extern MidiIO  midi;
extern volatile float g_cpuLoad;        // 0..1, measured on core1
extern volatile int   g_activeVoices;
extern volatile float g_peakL, g_peakR;
extern int  g_currentPreset;
extern bool g_recArm;
void uiTogglePlayAll();
void uiStopAll();
void uiApplySync();
void uiSaveAll();
void uiLoadFactoryPreset(int idx);
void uiLoadUserPreset(uint8_t slot);
void uiSaveUserPreset(uint8_t slot, const char* name);
void uiLoadSong(uint8_t slot);
void uiSaveSong(uint8_t slot, const char* name);
void uiPanic();
void uiPreviewNote(uint8_t note, bool on);
void uiSetEngine(EngineId id);
void uiUsbBegin();
void uiUsbEnd();
void uiBanksMayHaveChanged();

// ─── Layout ─────────────────────────────────────────────────────────────────
// 128x64 SSD1309 geometry — the exact panel and row math the CS-80/New York
// Ripper board's own UI proved out. VIS_ROWS=4 at ROW_H=10 fills the content
// band between the header and footer with room to spare.
#define ROW_H       10
#define VIS_ROWS     4
#define CONTENT_Y   11
#define FOOTER_Y    53

// ─── Synth parameter pages ──────────────────────────────────────────────────
// Pages are not a fixed table: each engine (CS-80/Minimoog/Prophet-5/
// Prophet-10/...) declares its own EnginePageDef[] (engine.h), walked here
// through engineNumPages()/enginePage() instead of a SYNTH_PAGE[] fixed to
// one engine's parameter layout.

static const char* const MENU_LABELS[] = {
    "STEP EDIT", "PATTERN", "PAT OPTS", "CHAIN", "TOOLS",
    "ENGINE", "SYNTH", "PRESETS", "MIXER", "MIDI", "SETTINGS", "PERF", "HELP"
};

static const char* const STEP_FIELD_LABELS[] = {
    "Active", "Note 1", "Note 2", "Note 3", "Note 4", "Notes",
    "Velocity", "Gate", "Probab.", "Micro", "Ratchet",
    "Accent", "Slide", "Tie", "P-LOCKS >"
};

static const char* const TOOL_LABELS[] = {
    "Euclid hits", "Euclid rot", "APPLY EUCLID",
    "Rand dens", "RANDOMISE", "Human amt", "HUMANISE",
    "Riff", "LOAD RIFF", "CLEAR PATTERN", "COPY->CLIP", "PASTE CLIP"
};
#define NUM_TOOL_ROWS 12

// First riff in RIFFS[] tagged with `cat` (RIFF_CAT_ALL = no filter, index 0).
// Walks the table rather than binary-searching it: it's called on a button
// press, not per frame, and 200 entries is nothing at that rate.
static uint8_t firstRiffInCategory(uint8_t cat) {
    if (cat == RIFF_CAT_ALL) return 0;
    for (uint8_t i = 0; i < NUM_RIFFS; i++)
        if (RIFFS[i].cat == cat) return i;
    return 0;
}

// Next/previous riff from `cur`, wrapping, staying inside `cat`.
static uint8_t adjacentRiffInCategory(uint8_t cur, bool forward, uint8_t cat) {
    if (cat == RIFF_CAT_ALL)
        return (uint8_t)((cur + (forward ? 1 : (NUM_RIFFS - 1))) % NUM_RIFFS);
    uint8_t idx = cur;
    for (uint8_t n = 0; n < NUM_RIFFS; n++) {
        idx = (uint8_t)((idx + (forward ? 1 : (NUM_RIFFS - 1))) % NUM_RIFFS);
        if (RIFFS[idx].cat == cat) return idx;
    }
    return cur;   // cat has no members -- shouldn't happen, stay put
}

static const char* const MIDI_LABELS[] = {
    "In Chan", "Out Enable", "Clock Out", "Ext Sync", "Soft Thru", "Kbd Play", "PANIC"
};
#define NUM_MIDI_ROWS 7

static const char* const SETTING_LABELS[] = {
    "Master Vol", "Contrast", "Metronome", "SAVE ALL", "FORMAT FS", "Def Time Sig", "USB DRIVE"
};
#define NUM_SETTING_ROWS 7

static const char* const PATOPT_LABELS[] = {
    "Length", "Swing", "Subdiv", "Scale", "Root", "Transpose", "MIDI Chan", "Preset",
    "Time Sig"
};
#define NUM_PATOPT_ROWS 9

static const char* const HELP_LINES[] = {
    "ENC turn: move  push: edit",
    "ENC long: back / step on-off",
    "PLAY: start-stop  long: step",
    "TRACK: track  long: copy pat",
    "MUTE: step on-off  long: paste",
    "PAGE: menu-back long: save",
    "SHIFT+turn: BPM (main)/fast",
    "SHIFT+PLAY: record arm",
    "SHIFT+MUTE: mute track",
    "SHIFT+TRACK: next pattern",
    "OCT DN/UP: transpose MIDI kbd",
    "CUTOFF/RESONANCE pots: live",
};
#define NUM_HELP_LINES 12

// ─── Init ───────────────────────────────────────────────────────────────────
// Display bring-up is deliberately split from everything else and is the
// FIRST thing setup() does. If any later stage stalls, the panel is already
// alive and bootStatus() can say where — a blank screen is the one failure
// mode that tells you nothing. This is the CS-80/New York Ripper bring-up
// sequence verbatim — same order, same calls, same fixed contrast — because
// that build is known good on this exact board.
void UI::beginDisplay() {
    Wire.setSDA(PIN_OLED_SDA);
    Wire.setSCL(PIN_OLED_SCL);
    Wire.begin();
    _oled.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit address
    _oled.begin();
    _oled.setBusClock(OLED_I2C_HZ);
    _oled.setContrast(220);

    bootStatus("BOOTING");
}

// Splash plus one line of progress. Every boot stage calls this, so if the
// machine ever stops during startup the screen names the stage it died in.
// The wordmark is real Minotaur type, not an approximation in a stock bitmap
// font: minotaur_splash.h holds "CARPENTER", "SYNTH" and the "ZOMBI SS"
// footer rasterized straight from extras/font/minotaur.ttf by
// tools/ttf_to_xbm.py (the exact command is in that header's own banner).
// Fixed strings rather than a full glyph set, because these three are the
// only places the face is ever used and a whole font would cost flash for
// nothing.
//
// All three carry a pixel of extra tracking (two on ZOMBI SS). Minotaur is a
// display face drawn with almost no sidebearing, and at a dozen pixels tall
// on a 1-bit panel its adjacent strokes merge -- ZOMBI's B and I came out as
// one solid block. Checked by rendering the candidates, not by eye.
//
// The 240x240 build's splash was a single RGB565 bitmap with the type baked
// in; that art can't survive the drop to 128x64 1-bit, so the layout is
// rebuilt here in code from the same three elements it had -- title,
// tagline, ZOMBI SS credit -- with the status line kept in its own band at
// the bottom.  The status line is the one thing on this screen that has a
// job beyond looking good: every boot stage calls this, so if the machine
// ever stops during startup the screen names the stage it died in.
void UI::bootStatus(const char* msg) {
    _oled.clearBuffer();

    // Stacked wordmark, each line centred on its own width.  Heights come
    // from the generated header rather than being written out again here, so
    // re-rasterizing at a different size cannot silently overlap the rows
    // below.
    _oled.drawXBM((DISP_W - MINO_CARPENTER_W) / 2, 1,
                  MINO_CARPENTER_W, MINO_CARPENTER_H, MINO_CARPENTER_BITS);
    _oled.drawXBM((DISP_W - MINO_SYNTH_W) / 2, 1 + MINO_CARPENTER_H,
                  MINO_SYNTH_W, MINO_SYNTH_H, MINO_SYNTH_BITS);

    _oled.setFont(u8g2_font_4x6_tr);
    const char* tag = "STEP SEQ & SYNTH";
    _oled.drawStr((DISP_W - _oled.getStrWidth(tag)) / 2, 36, tag);

    _oled.drawXBM((DISP_W - MINO_ZOMBI_W) / 2, 39,
                  MINO_ZOMBI_W, MINO_ZOMBI_H, MINO_ZOMBI_BITS);

    // Status band: its own rule and its own row, so a message never collides
    // with the wordmark above and a stalled boot is easy to read off the panel.
    _oled.drawHLine(0, 52, DISP_W);
    if (msg && *msg) {
        _oled.setFont(u8g2_font_6x10_tr);
        int w = _oled.getStrWidth(msg);
        _oled.drawStr((DISP_W - w) / 2, 62, msg);
    }
    _oled.sendBuffer();
}

void UI::bind(Sequencer* seqs, uint8_t* activeTrack, Storage* storage, GlobalSettings* gs) {
    _seqs = seqs; _track = activeTrack; _store = storage; _gs = gs;
    _dirty = true;
}

void UI::applyContrast(uint8_t c) {
    // Never let a restored setting black the panel out — that is
    // indistinguishable from a dead board.
    if (c < 40) c = 40;
    _oled.setContrast(c);
}

void UI::toast(const char* msg) {
    strncpy(_toastMsg, msg, sizeof(_toastMsg) - 1);
    _toastMsg[sizeof(_toastMsg) - 1] = 0;
    _toastUntil = millis() + 1100;
    _dirty = true;
}

void UI::gotoScreen(Screen s) {
    _screen = s;
    _editing = false;
    _row = 0; _scroll = 0;
    _dirty = true;
}

// ─── Row counts ─────────────────────────────────────────────────────────────
int UI::rowCount() const {
    switch (_screen) {
    case Screen::MENU:         return (int)MenuItem::COUNT;
    case Screen::STEP_EDIT:    return (int)StepField::COUNT;
    case Screen::PLOCK_EDIT:   return STEP_LOCKS * 2 + 1;   // id/value pairs + clear
    case Screen::PATTERN_SEL:  return NUM_PATTERNS;
    case Screen::PATTERN_OPTS: return NUM_PATOPT_ROWS;
    case Screen::CHAIN_EDIT:   return CHAIN_LEN + 3;        // slots + APPLY + LOAD SONG + SAVE SONG
    case Screen::TOOLS:        return NUM_TOOL_ROWS;
    case Screen::ENGINE_SEL:   return NUM_ENGINES;
    case Screen::SYNTH_PAGES:  return engineNumPages();
    case Screen::SYNTH_EDIT:   return enginePage(_synthPage)->count;
    case Screen::PRESETS:      return engineNumPresets() + USER_PRESET_SLOTS;
    case Screen::PRESET_SAVE:  return 3;                    // slot, name, SAVE
    case Screen::MIXER:        return NUM_TRACKS;
    case Screen::MIDI_SET:     return NUM_MIDI_ROWS;
    case Screen::SETTINGS:     return NUM_SETTING_ROWS;
    case Screen::HELP:         return NUM_HELP_LINES;
    case Screen::SONG_BROWSE:  return SONG_SLOTS;
    case Screen::SONG_SAVE_NAME: return 2;     // name, SAVE
    default:                   return 0;
    }
}

void UI::ensureVisible(int visibleRows) {
    if (_row < _scroll) _scroll = _row;
    if (_row >= _scroll + visibleRows) _scroll = _row - visibleRows + 1;
    int n = rowCount();
    if (_scroll > n - visibleRows) _scroll = n - visibleRows;
    if (_scroll < 0) _scroll = 0;
}

void UI::moveRow(int delta) {
    int n = rowCount();
    if (n <= 0) return;
    _row += delta;
    if (_row < 0) _row = n - 1;
    if (_row >= n) _row = 0;
    ensureVisible(VIS_ROWS);
}

// ─── Encoder ────────────────────────────────────────────────────────────────
void UI::onEncoder(int delta) {
    if (delta == 0) return;
    _dirty = true;
    // SHIFT multiplies the step so long ranges (BPM, note, cutoff) are reachable
    // without spinning the encoder for a minute.
    int fast = _shift ? 8 : 1;

    if (_screen == Screen::MAIN) {
        if (_shift) {
            for (uint8_t t = 0; t < NUM_TRACKS; t++) _seqs[t].nudgeBPM(delta);
            _gs->bpm = seq().bpm();
        } else {
            int len = pat().length ? pat().length : 1;
            int s = (int)_selStep + delta;
            while (s < 0) s += len;
            _selStep = (uint8_t)(s % len);
        }
        return;
    }

    if (_editing) editValue(delta * fast);
    else          moveRow(delta * fast);
}

void UI::onEncPress() {
    _dirty = true;
    switch (_screen) {
    case Screen::MAIN:
        _row = 0; _scroll = 0;
        gotoScreen(Screen::STEP_EDIT);
        break;
    default:
        activateRow();
        break;
    }
}

void UI::onEncLong() {
    _dirty = true;
    if (_screen == Screen::MAIN) {
        curStep().flags ^= ST_ACTIVE;
        toast((curStep().flags & ST_ACTIVE) ? "STEP ON" : "STEP OFF");
    } else {
        _editing = false;
        onPage();
    }
}

// ─── Buttons ────────────────────────────────────────────────────────────────
void UI::onPlay() {
    _dirty = true;
    if (_shift) {
        g_recArm = !g_recArm;
        for (uint8_t t = 0; t < NUM_TRACKS; t++) _seqs[t].setRecord(g_recArm && t == *_track);
        toast(g_recArm ? "REC ARMED" : "REC OFF");
        return;
    }
    if (_screen == Screen::STEP_EDIT || _screen == Screen::PLOCK_EDIT) {
        seq().auditionStep(_selStep);
        return;
    }
    uiTogglePlayAll();
}

void UI::onPlayLong() {
    _dirty = true;
    if (_screen == Screen::MAIN || _screen == Screen::STEP_EDIT) {
        curStep().flags ^= ST_ACTIVE;
        toast((curStep().flags & ST_ACTIVE) ? "STEP ON" : "STEP OFF");
    } else {
        uiStopAll();
        toast("STOP ALL");
    }
}

void UI::onPage() {
    _dirty = true;
    if (_editing) { _editing = false; return; }
    switch (_screen) {
    case Screen::MAIN:        gotoScreen(Screen::MENU); break;
    case Screen::MENU:        gotoScreen(Screen::MAIN); break;
    case Screen::PLOCK_EDIT:  gotoScreen(Screen::STEP_EDIT); break;
    case Screen::SYNTH_EDIT:  gotoScreen(Screen::SYNTH_PAGES); break;
    case Screen::PRESET_SAVE:    gotoScreen(Screen::PRESETS);    break;
    case Screen::SONG_BROWSE:    gotoScreen(Screen::CHAIN_EDIT); break;
    case Screen::SONG_SAVE_NAME: gotoScreen(Screen::SONG_BROWSE); break;
    case Screen::USB_MODE:       uiUsbEnd(); gotoScreen(Screen::SETTINGS); break;
    default:                     gotoScreen(Screen::MENU);       break;
    }
}

void UI::onPageLong() {
    if (_store && !_store->mounted()) {
        toast("NO FS - SEE SETTINGS");
    } else if (g_diskMode) {
        // A save here would silently no-op (every Storage method refuses
        // while the host owns the filesystem) -- say so rather than claim
        // SAVED for something that didn't happen.
        toast("USB DRIVE ACTIVE");
    } else {
        uiSaveAll();
        toast("SAVED");
    }
    _dirty = true;
}

void UI::onTrack() {
    _dirty = true;
    if (_screen == Screen::STEP_EDIT || _screen == Screen::PLOCK_EDIT) {
        // In the step editor the TRACK button steps through the pattern, which
        // is far quicker than backing out to the grid for every note.
        uint8_t len = pat().length ? pat().length : 1;
        _selStep = _shift ? (uint8_t)((_selStep + len - 1) % len)
                          : (uint8_t)((_selStep + 1) % len);
        return;
    }
    if (_shift) {   // SHIFT+TRACK on any other screen = next pattern
        uint8_t next = (uint8_t)((seq().currentPattern() + 1) % NUM_PATTERNS);
        seq().setPattern(next);
        _gs->lastPattern[*_track] = next;
        char b[20]; snprintf(b, sizeof(b), "PATTERN %u", (unsigned)(next + 1));
        toast(b);
        return;
    }
    *_track = (uint8_t)((*_track + 1) % NUM_TRACKS);
    _selStep = 0;
    char b[20];
    snprintf(b, sizeof(b), "TRACK %u%s", (unsigned)(*_track + 1),
             (*_track == INTERNAL_TRACK) ? " SYNTH" : " MIDI");
    toast(b);
}

void UI::onTrackLong() {
    _clip = pat();
    _clipFull = true;
    toast("PATTERN COPIED");
    _dirty = true;
}

void UI::onMute() {
    _dirty = true;
    if (_shift) {
        seq().setMuted(!seq().muted());
        _gs->trackMute[*_track] = seq().muted() ? 1 : 0;
        toast(seq().muted() ? "TRACK MUTED" : "TRACK LIVE");
        return;
    }
    if (_screen == Screen::MAIN || _screen == Screen::STEP_EDIT) {
        curStep().flags ^= ST_ACTIVE;
        return;
    }
    if (_screen == Screen::MIXER) {
        uint8_t r = (uint8_t)constrain((int)_row, 0, NUM_TRACKS - 1);
        _seqs[r].setMuted(!_seqs[r].muted());
        _gs->trackMute[r] = _seqs[r].muted() ? 1 : 0;
        return;
    }
    // Elsewhere MUTE is a quick jump back to the grid.
    gotoScreen(Screen::MAIN);
}

void UI::onMuteLong() {
    _dirty = true;
    if (!_clipFull) { toast("CLIPBOARD EMPTY"); return; }
    uint8_t ch = pat().midiChannel;
    pat() = _clip;
    pat().midiChannel = ch;          // paste the music, keep this track's routing
    seq().refreshTiming();
    toast("PATTERN PASTED");
}

// ─── Row activation / editing ───────────────────────────────────────────────
void UI::activateRow() {
    switch (_screen) {

    case Screen::MENU: {
        static const Screen DEST[] = {
            Screen::STEP_EDIT, Screen::PATTERN_SEL, Screen::PATTERN_OPTS,
            Screen::CHAIN_EDIT, Screen::TOOLS, Screen::ENGINE_SEL, Screen::SYNTH_PAGES,
            Screen::PRESETS, Screen::MIXER, Screen::MIDI_SET,
            Screen::SETTINGS, Screen::PERF, Screen::HELP
        };
        gotoScreen(DEST[_row]);
        break;
    }

    case Screen::STEP_EDIT:
        if ((StepField)_row == StepField::LOCKS) { gotoScreen(Screen::PLOCK_EDIT); break; }
        _editing = !_editing;
        break;

    case Screen::PLOCK_EDIT:
        if (_row == STEP_LOCKS * 2) {            // CLEAR ALL
            for (uint8_t i = 0; i < STEP_LOCKS; i++) curStep().lockId[i] = LOCK_NONE;
            toast("LOCKS CLEARED");
        } else {
            _editing = !_editing;
        }
        break;

    case Screen::PATTERN_SEL:
        seq().setPattern((uint8_t)_row);
        _gs->lastPattern[*_track] = (uint8_t)_row;
        _selStep = 0;
        toast("PATTERN LOADED");
        break;

    case Screen::PATTERN_OPTS:
        _editing = !_editing;
        break;

    case Screen::CHAIN_EDIT:
        if (_row == CHAIN_LEN + 2) {              // SAVE SONG
            _songSaving = true;
            gotoScreen(Screen::SONG_BROWSE);
        } else if (_row == CHAIN_LEN + 1) {        // LOAD SONG
            _songSaving = false;
            gotoScreen(Screen::SONG_BROWSE);
        } else if (_row == CHAIN_LEN) {            // APPLY
            ChainEntry c[CHAIN_LEN];
            uint8_t n = 0;
            for (uint8_t i = 0; i < CHAIN_LEN; i++) {
                if (_gs->chain[*_track][i].patternIdx < 0) break;
                c[n++] = _gs->chain[*_track][i];
            }
            if (n) { seq().setChain(c, n); toast("CHAIN ON"); }
            else   { seq().clearChain();   toast("CHAIN OFF"); }
            _gs->chainLen[*_track] = n;
        } else {
            _editing = !_editing;
        }
        break;

    case Screen::SONG_BROWSE:
        if (_shift) {
            if (_store->deleteSong((uint8_t)_row)) toast("DELETED");
            else                                    toast("EMPTY SLOT");
        } else if (_songSaving) {
            _songSlot = (uint8_t)_row;
            // Same priming as PRESETS' SHIFT+save: space-pad to a fixed 13
            // characters so the name editor never touches an uninitialised byte.
            memset(_nameBuf, ' ', 13);
            _nameBuf[13] = 0;
            char tmp[14];
            snprintf(tmp, sizeof(tmp), "SONG %03u", (unsigned)(_songSlot + 1));
            memcpy(_nameBuf, tmp, strlen(tmp));
            _nameCursor = 0;
            gotoScreen(Screen::SONG_SAVE_NAME);
        } else if (_store->songExists((uint8_t)_row)) {
            uiLoadSong((uint8_t)_row);
            gotoScreen(Screen::MAIN);
        } else {
            toast("EMPTY SLOT");
        }
        break;

    case Screen::SONG_SAVE_NAME:
        if (_row == 1) {
            uiSaveSong(_songSlot, _nameBuf);
            toast("SONG SAVED");
            gotoScreen(Screen::SONG_BROWSE);
        } else {
            _editing = !_editing;
        }
        break;

    case Screen::TOOLS: {
        Pattern& p = pat();
        uint8_t pi = seq().currentPattern();
        switch (_row) {
        case 0: case 1: case 3: case 5: case 7: _editing = !_editing; break;
        case 2: seq().euclidFill(pi, _eucHits, _eucRot); toast("EUCLID"); break;
        case 4: seq().randomisePattern(pi, _randDensity); toast("RANDOMISED"); break;
        case 6: seq().humanise(pi, _humanAmt); toast("HUMANISED"); break;
        case 8: {                                        // LOAD RIFF
            const Riff& r = RIFFS[_riffSel % NUM_RIFFS];
            uint8_t len = p.length ? p.length : 16;
            for (uint8_t s = 0; s < len; s++) {
                Step& st = p.steps[s];
                int8_t off = r.note[s % 16];
                if (off < 0) { st.flags &= (uint8_t)~ST_ACTIVE; continue; }
                st.flags     = ST_ACTIVE;
                st.noteCount = 1;
                st.notes[0]  = (uint8_t)constrain(36 + (int)p.rootNote + (int)off, 0, 127);
                st.velocity  = r.vel[s % 16] ? r.vel[s % 16] : 100;
                st.gate      = r.gate;
                st.probability = 100;
                st.ratchet   = 1;
                st.micro     = 0;
                if (st.velocity > 110) st.flags |= ST_ACCENT;
            }
            toast(r.name);
            break;
        }
        case 9:
            seq().clearPattern(pi);
            // clearPattern() resets to Pattern()'s built-in 4/4 default;
            // re-apply the global default time sig on top of that, same as
            // a freshly-cleared pattern should start on.
            p.timeSigIdx = _gs->defaultTimeSigIdx % NUM_TIME_SIGS;
            p.length     = TIME_SIGS[p.timeSigIdx].steps16;
            seq().refreshTiming();
            toast("CLEARED");
            break;
        case 10: _clip = p; _clipFull = true; toast("COPIED"); break;
        case 11: if (_clipFull) { uint8_t ch = p.midiChannel; p = _clip; p.midiChannel = ch;
                                  seq().refreshTiming(); toast("PASTED"); }
                 else toast("CLIP EMPTY");
                 break;
        }
        break;
    }

    case Screen::ENGINE_SEL:
        if (ENGINE_IS_STUB[_row]) { toast("NOT MODELLED YET"); break; }
        uiSetEngine((EngineId)_row);
        toast(ENGINE_NAMES[_row]);
        gotoScreen(Screen::MENU);
        break;

    case Screen::SYNTH_PAGES:
        _synthPage = (uint8_t)_row;
        gotoScreen(Screen::SYNTH_EDIT);
        break;

    case Screen::SYNTH_EDIT:
        _editing = !_editing;
        break;

    case Screen::PRESETS: {
        uint8_t numPresets = engineNumPresets();
        if (_shift) {                            // SHIFT+press = save to a slot
            _saveSlot = (uint8_t)((_row >= numPresets) ? (_row - numPresets) : 0);
            // Space-pad to a fixed 13 characters: the name editor addresses
            // every position, so there must be no uninitialised bytes in there.
            memset(_nameBuf, ' ', 13);
            _nameBuf[13] = 0;
            char tmp[14];
            snprintf(tmp, sizeof(tmp), "USER %02u", (unsigned)(_saveSlot + 1));
            memcpy(_nameBuf, tmp, strlen(tmp));
            _nameCursor = 0;
            gotoScreen(Screen::PRESET_SAVE);
        } else if (_row < numPresets) {
            uiLoadFactoryPreset(_row);
            toast(enginePresetName(_row));
        } else {
            uiLoadUserPreset((uint8_t)(_row - numPresets));
        }
        break;
    }

    case Screen::PRESET_SAVE:
        if (_row == 2) { uiSaveUserPreset(_saveSlot, _nameBuf); toast("PRESET SAVED");
                         gotoScreen(Screen::PRESETS); }
        else _editing = !_editing;
        break;

    case Screen::MIXER:
        _mixRow = (uint8_t)_row;
        _editing = !_editing;
        break;

    case Screen::MIDI_SET:
        if (_row == 6) { uiPanic(); toast("ALL NOTES OFF"); }
        else _editing = !_editing;
        break;

    case Screen::SETTINGS:
        if (_row == 3) {
            if (!_store->mounted()) { toast("NO FS - FORMAT 1ST"); break; }
            uiSaveAll();
            toast("SAVED");
        } else if (_row == 4) {
            // Destructive and slow (tens of seconds on an 8 MB partition), so
            // it needs SHIFT held, and it draws before it blocks rather than
            // looking like a freeze.
            if (!_shift) { toast("HOLD SHIFT TO FORMAT"); break; }
            bootStatus("FORMATTING FS...");
            _store->formatAll();
            toast(_store->mounted() ? "FS FORMATTED" : "FORMAT FAILED");
            // A format wipes /banks along with everything else -- rescan
            // rather than leave a DX7 voice pointing at a file that no
            // longer exists (see uiBanksMayHaveChanged() in the sketch).
            uiBanksMayHaveChanged();
        } else if (_row == 6) {
            // Exclusive: stops the transport and hands the filesystem to
            // whatever the encoder/USB cable is plugged into.
            if (!_store->mounted()) { toast("NO FS - FORMAT 1ST"); break; }
            uiStopAll();
            uiPanic();
            uiUsbBegin();
            gotoScreen(Screen::USB_MODE);
        } else {
            _editing = !_editing;
        }
        break;

    default:
        break;
    }
    _dirty = true;
}

void UI::editValue(int d) {
    switch (_screen) {

    case Screen::STEP_EDIT: {
        Step& st = curStep();
        switch ((StepField)_row) {
        case StepField::ACTIVE:  st.flags ^= ST_ACTIVE; _editing = false; break;
        case StepField::NOTE1: case StepField::NOTE2:
        case StepField::NOTE3: case StepField::NOTE4: {
            uint8_t i = (uint8_t)_row - (uint8_t)StepField::NOTE1;
            st.notes[i] = (uint8_t)constrain((int)st.notes[i] + d, 0, 127);
            uiPreviewNote(st.notes[i], true);
            break;
        }
        case StepField::NOTECOUNT:
            st.noteCount = (uint8_t)constrain((int)st.noteCount + (d > 0 ? 1 : -1), 1, STEP_MAX_NOTES);
            break;
        case StepField::VELOCITY: st.velocity = (uint8_t)constrain((int)st.velocity + d, 1, 127); break;
        case StepField::GATE:     st.gate = (uint8_t)constrain((int)st.gate + d, 1, 200); break;
        case StepField::PROB:     st.probability = (uint8_t)constrain((int)st.probability + d, 0, 100); break;
        case StepField::MICRO:    st.micro = (int8_t)constrain((int)st.micro + d, -50, 50); break;
        case StepField::RATCHET:  st.ratchet = (uint8_t)constrain((int)st.ratchet + (d > 0 ? 1 : -1), 1, 8); break;
        case StepField::ACCENT:   st.flags ^= ST_ACCENT; _editing = false; break;
        case StepField::SLIDE:    st.flags ^= ST_SLIDE;  _editing = false; break;
        case StepField::TIE:      st.flags ^= ST_TIE;    _editing = false; break;
        default: break;
        }
        break;
    }

    case Screen::PLOCK_EDIT: {
        Step& st = curStep();
        uint8_t slot = (uint8_t)(_row / 2);
        if (slot >= STEP_LOCKS) break;
        if ((_row & 1) == 0) {
            // Parameter selector: -1 means "no lock", then every lockable id.
            int cur = (st.lockId[slot] == LOCK_NONE) ? -1 : (int)st.lockId[slot];
            int next = cur;
            uint8_t numParams = engineNumParams();
            for (int guard = 0; guard < (int)numParams + 2; guard++) {
                next += (d > 0 ? 1 : -1);
                if (next < -1) next = (int)numParams - 1;
                if (next >= (int)numParams) next = -1;
                if (next == -1 || engineParamIsLockable((uint8_t)next)) break;
            }
            if (next < 0) st.lockId[slot] = LOCK_NONE;
            else {
                st.lockId[slot] = (uint8_t)next;
                if (cur < 0) st.lockVal[slot] = (uint8_t)(engineGetParameter((uint8_t)next) * 255.0f);
            }
        } else {
            st.lockVal[slot] = (uint8_t)constrain((int)st.lockVal[slot] + d * 4, 0, 255);
        }
        break;
    }

    case Screen::PATTERN_OPTS: {
        Pattern& p = pat();
        switch (_row) {
        case 0: p.length = (uint8_t)constrain((int)p.length + d, 1, NUM_STEPS); break;
        case 1: p.swing  = (uint8_t)constrain((int)p.swing + d, 0, SWING_MAX); break;
        case 2: p.subdiv = (uint8_t)constrain((int)p.subdiv + (d > 0 ? 1 : -1), 0, NUM_SUBDIVS - 1);
                seq().refreshTiming(); break;
        case 3: p.scaleIdx = (uint8_t)constrain((int)p.scaleIdx + (d > 0 ? 1 : -1), 0, NUM_SCALES - 1); break;
        case 4: p.rootNote = (uint8_t)(((int)p.rootNote + (d > 0 ? 1 : 11)) % 12); break;
        case 5: p.transpose = (int8_t)constrain((int)p.transpose + (d > 0 ? 1 : -1), -24, 24); break;
        case 6: p.midiChannel = (uint8_t)constrain((int)p.midiChannel + (d > 0 ? 1 : -1), 1, 16); break;
        case 7: {
            int numPresets = (int)engineNumPresets();
            int v = (p.presetIdx == 0xFF) ? -1 : (int)p.presetIdx;
            v += (d > 0 ? 1 : -1);
            if (v < -1) v = numPresets - 1;
            if (v >= numPresets) v = -1;
            p.presetIdx = (v < 0) ? 0xFF : (uint8_t)v;
            break;
        }
        case 8: {
            // Sets length to the meter's suggested step count; length stays a
            // free row above and can still be hand-tuned after picking this.
            int v = (int)p.timeSigIdx + (d > 0 ? 1 : -1);
            if (v < 0) v = NUM_TIME_SIGS - 1;
            if (v >= NUM_TIME_SIGS) v = 0;
            p.timeSigIdx = (uint8_t)v;
            p.length = TIME_SIGS[v].steps16;
            seq().refreshTiming();
            break;
        }
        }
        break;
    }

    case Screen::CHAIN_EDIT: {
        if (_row >= CHAIN_LEN) break;
        ChainEntry& c = _gs->chain[*_track][_row];
        if (_shift) c.repeats = (uint8_t)constrain((int)c.repeats + (d > 0 ? 1 : -1), 1, 16);
        else {
            int v = c.patternIdx + (d > 0 ? 1 : -1);
            if (v < -1) v = NUM_PATTERNS - 1;
            if (v >= NUM_PATTERNS) v = -1;
            c.patternIdx = (int8_t)v;
            if (c.repeats == 0) c.repeats = 1;
        }
        break;
    }

    case Screen::TOOLS:
        switch (_row) {
        case 0: _eucHits = (uint8_t)constrain((int)_eucHits + (d > 0 ? 1 : -1), 0, NUM_STEPS); break;
        case 1: _eucRot  = (uint8_t)constrain((int)_eucRot + (d > 0 ? 1 : -1), 0, NUM_STEPS - 1); break;
        case 3: _randDensity = (uint8_t)constrain((int)_randDensity + d, 0, 100); break;
        case 5: _humanAmt = (uint8_t)constrain((int)_humanAmt + d, 0, 60); break;
        case 7:
            if (_shift) {
                _riffCatFilter = (uint8_t)((_riffCatFilter + (d > 0 ? 1 : RIFF_CAT_COUNT - 1)) % RIFF_CAT_COUNT);
                _riffSel = firstRiffInCategory(_riffCatFilter);
            } else {
                _riffSel = adjacentRiffInCategory(_riffSel, d > 0, _riffCatFilter);
            }
            break;
        }
        break;

    case Screen::SYNTH_EDIT: {
        uint8_t id = enginePage(_synthPage)->first + (uint8_t)_row;
        const ParamInfo& pi = *engineParamInfo(id);
        float v = engineGetParameter(id);
        if (pi.kind == PK_TOGGLE)      { v = (v > 0.5f) ? 0.0f : 1.0f; _editing = false; }
        else if (pi.kind == PK_ENUM)   { v += (d > 0 ? 1.0f : -1.0f) / (float)pi.numOpts; }
        else if (pi.kind == PK_INT)    { float st = 1.0f / (float)max(1, pi.dispMax - pi.dispMin);
                                         v += (d > 0 ? st : -st); }
        else                            { v += (float)d * 0.02f; }
        engineSetParameter(id, constrain(v, 0.0f, 1.0f));
        break;
    }

    case Screen::PRESET_SAVE:
        if (_row == 0) {
            _saveSlot = (uint8_t)constrain((int)_saveSlot + (d > 0 ? 1 : -1), 0, USER_PRESET_SLOTS - 1);
        } else if (_row == 1) {
            // Name editor: turn picks the character, SHIFT+turn moves the caret.
            if (_shift) {
                _nameCursor = (uint8_t)constrain((int)_nameCursor + (d > 0 ? 1 : -1), 0, 12);
            } else {
                static const char CHARSET[] =
                    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
                const int n = (int)sizeof(CHARSET) - 1;
                const char* p = strchr(CHARSET, _nameBuf[_nameCursor]);
                int idx = (p && *p) ? (int)(p - CHARSET) : 0;
                idx = (idx + (d > 0 ? 1 : n - 1)) % n;
                _nameBuf[_nameCursor] = CHARSET[idx];
                _nameBuf[13] = 0;
            }
        }
        break;

    case Screen::SONG_SAVE_NAME:
        if (_row == 0) {
            // Same name editor as PRESET_SAVE, just living on row 0 here since
            // the slot was already chosen back in SONG_BROWSE.
            if (_shift) {
                _nameCursor = (uint8_t)constrain((int)_nameCursor + (d > 0 ? 1 : -1), 0, 12);
            } else {
                static const char CHARSET[] =
                    " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_.";
                const int n = (int)sizeof(CHARSET) - 1;
                const char* p = strchr(CHARSET, _nameBuf[_nameCursor]);
                int idx = (p && *p) ? (int)(p - CHARSET) : 0;
                idx = (idx + (d > 0 ? 1 : n - 1)) % n;
                _nameBuf[_nameCursor] = CHARSET[idx];
                _nameBuf[13] = 0;
            }
        }
        break;

    case Screen::MIXER: {
        Pattern& p = _seqs[_mixRow].currentPatternRef();
        p.midiChannel = (uint8_t)constrain((int)p.midiChannel + (d > 0 ? 1 : -1), 1, 16);
        break;
    }

    case Screen::MIDI_SET:
        switch (_row) {
        case 0: _gs->midiInChannel = (uint8_t)constrain((int)_gs->midiInChannel + (d > 0 ? 1 : -1), 0, 16);
                midi.setInChannel(_gs->midiInChannel); break;
        case 1: _gs->midiOutEnable = !_gs->midiOutEnable; midi.setOutEnabled(_gs->midiOutEnable);
                _editing = false; break;
        case 2: _gs->midiClockOut = !_gs->midiClockOut; uiApplySync(); _editing = false; break;
        case 3: _gs->extSync = !_gs->extSync; uiApplySync(); _editing = false; break;
        case 4: _gs->midiThru = !_gs->midiThru; _editing = false; break;
        case 5: _gs->midiKbdPlay = !_gs->midiKbdPlay; _editing = false; break;
        }
        break;

    case Screen::SETTINGS:
        switch (_row) {
        case 0: { uint8_t mp = engineMasterParamId();
                  engineSetParameter(mp, constrain(engineGetParameter(mp) + d * 0.02f, 0.0f, 1.0f)); break; }
        case 1: _gs->contrast = (uint8_t)constrain((int)_gs->contrast + d * 4, 0, 255);
                _oled.setContrast(_gs->contrast); break;
        case 2: _gs->metronome = !_gs->metronome; _editing = false; break;
        case 5: {
            int v = (int)_gs->defaultTimeSigIdx + (d > 0 ? 1 : -1);
            if (v < 0) v = NUM_TIME_SIGS - 1;
            if (v >= NUM_TIME_SIGS) v = 0;
            _gs->defaultTimeSigIdx = (uint8_t)v;
            break;
        }
        }
        break;

    default: break;
    }
    _dirty = true;
}

// ─── Drawing ────────────────────────────────────────────────────────────────
void UI::update() {
    unsigned long now = millis();
    // Redraw on a fixed cadence while playing (the playhead animates) and only
    // on demand when stopped, so a stopped machine leaves the I2C bus alone.
    bool animating = (seq().playState() == PlayState::PLAYING) || (_toastUntil > now);
    if (!_dirty && !animating) return;
    if (now - _lastDraw < UI_REFRESH_MS) return;
    _lastDraw = now;
    _dirty = false;
    drawAll();
}

void UI::drawBar(int x, int y, int w, float v) {
    _oled.drawFrame(x, y, w, 6);
    int fw = (int)(v * (float)(w - 2) + 0.5f);
    fw = constrain(fw, 0, w - 2);
    if (fw > 0) _oled.drawBox(x + 1, y + 1, fw, 4);
}

void UI::drawListRow(int y, bool sel, const char* label, const char* value) {
    if (sel) {
        _oled.drawBox(0, y, DISP_W, ROW_H - 1);
        _oled.setDrawColor(0);
    }
    _oled.drawStr(2, y + 8, label);
    if (value) {
        int w = _oled.getStrWidth(value);
        _oled.drawStr(DISP_W - 3 - w, y + 8, value);
    }
    if (sel) _oled.setDrawColor(1);
}

void UI::drawHeader() {
    _oled.setFont(u8g2_font_5x7_tr);
    char buf[26];
    Sequencer& s = seq();
    // T1.P1  120  ▶ R
    snprintf(buf, sizeof(buf), "T%u%c P%u %u",
             (unsigned)(*_track + 1), s.muted() ? 'x' : '.',
             (unsigned)(s.currentPattern() + 1), (unsigned)s.bpm());
    _oled.drawStr(0, 7, buf);

    // Transport icons are built from boxes and vertical lines only.  drawDisc()
    // and drawTriangle() are documented as unreliable on this SSD1309 config,
    // and a UI glyph is not worth finding out where that goes wrong.
    int x = 66;
    if (s.playState() == PlayState::PLAYING) {
        for (int i = 0; i < 4; i++) _oled.drawVLine(x + i, 1 + i, 7 - 2 * i);
        x += 9;
    } else if (s.playState() == PlayState::PAUSED) {
        _oled.drawBox(x, 1, 2, 7); _oled.drawBox(x + 3, 1, 2, 7);
        x += 8;
    }
    if (g_recArm) { _oled.drawBox(x + 1, 2, 5, 5); x += 9; }
    if (s.externalSync()) { _oled.drawStr(x, 7, "EXT"); x += 17; }
    else if (s.midiClockOut()) { _oled.drawStr(x, 7, "CLK"); x += 17; }

    // Output level meter, right-aligned.
    float peak = max(g_peakL, g_peakR);
    int mw = constrain((int)(peak * 22.0f), 0, 22);
    _oled.drawFrame(DISP_W - 24, 1, 24, 6);
    if (mw > 0) _oled.drawBox(DISP_W - 23, 2, mw, 4);

    _oled.drawHLine(0, 9, DISP_W);
}

void UI::drawFooter(const char* hint) {
    _oled.drawHLine(0, FOOTER_Y, DISP_W);
    _oled.setFont(u8g2_font_5x7_tr);
    if (_toastUntil > millis()) {
        int w = _oled.getStrWidth(_toastMsg);
        _oled.drawStr((DISP_W - w) / 2, FOOTER_Y + 9, _toastMsg);
    } else if (hint) {
        _oled.drawStr(1, FOOTER_Y + 9, hint);
    }
    if (_editing) {
        _oled.drawStr(DISP_W - 22, FOOTER_Y + 9, "EDIT");
    }
}

void UI::drawStepCell(uint8_t step, int x, int y, bool cursor, bool playing) {
    const Step& st = pat().steps[step];
    bool inLen = step < pat().length;

    if (playing) _oled.drawBox(x, y, STEP_CELL_W - 1, STEP_CELL_H - 1);
    else         _oled.drawFrame(x, y, STEP_CELL_W - 1, STEP_CELL_H - 1);

    if (playing) _oled.setDrawColor(0);

    if (st.active()) {
        // Filled block = note; the height encodes velocity so the whole page
        // reads as a dynamics contour at a glance.
        int h = 2 + (int)st.velocity * (STEP_CELL_H - 6) / 127;
        _oled.drawBox(x + 3, y + STEP_CELL_H - 3 - h, STEP_CELL_W - 7, h);
        if (st.accent()) _oled.drawPixel(x + 2, y + 2);
        if (st.slide())  _oled.drawPixel(x + STEP_CELL_W - 4, y + 2);
        if (st.ratchet > 1) _oled.drawPixel(x + STEP_CELL_W - 4, y + STEP_CELL_H - 4);
        if (st.lockId[0] != LOCK_NONE) _oled.drawPixel(x + 2, y + STEP_CELL_H - 4);
    } else if (!inLen) {
        _oled.drawPixel(x + STEP_CELL_W / 2 - 1, y + STEP_CELL_H / 2 - 1);
    }

    if (playing) _oled.setDrawColor(1);
    if (cursor) _oled.drawFrame(x - 1, y - 1, STEP_CELL_W + 1, STEP_CELL_H + 1);
}

void UI::drawMain() {
    Sequencer& s = seq();
    uint8_t page = _selStep / STEPS_PER_PAGE;
    uint8_t base = page * STEPS_PER_PAGE;

    for (uint8_t i = 0; i < STEPS_PER_PAGE; i++) {
        uint8_t st = base + i;
        if (st >= NUM_STEPS) break;
        int x = (i % 8) * STEP_CELL_W;
        int y = (i < 8) ? STEP_ROW1_Y : STEP_ROW2_Y;
        bool playing = (s.playState() == PlayState::PLAYING) && (st == s.currentStep());
        drawStepCell(st, x, y, st == _selStep, playing);
    }

    _oled.setFont(u8g2_font_5x7_tr);
    char buf[30];
    const Pattern& p = pat();
    snprintf(buf, sizeof(buf), "PG%u/%u L%u %s%s",
             (unsigned)(page + 1), (unsigned)((p.length + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE),
             (unsigned)p.length, NOTE_NAMES[p.rootNote % 12], SCALES[p.scaleIdx].name);
    _oled.drawStr(0, 45, buf);

    const Step& st = pat().steps[_selStep];
    char nb[8]; noteName(st.notes[0], nb);
    snprintf(buf, sizeof(buf), "S%02u %s v%u", (unsigned)(_selStep + 1), nb, (unsigned)st.velocity);
    int w = _oled.getStrWidth(buf);
    _oled.drawStr(DISP_W - w, 45, buf);

    drawFooter(_shift ? "SH:BPM/PAT/REC" : "PAGE=MENU  PUSH=EDIT");
}

void UI::drawMenu() {
    _oled.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= (int)MenuItem::COUNT) break;
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, MENU_LABELS[idx], nullptr);
    }
    drawFooter("PUSH=OPEN  PAGE=BACK");
}

void UI::drawStepEdit() {
    _oled.setFont(u8g2_font_6x10_tr);
    Step& st = curStep();
    char val[16];

    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= (int)StepField::COUNT) break;
        val[0] = 0;
        switch ((StepField)idx) {
        case StepField::ACTIVE:  snprintf(val, sizeof(val), "%s", st.active() ? "ON" : "OFF"); break;
        case StepField::NOTE1: case StepField::NOTE2:
        case StepField::NOTE3: case StepField::NOTE4: {
            uint8_t n = (uint8_t)idx - (uint8_t)StepField::NOTE1;
            if (n < st.noteCount) noteName(st.notes[n], val);
            else                  snprintf(val, sizeof(val), "--");
            break;
        }
        case StepField::NOTECOUNT: snprintf(val, sizeof(val), "%u", (unsigned)st.noteCount); break;
        case StepField::VELOCITY:  snprintf(val, sizeof(val), "%u", (unsigned)st.velocity); break;
        case StepField::GATE:      snprintf(val, sizeof(val), "%u%%", (unsigned)st.gate); break;
        case StepField::PROB:      snprintf(val, sizeof(val), "%u%%", (unsigned)st.probability); break;
        case StepField::MICRO:     snprintf(val, sizeof(val), "%+d", (int)st.micro); break;
        case StepField::RATCHET:   snprintf(val, sizeof(val), "x%u", (unsigned)st.ratchet); break;
        case StepField::ACCENT:    snprintf(val, sizeof(val), "%s", st.accent() ? "ON" : "OFF"); break;
        case StepField::SLIDE:     snprintf(val, sizeof(val), "%s", st.slide() ? "ON" : "OFF"); break;
        case StepField::TIE:       snprintf(val, sizeof(val), "%s", st.tie() ? "ON" : "OFF"); break;
        case StepField::LOCKS: {
            uint8_t used = 0;
            for (uint8_t k = 0; k < STEP_LOCKS; k++) if (st.lockId[k] != LOCK_NONE) used++;
            snprintf(val, sizeof(val), "%u/%u", (unsigned)used, (unsigned)STEP_LOCKS);
            break;
        }
        default: break;
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, STEP_FIELD_LABELS[idx], val);
    }

    char hint[26];
    snprintf(hint, sizeof(hint), "STEP %02u  TRK=NEXT", (unsigned)(_selStep + 1));
    drawFooter(hint);
}

void UI::drawStepLocks() {
    _oled.setFont(u8g2_font_6x10_tr);
    Step& st = curStep();
    char lbl[16], val[16];

    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= rowCount()) break;
        if (idx == STEP_LOCKS * 2) {
            drawListRow(CONTENT_Y + i * ROW_H, idx == _row, "CLEAR ALL", nullptr);
            continue;
        }
        uint8_t slot = (uint8_t)(idx / 2);
        if ((idx & 1) == 0) {
            snprintf(lbl, sizeof(lbl), "L%u Param", (unsigned)(slot + 1));
            if (st.lockId[slot] == LOCK_NONE) snprintf(val, sizeof(val), "--");
            else snprintf(val, sizeof(val), "%s", engineParamInfo(st.lockId[slot])->name);
        } else {
            snprintf(lbl, sizeof(lbl), "L%u Value", (unsigned)(slot + 1));
            if (st.lockId[slot] == LOCK_NONE) snprintf(val, sizeof(val), "--");
            else snprintf(val, sizeof(val), "%u", (unsigned)(st.lockVal[slot] * 100 / 255));
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl, val);
    }
    drawFooter("PARAM LOCKS");
}

void UI::drawPatternSel() {
    _oled.setFont(u8g2_font_6x10_tr);
    char lbl[16], val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_PATTERNS) break;
        Pattern& p = seq().getPattern((uint8_t)idx);
        uint8_t used = 0;
        for (uint8_t s = 0; s < p.length; s++) if (p.steps[s].active()) used++;
        snprintf(lbl, sizeof(lbl), "PATTERN %d%s", idx + 1,
                 (idx == seq().currentPattern()) ? " *" : "");
        snprintf(val, sizeof(val), "%u/%u", (unsigned)used, (unsigned)p.length);
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl, val);
    }
    drawFooter("PUSH=SELECT");
}

void UI::drawPatternOpts() {
    _oled.setFont(u8g2_font_6x10_tr);
    Pattern& p = pat();
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_PATOPT_ROWS) break;
        switch (idx) {
        case 0: snprintf(val, sizeof(val), "%u", (unsigned)p.length); break;
        case 1: snprintf(val, sizeof(val), "%u%%", (unsigned)p.swing); break;
        case 2: snprintf(val, sizeof(val), "%s", SUBDIVS[p.subdiv % NUM_SUBDIVS].name); break;
        case 3: snprintf(val, sizeof(val), "%s", SCALES[p.scaleIdx].name); break;
        case 4: snprintf(val, sizeof(val), "%s", NOTE_NAMES[p.rootNote % 12]); break;
        case 5: snprintf(val, sizeof(val), "%+d", (int)p.transpose); break;
        case 6: snprintf(val, sizeof(val), "%u", (unsigned)p.midiChannel); break;
        case 7: snprintf(val, sizeof(val), "%s",
                         (p.presetIdx == 0xFF) ? "--" : enginePresetName(p.presetIdx % engineNumPresets())); break;
        case 8: snprintf(val, sizeof(val), "%s", TIME_SIGS[p.timeSigIdx % NUM_TIME_SIGS].name); break;
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, PATOPT_LABELS[idx], val);
    }
    drawFooter("PATTERN OPTIONS");
}

void UI::drawChain() {
    _oled.setFont(u8g2_font_6x10_tr);
    char lbl[16], val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx > CHAIN_LEN + 2) break;
        if (idx == CHAIN_LEN + 2) {
            drawListRow(CONTENT_Y + i * ROW_H, idx == _row, "SAVE SONG", nullptr);
            continue;
        }
        if (idx == CHAIN_LEN + 1) {
            drawListRow(CONTENT_Y + i * ROW_H, idx == _row, "LOAD SONG", nullptr);
            continue;
        }
        if (idx == CHAIN_LEN) {
            drawListRow(CONTENT_Y + i * ROW_H, idx == _row,
                        seq().isChaining() ? "APPLY / OFF" : "APPLY", nullptr);
            continue;
        }
        ChainEntry& c = _gs->chain[*_track][idx];
        snprintf(lbl, sizeof(lbl), "SLOT %d", idx + 1);
        if (c.patternIdx < 0) snprintf(val, sizeof(val), "--");
        else snprintf(val, sizeof(val), "P%d x%u", c.patternIdx + 1, (unsigned)c.repeats);
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl, val);
    }
    drawFooter("SH+TURN = REPEATS");
}

void UI::drawTools() {
    _oled.setFont(u8g2_font_6x10_tr);
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_TOOL_ROWS) break;
        val[0] = 0;
        switch (idx) {
        case 0: snprintf(val, sizeof(val), "%u", (unsigned)_eucHits); break;
        case 1: snprintf(val, sizeof(val), "%u", (unsigned)_eucRot); break;
        case 3: snprintf(val, sizeof(val), "%u%%", (unsigned)_randDensity); break;
        case 5: snprintf(val, sizeof(val), "%u", (unsigned)_humanAmt); break;
        case 7: snprintf(val, sizeof(val), "%s", RIFFS[_riffSel % NUM_RIFFS].name); break;
        default: break;
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, TOOL_LABELS[idx],
                    val[0] ? val : nullptr);
    }
    if (_row == 7) {
        char hint[32];
        snprintf(hint, sizeof(hint), "CAT:%s SH+TURN=CHG", RIFF_CATEGORY_NAMES[_riffCatFilter]);
        drawFooter(hint);
    } else {
        drawFooter("PATTERN TOOLS");
    }
}

void UI::drawEngineSel() {
    _oled.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_ENGINES) break;
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, ENGINE_NAMES[idx],
                    ENGINE_IS_STUB[idx] ? "(SOON)" : ((idx == currentEngine) ? "*" : nullptr));
    }
    char hint[26];
    snprintf(hint, sizeof(hint), "%s", ENGINE_MAKER[_row]);
    drawFooter(hint);
}

void UI::drawSynthPages() {
    _oled.setFont(u8g2_font_6x10_tr);
    uint8_t numPages = engineNumPages();
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= numPages) break;
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, enginePage((uint8_t)idx)->name, nullptr);
    }
    char hint[26];
    uint8_t numPresets = engineNumPresets();
    snprintf(hint, sizeof(hint), "%s", (g_currentPreset >= 0 && g_currentPreset < numPresets)
             ? enginePresetName((uint8_t)g_currentPreset) : "USER PATCH");
    drawFooter(hint);
}

void UI::drawSynthEdit() {
    _oled.setFont(u8g2_font_6x10_tr);
    const EnginePageDef& pg = *enginePage(_synthPage);
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= pg.count) break;
        uint8_t id = pg.first + (uint8_t)idx;
        engineFormatParameter(id, val, sizeof(val));

        int y = CONTENT_Y + i * ROW_H;
        bool sel = (idx == _row);
        if (sel) { _oled.drawBox(0, y, DISP_W, ROW_H - 1); _oled.setDrawColor(0); }
        const ParamInfo& pi = *engineParamInfo(id);
        _oled.drawStr(2, y + 8, pi.name);

        int w = _oled.getStrWidth(val);
        _oled.drawStr(DISP_W - 3 - w, y + 8, val);
        // Continuous parameters also get a bar between the label and the value,
        // so a sweep is readable without parsing the number.
        if (pi.kind == PK_CONT || pi.kind == PK_HZ || pi.kind == PK_MS ||
            pi.kind == PK_BIPOLAR || pi.kind == PK_CENTS) {
            int barX = 60, barW = DISP_W - 8 - w - barX;
            if (barW > 8) drawBar(barX, y + 2, barW, engineGetParameter(id));
        }
        if (sel) _oled.setDrawColor(1);
    }
    drawFooter(pg.name);
}

void UI::drawPresets() {
    _oled.setFont(u8g2_font_6x10_tr);
    char lbl[22];
    uint8_t numPresets = engineNumPresets();
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= rowCount()) break;
        if (idx < numPresets)
            snprintf(lbl, sizeof(lbl), "F%02d %s", idx + 1, enginePresetName((uint8_t)idx));
        else
            snprintf(lbl, sizeof(lbl), "U%02d USER SLOT", idx - numPresets + 1);
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl,
                    (idx == g_currentPreset) ? "*" : nullptr);
    }
    drawFooter("PUSH=LOAD SH+PUSH=SAVE");
}

void UI::drawPresetSave() {
    _oled.setFont(u8g2_font_6x10_tr);
    char val[16];
    for (int i = 0; i < 3; i++) {
        const char* lbl = (i == 0) ? "Slot" : (i == 1) ? "Name" : "SAVE";
        val[0] = 0;
        if (i == 0) snprintf(val, sizeof(val), "U%02u", (unsigned)(_saveSlot + 1));
        else if (i == 1) snprintf(val, sizeof(val), "%s", _nameBuf);
        drawListRow(CONTENT_Y + i * ROW_H, i == _row, lbl, val[0] ? val : nullptr);
    }
    // Caret under the character being edited.
    if (_row == 1 && _editing) {
        int w = _oled.getStrWidth(_nameBuf);
        int x = DISP_W - 3 - w + _nameCursor * 6;
        _oled.drawHLine(x, CONTENT_Y + ROW_H + 9, 5);
    }
    drawFooter("SH+TURN = MOVE CARET");
}

void UI::drawMixer() {
    _oled.setFont(u8g2_font_6x10_tr);
    char lbl[18], val[16];
    for (int i = 0; i < VIS_ROWS && i < NUM_TRACKS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_TRACKS) break;
        Sequencer& s = _seqs[idx];
        snprintf(lbl, sizeof(lbl), "T%d %s%s", idx + 1,
                 (idx == INTERNAL_TRACK) ? "SYNTH" : "MIDI ",
                 s.muted() ? " M" : "");
        snprintf(val, sizeof(val), "ch%u P%u",
                 (unsigned)s.currentPatternRef().midiChannel,
                 (unsigned)(s.currentPattern() + 1));
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl, val);
    }
    drawFooter("MUTE BTN = MUTE ROW");
}

void UI::drawMidiSet() {
    _oled.setFont(u8g2_font_6x10_tr);
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_MIDI_ROWS) break;
        val[0] = 0;
        switch (idx) {
        case 0: if (_gs->midiInChannel == 0) snprintf(val, sizeof(val), "OMNI");
                else snprintf(val, sizeof(val), "%u", (unsigned)_gs->midiInChannel); break;
        case 1: snprintf(val, sizeof(val), "%s", _gs->midiOutEnable ? "ON" : "OFF"); break;
        case 2: snprintf(val, sizeof(val), "%s", _gs->midiClockOut ? "ON" : "OFF"); break;
        case 3: snprintf(val, sizeof(val), "%s", _gs->extSync ? "ON" : "OFF"); break;
        case 4: snprintf(val, sizeof(val), "%s", _gs->midiThru ? "ON" : "OFF"); break;
        case 5: snprintf(val, sizeof(val), "%s", _gs->midiKbdPlay ? "ON" : "OFF"); break;
        default: break;
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, MIDI_LABELS[idx],
                    val[0] ? val : nullptr);
    }
    drawFooter("OUT=GP1  IN=GP0");
}

void UI::drawSettings() {
    _oled.setFont(u8g2_font_6x10_tr);
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_SETTING_ROWS) break;
        val[0] = 0;
        switch (idx) {
        case 0: snprintf(val, sizeof(val), "%d%%", (int)(engineGetParameter(engineMasterParamId()) * 100.0f)); break;
        case 1: snprintf(val, sizeof(val), "%u", (unsigned)_gs->contrast); break;
        case 2: snprintf(val, sizeof(val), "%s", _gs->metronome ? "ON" : "OFF"); break;
        case 3: snprintf(val, sizeof(val), "%s", _store->mounted() ? "" : "NO FS"); break;
        case 4: snprintf(val, sizeof(val), "%s", _shift ? "READY" : "+SHIFT"); break;
        case 5: snprintf(val, sizeof(val), "%s", TIME_SIGS[_gs->defaultTimeSigIdx % NUM_TIME_SIGS].name); break;
        case 6: snprintf(val, sizeof(val), "%s", g_diskMode ? "MOUNTED" : ""); break;
        default: break;
        }
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, SETTING_LABELS[idx],
                    val[0] ? val : nullptr);
    }
    drawFooter(_store->mounted() ? "PAGE LONG = SAVE ALL" : "NO FILESYSTEM MOUNTED");
}

// Static message screen, deliberately with no drawFooter() band -- every
// line here fits above FOOTER_Y (53) on purpose, and what PAGE does is
// already stated in the body text itself. This is also the one screen an
// explicit user action (SETTINGS -> USB DRIVE) parked the transport and
// silenced the engine for, so unlike everywhere else, nothing behind these
// four lines is moving for it to collide with.
void UI::drawUsbMode() {
    _oled.setFont(u8g2_font_6x10_tr);
    const char* title = "USB DRIVE";
    _oled.drawStr((DISP_W - _oled.getStrWidth(title)) / 2, 20, title);

    _oled.setFont(u8g2_font_5x7_tr);
    const char* status = g_diskMode ? "mounted on the host" : "waiting for host...";
    _oled.drawStr((DISP_W - _oled.getStrWidth(status)) / 2, 29, status);

    const char* l1 = "drop .syx in /banks,";
    const char* l2 = "eject, then PAGE";
    _oled.drawStr((DISP_W - _oled.getStrWidth(l1)) / 2, 38, l1);
    _oled.drawStr((DISP_W - _oled.getStrWidth(l2)) / 2, 46, l2);
}

void UI::drawPerf() {
    _oled.setFont(u8g2_font_6x10_tr);
    char buf[26];
    int y = CONTENT_Y + 8;

    snprintf(buf, sizeof(buf), "CPU  %3d%%", (int)(g_cpuLoad * 100.0f));
    _oled.drawStr(2, y, buf);
    drawBar(66, y - 7, 58, g_cpuLoad);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "VOICES %d/%d", g_activeVoices, MAX_VOICES);
    _oled.drawStr(2, y, buf);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "RAM  %u KB", (unsigned)(rp2040.getFreeHeap() / 1024));
    _oled.drawStr(2, y, buf);
    y += ROW_H;

    snprintf(buf, sizeof(buf), "CLK  %u MHz", (unsigned)(F_CPU / 1000000));
    _oled.drawStr(2, y, buf);

    drawFooter("PERFORMANCE");
}

void UI::drawHelp() {
    _oled.setFont(u8g2_font_5x7_tr);
    // Six lines fit in the content area at 7px/line (42px between the header
    // and footer), more than the 4-row list budget the other screens use, so
    // this scrolls on its own 6-row window rather than VIS_ROWS.
    int n = NUM_HELP_LINES;
    if (_scroll > n - 6) _scroll = max(0, n - 6);
    for (int i = 0; i < 6; i++) {
        int idx = _scroll + i;
        if (idx >= n) break;
        _oled.drawStr(1, CONTENT_Y + 7 + i * 7, HELP_LINES[idx]);
    }
    drawFooter("HELP");
}

void UI::drawSongBrowse() {
    _oled.setFont(u8g2_font_6x10_tr);
    char lbl[16], val[16];
    Song s;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= SONG_SLOTS) break;
        snprintf(lbl, sizeof(lbl), "SONG %03d", idx + 1);
        // Only the visible rows ever touch the filesystem -- VIS_ROWS opens
        // per redraw, and redraws only happen on navigation (see update()),
        // so this stays cheap even with 200 slots on offer.
        if (_store->loadSong((uint8_t)idx, s) && s.name[0])
            snprintf(val, sizeof(val), "%s", s.name);
        else
            snprintf(val, sizeof(val), "--");
        drawListRow(CONTENT_Y + i * ROW_H, idx == _row, lbl, val);
    }
    drawFooter(_songSaving ? "PUSH=SAVE SH+PUSH=DEL" : "PUSH=LOAD SH+PUSH=DEL");
}

void UI::drawSongSaveName() {
    _oled.setFont(u8g2_font_6x10_tr);
    char val[16];
    for (int i = 0; i < 2; i++) {
        const char* lbl = (i == 0) ? "Name" : "SAVE";
        val[0] = 0;
        if (i == 0) snprintf(val, sizeof(val), "%s", _nameBuf);
        drawListRow(CONTENT_Y + i * ROW_H, i == _row, lbl, val[0] ? val : nullptr);
    }
    if (_row == 0 && _editing) {
        int w = _oled.getStrWidth(_nameBuf);
        int x = DISP_W - 3 - w + _nameCursor * 6;
        _oled.drawHLine(x, CONTENT_Y + 9, 5);
    }
    char hint[32];
    snprintf(hint, sizeof(hint), "SLOT %03u SH+TURN=CARET", (unsigned)(_songSlot + 1));
    drawFooter(hint);
}

void UI::drawAll() {
    _oled.clearBuffer();
    drawHeader();
    switch (_screen) {
    case Screen::MAIN:         drawMain();        break;
    case Screen::MENU:         drawMenu();        break;
    case Screen::STEP_EDIT:    drawStepEdit();    break;
    case Screen::PLOCK_EDIT:   drawStepLocks();   break;
    case Screen::PATTERN_SEL:  drawPatternSel();  break;
    case Screen::PATTERN_OPTS: drawPatternOpts(); break;
    case Screen::CHAIN_EDIT:   drawChain();       break;
    case Screen::TOOLS:        drawTools();       break;
    case Screen::ENGINE_SEL:   drawEngineSel();   break;
    case Screen::SYNTH_PAGES:  drawSynthPages();  break;
    case Screen::SYNTH_EDIT:   drawSynthEdit();   break;
    case Screen::PRESETS:      drawPresets();     break;
    case Screen::PRESET_SAVE:  drawPresetSave();  break;
    case Screen::MIXER:        drawMixer();       break;
    case Screen::MIDI_SET:     drawMidiSet();     break;
    case Screen::SETTINGS:     drawSettings();    break;
    case Screen::USB_MODE:     drawUsbMode();     break;
    case Screen::PERF:         drawPerf();        break;
    case Screen::HELP:         drawHelp();        break;
    case Screen::SONG_BROWSE:    drawSongBrowse();    break;
    case Screen::SONG_SAVE_NAME: drawSongSaveName();  break;
    }
    _oled.sendBuffer();
}

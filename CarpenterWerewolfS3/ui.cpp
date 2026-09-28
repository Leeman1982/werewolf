#include "ui.h"
#include "panel.h"
#include "arps.h"
#include "splash_wordmark.h"
#include "midi.h"
#include "dsp_tables.h"
#include "scales.h"
#include <stdio.h>
#include <string.h>

using namespace gfx;

// ─── Hooks into the sketch ──────────────────────────────────────────────────
extern MidiIO  midi;
extern volatile float g_cpuLoad;        // 0..1, measured on the audio core
extern volatile int   g_activeVoices;
extern volatile float g_peakL, g_peakR;
extern volatile uint32_t g_xruns;       // audio blocks that missed their deadline
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
bool uiUsbAvailable();
void uiBanksMayHaveChanged();
int  uiKbdOctave(int delta);      // OCT DOWN/UP transpose; returns the new shift

// ─── Layout (320x240) ───────────────────────────────────────────────────────
// Same frame on every page, NightShift style: a lit top bar, a title box,
// the content band, and (touch build) a soft-key strip where the manual's
// hint footer sat.
#define TOP_H        18
#define TITLE_Y      21
#define TITLE_H      21
#define CONTENT_Y    45
#define ROW_H        27
#define VIS_ROWS      6
#define LIST_X        4
#define LIST_R      308          // right edge of values (scrollbar lives past it)
#define SK_Y        212
#define SK_H         28
#define SK_W         40

// Main grid geometry.
#define CELL_X0       2
#define CELL_W       37
#define CELL_GAP      3
#define CELL_H       57
#define CELL_Y1      64
#define CELL_Y2     (CELL_Y1 + CELL_H + 4)

// Touch tuning.
#define DRAG_START_PX   10       // movement before a touch counts as a drag
#define DRAG_DETENT_PX  14       // pixels of drag per encoder detent
#define KEY_REPEAT_MS  350       // arrow-key auto-repeat delay...
#define KEY_RATE_MS     70       // ...and rate

static const uint8_t* const FONT_KEY = u8g2_font_6x13B_tr;

static const char* const MENU_LABELS[] = {
    "STEP EDIT", "PATTERN", "PAT OPTS", "CHAIN", "TOOLS",
    "ENGINE", "SYNTH", "PRESETS", "MIXER", "MIDI", "SETTINGS", "PERF", "HELP"
};

static const char* const STEP_FIELD_LABELS[] = {
    "ACTIVE", "NOTE 1", "NOTE 2", "NOTE 3", "NOTE 4", "NOTES",
    "VELOCITY", "GATE", "PROBAB.", "MICRO", "RATCHET",
    "ACCENT", "SLIDE", "TIE", "P-LOCKS"
};

static const char* const TOOL_LABELS[] = {
    "EUCLID HITS", "EUCLID ROT", "APPLY EUCLID",
    "RAND DENS", "RANDOMISE", "HUMAN AMT", "HUMANISE",
    "RIFF", "LOAD RIFF", "CLEAR PATTERN", "COPY>CLIP", "PASTE CLIP"
};
#define NUM_TOOL_ROWS 12

// First riff in RIFFS[] tagged with `cat` (RIFF_CAT_ALL = no filter, index 0).
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
    "IN CHAN", "OUT ENABLE", "CLOCK OUT", "EXT SYNC", "SOFT THRU", "KBD PLAY", "KBD OCTAVE", "PANIC"
};
#define NUM_MIDI_ROWS 8
#define MIDI_ROW_PANIC 7

static const char* const SETTING_LABELS[] = {
    "MASTER VOL", "BACKLIGHT", "METRONOME", "CRT LINES", "PHOSPHOR", "TOUCH CAL",
    "SAVE ALL", "FORMAT FS", "DEF TIME SIG", "USB DRIVE"
};
#define NUM_SETTING_ROWS ((int)SetRow::COUNT)

static const char* const PHOSPHOR_NAMES[NUM_PHOSPHORS] = { "GREEN", "AMBER", "BLOOD" };

static const char* const PATOPT_LABELS[] = {
    "LENGTH", "SWING", "SUBDIV", "SCALE", "ROOT", "TRANSPOSE", "MIDI CHAN", "PRESET",
    "TIME SIG"
};
#define NUM_PATOPT_ROWS 9

static const char* const HELP_LINES[] = {
    "TAP a row or step: select it",
    "TAP it again: PUSH (open / edit)",
    "DRAG up/down: turn the encoder",
    "HOLD a step: switch it on/off",
    "PUSH held: back",
    "PLAY: start-stop  held: step",
    "PAGE: menu-back  held: SAVE",
    "TRACK: next track  held: copy",
    "MUTE: step on/off  held: paste",
    "SHIFT latches: +PLAY rec arm,",
    " +MUTE mute track, +TRACK next",
    " pattern, +drag BPM / fast",
    "Tap the title box: back",
};
#define NUM_HELP_LINES 13

// ─── Phosphor palettes ──────────────────────────────────────────────────────
// Slot order is gfx::Col. GREEN is sampled straight off the NightShift
// manual's screens; AMBER and BLOOD keep its structure (a primary, a
// contrasting cursor colour, red for danger) on warmer glass.
static const uint32_t PALETTES[NUM_PHOSPHORS][NUM_COLS] = {
    // BG       PANEL     HILITE    GRID      DIM       MUTED     TEXT      LIME
    // LIME_D   BAR       BAR_D     AMBER     AMBER_D   RED       RED_D     FOOT
    { 0x080C08, 0x101C10, 0x182818, 0x313C31, 0x62715A, 0x83997B, 0xCDE2B4, 0xACF26A,
      0x83B652, 0x527141, 0x2B3A25, 0xE6AE5A, 0xAC854A, 0xD53829, 0xA42C20, 0x101408 },
    { 0x0C0806, 0x1C140C, 0x2A1E10, 0x3E3222, 0x7A6448, 0x9E8664, 0xF2DDB6, 0xFFB347,
      0xC08634, 0x7A5A30, 0x3E2E1A, 0xB8F07A, 0x86B252, 0xE0402E, 0xA42C20, 0x140E08 },
    { 0x0C0606, 0x1C0E0E, 0x2A1414, 0x402424, 0x7A4A48, 0x9E7270, 0xF0D0CC, 0xFF4A3A,
      0xC0362A, 0x7A2A26, 0x3E1818, 0xE6AE5A, 0xAC854A, 0xFFFFFF, 0xB0B0B0, 0x140808 },
};

// ─── Small drawing vocabulary ───────────────────────────────────────────────
static inline bool blinkOn() { return ((millis() / 350) & 1) == 0; }

// Everything on this panel is set in capitals, as on the reference; engine
// parameter and preset names are mixed case in their own tables.
static const char* upper(const char* s) {
    static char buf[48];
    size_t i = 0;
    for (; s[i] && i + 1 < sizeof(buf); i++) {
        char c = s[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c;
    }
    buf[i] = 0;
    return buf;
}

// Double frame: the cursor.
static void cursorFrame(int x, int y, int w, int h, uint8_t c) {
    canvas.frame(x, y, w, h, c);
    canvas.frame(x + 1, y + 1, w - 2, h - 2, c);
}

// Right-pointing chevron, the list's selection marker.
static void chevron(int x, int y, uint8_t c) {
    for (int i = 0; i < 5; i++) { canvas.pixel(x + i, y + i, c); canvas.pixel(x + i, y + 8 - i, c);
                                  canvas.pixel(x + i + 1, y + i, c); canvas.pixel(x + i + 1, y + 8 - i, c); }
}

static void triRight(int x, int y, int h, uint8_t c) {   // filled, h tall
    for (int i = 0; i <= h / 2; i++) canvas.vline(x + i, y + i, h - 2 * i, c);
}
static void triLeft(int x, int y, int h, uint8_t c) {
    for (int i = 0; i <= h / 2; i++) canvas.vline(x + h / 2 - i, y + i, h - 2 * i, c);
}

// The composer half of ENGINE_MAKER ("Moog (Carpenter, Simonetti)" ->
// "CARPENTER, SIMONETTI"), upper-cased: on this instrument the credit is the
// half worth reading.
static void composerOf(uint8_t e, char* out, size_t n) {
    const char* m = ENGINE_MAKER[e];
    const char* o = strchr(m, '(');
    const char* src = o ? o + 1 : m;
    size_t k = 0;
    for (; *src && *src != ')' && k + 1 < n; src++) {
        char ch = *src;
        if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 'a' + 'A');
        out[k++] = ch;
    }
    out[k] = 0;
}

// ─── Init ───────────────────────────────────────────────────────────────────
void UI::beginDisplay() {
    panelBegin();
    canvas.begin();
    applyLook(PHOS_GREEN, true);
    bootStatus("BOOTING");
}

void UI::applyLook(uint8_t phosphor, bool crt) {
    if (phosphor >= NUM_PHOSPHORS) phosphor = PHOS_GREEN;
    for (uint8_t i = 0; i < NUM_COLS; i++) canvas.setPalette(i, PALETTES[phosphor][i]);
    canvas.setCrt(crt);
    _dirty = true;
}

// ─── Boot title card ────────────────────────────────────────────────────────
// The NightShift title card, retitled: the wordmark is set in Grobe
// Deutschmeister (splash_wordmark.h, rasterized by tools/ttf_to_xbm.py from
// extras/font/GrobeDeutschmeister.ttf), inside a double frame at the end of
// a receding corridor. The heartbeat trace along the bottom grows one beat
// per boot stage, and the status line names the stage -- if start-up ever
// stops, the screen says where.
#define BOOT_STAGES 9

void UI::bootStatus(const char* msg) {
    if (_bootStage < 250) _bootStage++;
    canvas.clear(BG);

    canvas.setFont(FONT_BODY);
    canvas.text(8, 14, "ZOMBI SS / WEREWOLF S3", MUTED);
    canvas.textRight(312, 14, "17 ENGINES", MUTED);
    canvas.hline(0, 19, W, GRID);

    // Corridor: four lines from the corners into a doorway behind the title.
    const int dx0 = 142, dx1 = 178, dy0 = 64, dy1 = 176;
    canvas.line(0, 24, dx0, dy0, GRID);   canvas.line(319, 24, dx1, dy0, GRID);
    canvas.line(0, 204, dx0, dy1, GRID);  canvas.line(319, 204, dx1, dy1, GRID);
    canvas.frame(dx0, dy0, dx1 - dx0, dy1 - dy0, GRID);
    canvas.fill(dx0 + 1, dy0 + 1, dx1 - dx0 - 2, dy1 - dy0 - 2, PANEL);
    // Something in the doorway: two red eyes, lit once the audio is up.
    if (_bootStage >= 7) {
        canvas.fill(155, 158, 2, 1, RED);
        canvas.fill(163, 158, 2, 1, RED);
    }

    // BOOT SEQUENCE chip.
    canvas.fill(8, 28, 106, 17, BAR);
    canvas.setFont(FONT_BODY);
    canvas.text(13, 41, "BOOT SEQUENCE", TEXT);

    // Title box: double frame, wordmark with a drop shadow, tagline.
    const int bx = 12, by = 78, bw = 296, bh = 78;
    canvas.fill(bx, by, bw, bh, BG);
    canvas.frame(bx, by, bw, bh, GRID);
    canvas.frame(bx + 3, by + 3, bw - 6, bh - 6, GRID);
    int wx = (W - WOLF_TITLE_W) / 2, wy = by + 7;
    canvas.xbm(wx + 2, wy + 2, WOLF_TITLE_W, WOLF_TITLE_H, WOLF_TITLE_BITS, BAR_D);
    canvas.xbm(wx, wy, WOLF_TITLE_W, WOLF_TITLE_H, WOLF_TITLE_BITS, LIME);
    canvas.hline(bx + 20, by + bh - 22, bw - 40, GRID);
    canvas.setFont(FONT_BODY);
    canvas.textCenter(W / 2, by + bh - 8, "SYNTH / STEP SEQUENCER", LIME);

    // Heartbeat: a flat trace with one blip per completed stage.
    const int hy = 188;
    canvas.hline(8, hy, 304, BAR);
    int beats = _bootStage > BOOT_STAGES ? BOOT_STAGES : _bootStage;
    for (int b = 0; b < beats; b++) {
        int x = 20 + b * 32;
        canvas.line(x, hy, x + 3, hy - 9, LIME);
        canvas.line(x + 3, hy - 9, x + 6, hy + 6, LIME);
        canvas.line(x + 6, hy + 6, x + 9, hy, LIME);
    }
    for (int x = 8; x < 312; x += 38) canvas.vline(x, hy - 2, 5, BAR);

    // Status line.
    canvas.setFont(FONT_BOLD);
    char st[28];
    snprintf(st, sizeof(st), "%s_", (msg && *msg) ? msg : "");
    int ex = canvas.text(8, 213, st, LIME);
    if (blinkOn()) canvas.fill(ex + 2, 203, 7, 12, LIME);
    char stage[12];
    snprintf(stage, sizeof(stage), "%02u/%02u", (unsigned)(beats), (unsigned)BOOT_STAGES);
    canvas.setFont(FONT_BODY);
    canvas.textRight(312, 213, stage, MUTED);

    // Byline, also in the title face.
    canvas.xbm((W - WOLF_BYLINE_W) / 2, 222, WOLF_BYLINE_W, WOLF_BYLINE_H, WOLF_BYLINE_BITS, LIME);

    canvas.flush(panelSink());
}

void UI::bind(Sequencer* seqs, uint8_t* activeTrack, Storage* storage, GlobalSettings* gs) {
    _seqs = seqs; _track = activeTrack; _store = storage; _gs = gs;
    _dirty = true;
}

void UI::applyContrast(uint8_t c) {
    // Never let a restored setting black the panel out — that is
    // indistinguishable from a dead board.
    if (c < 24) c = 24;
    panelBacklight(c);
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

void UI::consumeShift() {
    if (_shiftLatch) { _shiftLatch = false; _dirty = true; }
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

// Screens drawn as the standard 6-row list (and hit-tested as one).
bool UI::isListScreen() const {
    switch (_screen) {
    case Screen::STEP_EDIT: case Screen::PLOCK_EDIT: case Screen::PATTERN_OPTS:
    case Screen::TOOLS: case Screen::ENGINE_SEL: case Screen::SYNTH_PAGES:
    case Screen::SYNTH_EDIT: case Screen::PRESETS: case Screen::PRESET_SAVE:
    case Screen::MIDI_SET: case Screen::SETTINGS: case Screen::SONG_BROWSE:
    case Screen::SONG_SAVE_NAME:
        return true;
    default:
        return false;
    }
}

int UI::visRows() const {
    // Screens that are not the 6-row list lay out every row at once, so their
    // cursor never has to scroll.
    return isListScreen() ? VIS_ROWS : 64;
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
    ensureVisible(visRows());
}

// ─── Encoder ────────────────────────────────────────────────────────────────
void UI::onEncoder(int delta) {
    if (delta == 0) return;
    _dirty = true;
    // SHIFT multiplies the step so long ranges (BPM, note, cutoff) are reachable
    // without spinning the encoder for a minute.
    int fast = shift() ? 8 : 1;

    if (_screen == Screen::MAIN) {
        if (shift()) {
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
    if (_screen == Screen::HELP) {
        _scroll = (int16_t)constrain((int)_scroll + delta, 0, 0);
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
    consumeShift();
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
    consumeShift();
}

// ─── Buttons ────────────────────────────────────────────────────────────────
void UI::onPlay() {
    _dirty = true;
    if (shift()) {
        g_recArm = !g_recArm;
        for (uint8_t t = 0; t < NUM_TRACKS; t++) _seqs[t].setRecord(g_recArm && t == *_track);
        toast(g_recArm ? "REC ARMED" : "REC OFF");
        consumeShift();
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
    consumeShift();
}

void UI::onPage() {
    _dirty = true;
    consumeShift();
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
    consumeShift();
    _dirty = true;
}

void UI::onTrack() {
    _dirty = true;
    if (_screen == Screen::STEP_EDIT || _screen == Screen::PLOCK_EDIT) {
        // In the step editor the TRACK button steps through the pattern, which
        // is far quicker than backing out to the grid for every note.
        uint8_t len = pat().length ? pat().length : 1;
        _selStep = shift() ? (uint8_t)((_selStep + len - 1) % len)
                           : (uint8_t)((_selStep + 1) % len);
        consumeShift();
        return;
    }
    if (shift()) {   // SHIFT+TRACK on any other screen = next pattern
        uint8_t next = (uint8_t)((seq().currentPattern() + 1) % NUM_PATTERNS);
        seq().setPattern(next);
        _gs->lastPattern[*_track] = next;
        char b[20]; snprintf(b, sizeof(b), "PATTERN %u", (unsigned)(next + 1));
        toast(b);
        consumeShift();
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
    consumeShift();
    _dirty = true;
}

void UI::onMute() {
    _dirty = true;
    if (shift()) {
        seq().setMuted(!seq().muted());
        _gs->trackMute[*_track] = seq().muted() ? 1 : 0;
        toast(seq().muted() ? "TRACK MUTED" : "TRACK LIVE");
        consumeShift();
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
    consumeShift();
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
        if (shift()) {
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
        if (shift()) {                           // SHIFT+press = save to a slot
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
        if (_row == MIDI_ROW_PANIC) { uiPanic(); toast("ALL NOTES OFF"); }
        else _editing = !_editing;
        break;

    case Screen::SETTINGS:
        switch ((SetRow)_row) {
        case SetRow::SAVE_ALL:
            if (!_store->mounted()) { toast("NO FS - FORMAT 1ST"); break; }
            uiSaveAll();
            toast("SAVED");
            break;
        case SetRow::FORMAT_FS:
            // Destructive and slow (tens of seconds on an 8 MB partition), so
            // it needs SHIFT, and it draws before it blocks rather than
            // looking like a freeze.
            if (!shift()) { toast("SHIFT + PUSH TO FORMAT"); break; }
            _bootStage = 0;
            bootStatus("FORMATTING FS...");
            _store->formatAll();
            canvas.invalidate();
            toast(_store->mounted() ? "FS FORMATTED" : "FORMAT FAILED");
            // A format wipes /banks along with everything else -- rescan
            // rather than leave a DX7 voice pointing at a file that no
            // longer exists (see uiBanksMayHaveChanged() in the sketch).
            uiBanksMayHaveChanged();
            break;
        case SetRow::USB_DRIVE:
            // Exclusive: stops the transport and hands the filesystem to the
            // computer on the USB cable.
            if (!uiUsbAvailable())   { toast("NEEDS USB-OTG MODE"); break; }
            if (!_store->mounted())  { toast("NO FS - FORMAT 1ST"); break; }
            uiStopAll();
            uiPanic();
            uiUsbBegin();
            gotoScreen(Screen::USB_MODE);
            break;
        case SetRow::TOUCH_CAL:
            runTouchCalibration();
            toast("TOUCH CALIBRATED");
            break;
        default:
            _editing = !_editing;
            break;
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
        if (shift()) c.repeats = (uint8_t)constrain((int)c.repeats + (d > 0 ? 1 : -1), 1, 16);
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
            if (shift()) {
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
    case Screen::SONG_SAVE_NAME: {
        const int nameRow = (_screen == Screen::PRESET_SAVE) ? 1 : 0;
        if (_screen == Screen::PRESET_SAVE && _row == 0) {
            _saveSlot = (uint8_t)constrain((int)_saveSlot + (d > 0 ? 1 : -1), 0, USER_PRESET_SLOTS - 1);
        } else if (_row == nameRow) {
            // Name editor: turn picks the character, SHIFT+turn moves the caret.
            if (shift()) {
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
    }

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
        case 6: uiKbdOctave(d > 0 ? 1 : -1); break;   // same as the OCT buttons
        }
        break;

    case Screen::SETTINGS:
        switch ((SetRow)_row) {
        case SetRow::MASTER: { uint8_t mp = engineMasterParamId();
                  engineSetParameter(mp, constrain(engineGetParameter(mp) + d * 0.02f, 0.0f, 1.0f)); break; }
        case SetRow::BACKLIGHT:
            _gs->contrast = (uint8_t)constrain((int)_gs->contrast + d * 8, 24, 255);
            applyContrast(_gs->contrast); break;
        case SetRow::METRONOME: _gs->metronome = !_gs->metronome; _editing = false; break;
        case SetRow::CRT:
            _gs->crtLines = !_gs->crtLines; _editing = false;
            applyLook(_gs->phosphor, _gs->crtLines != 0); break;
        case SetRow::PHOSPHOR:
            _gs->phosphor = (uint8_t)((_gs->phosphor + (d > 0 ? 1 : NUM_PHOSPHORS - 1)) % NUM_PHOSPHORS);
            applyLook(_gs->phosphor, _gs->crtLines != 0); break;
        case SetRow::DEF_TIMESIG: {
            int v = (int)_gs->defaultTimeSigIdx + (d > 0 ? 1 : -1);
            if (v < 0) v = NUM_TIME_SIGS - 1;
            if (v >= NUM_TIME_SIGS) v = 0;
            _gs->defaultTimeSigIdx = (uint8_t)v;
            break;
        }
        default: break;
        }
        break;

    default: break;
    }
    _dirty = true;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Touch
// ═══════════════════════════════════════════════════════════════════════════
void UI::touch(bool down, int x, int y) {
    if (down && !_tDown)      touchDown(x, y);
    else if (down && _tDown)  touchHeld(x, y);
    else if (!down && _tDown) touchUp(_tX, _tY);
}

void UI::touchDown(int x, int y) {
    _tDown = true;
    _tX0 = _tX = (int16_t)x; _tY0 = _tY = (int16_t)y;
    _tT0 = millis();
    _tLongFired = false;
    _tDragging = false;
    _tDragAcc = 0;
    _tKey = SK_NONE;
    _tHitRow = -1;
    _dirty = true;

    if (y >= SK_Y) {
        _tKey = (uint8_t)constrain(x / SK_W, 0, SK_COUNT - 1);
        // Encoder-step keys act on contact, then auto-repeat while held.
        if (_tKey == SK_DOWN || _tKey == SK_UP) {
            onEncoder(_tKey == SK_UP ? 1 : -1);
            _tRepeatAt = _tT0 + KEY_REPEAT_MS;
        }
        return;
    }
    _tHitRow = (int16_t)hitContent(x, y);
}

void UI::touchHeld(int x, int y) {
    _tX = (int16_t)x; _tY = (int16_t)y;
    unsigned long now = millis();

    if (_tKey != SK_NONE) {
        if (_tKey == SK_DOWN || _tKey == SK_UP) {
            if (now >= _tRepeatAt) {
                onEncoder(_tKey == SK_UP ? 1 : -1);
                _tRepeatAt = now + KEY_RATE_MS;
            }
        } else if (!_tLongFired && (now - _tT0) >= LONG_PRESS_MS) {
            _tLongFired = true;
            softKeyLong(_tKey);
        }
        return;
    }

    // Content: a drag turns the encoder, a still hold is a long press.
    int dy = _tY0 - y;                    // finger up = positive = clockwise
    if (!_tDragging && (dy > DRAG_START_PX || dy < -DRAG_START_PX)) {
        _tDragging = true;
        _tDragAcc = 0;
        _tY0 = (int16_t)y;                // detents count from here
        dy = 0;
    }
    if (_tDragging) {
        dragDetents(_tY0 - y);
        return;
    }
    if (!_tLongFired && (now - _tT0) >= LONG_PRESS_MS && _tHitRow >= 0) {
        _tLongFired = true;
        if (_screen == Screen::MAIN) {    // hold a step: switch it on/off
            _selStep = (uint8_t)_tHitRow;
            onEncLong();
        }
    }
}

void UI::dragDetents(int dy) {
    int want = dy / DRAG_DETENT_PX;
    int d = want - _tDragAcc;
    if (d != 0) {
        _tDragAcc = (int16_t)want;
        // On the grid, a vertical drag with SHIFT is BPM; without, it walks
        // the cursor, same as the encoder.
        onEncoder(d);
    }
}

void UI::touchUp(int x, int y) {
    _tDown = false;
    _dirty = true;
    if (_tKey != SK_NONE) {
        uint8_t k = _tKey;
        _tKey = SK_NONE;
        if (k == SK_DOWN || k == SK_UP) return;    // already acted on contact
        if (!_tLongFired) softKeyShort(k);
        return;
    }
    if (_tDragging || _tLongFired) return;

    // Title box: back, the same as PAGE.
    if (y >= TITLE_Y && y < TITLE_Y + TITLE_H) { onPage(); return; }
    (void)x;
    if (_tHitRow >= 0) tapContent(_tHitRow);
}

void UI::softKeyShort(uint8_t k) {
    switch (k) {
    case SK_PAGE:  onPage();  break;
    case SK_PLAY:  onPlay();  break;
    case SK_SHIFT: _shiftLatch = !_shiftLatch; _dirty = true; break;
    case SK_TRACK: onTrack(); break;
    case SK_MUTE:  onMute();  break;
    case SK_PUSH:  onEncPress(); break;
    default: break;
    }
}

void UI::softKeyLong(uint8_t k) {
    switch (k) {
    case SK_PAGE:  onPageLong();  break;
    case SK_PLAY:  onPlayLong();  break;
    case SK_SHIFT: break;
    case SK_TRACK: onTrackLong(); break;
    case SK_MUTE:  onMuteLong();  break;
    case SK_PUSH:  onEncLong();   break;
    default: break;
    }
}

// Which row / step / card is under (x, y) on the current screen, or -1.
int UI::hitContent(int x, int y) const {
    if (y < CONTENT_Y || y >= SK_Y) return -1;
    switch (_screen) {
    case Screen::MAIN: {
        int r = (y >= CELL_Y1 && y < CELL_Y1 + CELL_H) ? 0
              : (y >= CELL_Y2 && y < CELL_Y2 + CELL_H) ? 1 : -1;
        if (r < 0) return -1;
        int c = (x - CELL_X0) / (CELL_W + CELL_GAP);
        if (c < 0 || c > 7) return -1;
        int page = _selStep / STEPS_PER_PAGE;
        int st = page * STEPS_PER_PAGE + r * 8 + c;
        return (st < NUM_STEPS) ? st : -1;
    }
    case Screen::MENU: {
        int r = (y - (CONTENT_Y + 2)) / 23;
        if (r < 0 || r > 6) return -1;
        int idx = (x < W / 2) ? r : 7 + r;
        return (idx < (int)MenuItem::COUNT) ? idx : -1;
    }
    case Screen::PATTERN_SEL: {
        int r = (y - CONTENT_Y) / 41, c = (x < W / 2) ? 0 : 1;
        int idx = r * 2 + c;
        return (r >= 0 && r < 4 && idx < NUM_PATTERNS) ? idx : -1;
    }
    case Screen::CHAIN_EDIT: {
        if (y >= 58 && y < 118) {
            int c = (x - 4) / 39;
            return (c >= 0 && c < CHAIN_LEN) ? c : -1;
        }
        if (y >= 132 && y < 158) {                  // APPLY / LOAD / SAVE
            int c = (x - 8) / 102;
            return (c >= 0 && c < 3) ? CHAIN_LEN + c : -1;
        }
        return -1;
    }
    case Screen::MIXER: {
        if (y > 150) return -1;
        int c = (x - 4) / 78;
        return (c >= 0 && c < NUM_TRACKS) ? c : -1;
    }
    default:
        break;
    }
    if (isListScreen()) {
        int slot = (y - CONTENT_Y) / ROW_H;
        if (slot < 0 || slot >= VIS_ROWS) return -1;
        int idx = _scroll + slot;
        return (idx < rowCount()) ? idx : -1;
    }
    return -1;
}

// A tap puts the cursor on what was touched; a tap on what is already under
// the cursor is PUSH. Menus and the CHAIN buttons act on the first tap --
// there is nothing to adjust on them, so a confirming second tap would only
// be in the way.
void UI::tapContent(int hit) {
    _dirty = true;
    if (_screen == Screen::MAIN) {
        if (hit == _selStep) onEncPress();
        else                 _selStep = (uint8_t)hit;
        return;
    }
    bool oneTap = (_screen == Screen::MENU) || (_screen == Screen::SYNTH_PAGES) ||
                  (_screen == Screen::CHAIN_EDIT && hit >= CHAIN_LEN);
    if (hit != _row && !oneTap) {
        _editing = false;
        _row = (int16_t)hit;
        ensureVisible(visRows());
        return;
    }
    _row = (int16_t)hit;
    // Two-state rows flip on a tap while editing, like turning the encoder
    // one click, instead of needing a drag.
    onEncPress();
}

// ═══════════════════════════════════════════════════════════════════════════
//  Touch calibration
// ═══════════════════════════════════════════════════════════════════════════
// Three targets, non-collinear, solve the affine raw->screen map exactly --
// which also absorbs whichever way the touch layer is rotated or mirrored
// against the TFT, so no per-module swap/invert flags are needed.
void UI::runTouchCalibration() {
    static const int TX[3] = { 30, 290, 160 };
    static const int TY[3] = { 30, 120, 210 };
    float rx[3], ry[3];

    for (int p = 0; p < 3; p++) {
        // Wait for any finger still down from the previous point.
        uint16_t a, b;
        while (touchRaw(a, b)) delay(10);

        canvas.clear(BG);
        canvas.setFont(FONT_BOLD);
        canvas.textCenter(W / 2, 60 + (p == 0 ? 60 : 0), "TOUCH CALIBRATION", LIME);
        canvas.setFont(FONT_BODY);
        char b2[32];
        snprintf(b2, sizeof(b2), "TAP THE CROSS  %d/3", p + 1);
        canvas.textCenter(W / 2, 78 + (p == 0 ? 60 : 0), b2, TEXT);
        canvas.hline(TX[p] - 10, TY[p], 21, AMBER);
        canvas.vline(TX[p], TY[p] - 10, 21, AMBER);
        canvas.frame(TX[p] - 4, TY[p] - 4, 9, 9, AMBER);
        canvas.flush(panelSink());

        // Average a steady press.
        uint32_t sx = 0, sy = 0; int n = 0;
        while (n < 24) {
            if (touchRaw(a, b)) { sx += a; sy += b; n++; }
            else if (n) { n = 0; sx = sy = 0; }       // lifted too soon: start over
            delay(8);
        }
        rx[p] = (float)sx / n; ry[p] = (float)sy / n;
    }

    // Solve [rx ry 1] * [a b c]^T = target, for x and for y (Cramer's rule).
    float det = rx[0] * (ry[1] - ry[2]) - ry[0] * (rx[1] - rx[2]) + (rx[1] * ry[2] - rx[2] * ry[1]);
    TouchCal c;
    if (det > -1e-3f && det < 1e-3f) {
        toast("CAL FAILED - RETRY");
        return;
    }
    auto solve = [&](const int* t, float& A, float& B, float& C) {
        A = (t[0] * (ry[1] - ry[2]) - ry[0] * (t[1] - t[2]) + (t[1] * ry[2] - t[2] * ry[1])) / det;
        B = (rx[0] * (t[1] - t[2]) - t[0] * (rx[1] - rx[2]) + (rx[1] * t[2] - rx[2] * t[1])) / det;
        C = (rx[0] * (ry[1] * t[2] - ry[2] * t[1]) - ry[0] * (rx[1] * t[2] - rx[2] * t[1])
             + t[0] * (rx[1] * ry[2] - rx[2] * ry[1])) / det;
    };
    solve(TX, c.ax, c.bx, c.cx);
    solve(TY, c.ay, c.by, c.cy);
    touchCalSet(c, true);

    uint16_t a, b;
    while (touchRaw(a, b)) delay(10);
    canvas.invalidate();
    _dirty = true;
}

UI::BootChoice UI::bootHoldMenu() {
    static const char* const OPT[3] = { "NORMAL BOOT", "SAFE BOOT", "TOUCH CALIBRATION" };
    static const char* const SUB[3] = { "", "SKIP STORAGE, BUILT-IN DEFAULTS", "TAP THREE CROSSES" };
    const unsigned long BAND_MS = 1500;
    unsigned long t0 = millis();
    uint16_t a, b;
    int choice = 0, released = 0;
    for (;;) {
        bool down = touchRaw(a, b);
        // A few missed samples in a row count as a lift -- resistive panels
        // drop out briefly under a steady finger.
        released = down ? 0 : released + 1;
        if (released >= 4) break;
        choice = (int)((millis() - t0) / BAND_MS);
        if (choice > 2) choice = 2;

        canvas.clear(BG);
        canvas.setFont(FONT_BOLD);
        canvas.textCenter(W / 2, 40, "BOOT OPTIONS", LIME);
        canvas.setFont(FONT_BODY);
        canvas.textCenter(W / 2, 58, "KEEP HOLDING TO MOVE DOWN, LIFT TO PICK", MUTED);
        for (int i = 0; i < 3; i++) {
            int y = 76 + i * 44;
            bool on = (i == choice);
            if (on) cursorFrame(20, y, W - 40, 38, AMBER);
            else    canvas.frame(20, y, W - 40, 38, GRID);
            canvas.setFont(FONT_BOLD);
            canvas.textCenter(W / 2, y + 17, OPT[i], on ? LIME : TEXT);
            canvas.setFont(FONT_SMALL);
            canvas.textCenter(W / 2, y + 30, SUB[i], on ? AMBER : DIM);
        }
        // Progress through the current band.
        unsigned long into = (millis() - t0) % BAND_MS;
        if (choice < 2) canvas.fill(20, 214, (int)((W - 40) * into / BAND_MS), 4, LIME);
        canvas.flush(panelSink());
        delay(20);
    }
    canvas.invalidate();
    _bootStage = 0;
    return (BootChoice)choice;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Drawing
// ═══════════════════════════════════════════════════════════════════════════
void UI::update() {
    unsigned long now = millis();
    // Always redraw on the frame cadence: meters, blinking cursors and the
    // playhead all move, and flush() only sends the bands that changed, so
    // a static page costs a hash per band and no SPI traffic at all.
    if (now - _lastDraw < UI_REFRESH_MS) return;
    _lastDraw = now;
    _dirty = false;

    if (now - _cpuHistT >= 250) {                  // 64 samples = the last 16 s
        _cpuHistT = now;
        _cpuHist[_cpuHistPos] = (uint8_t)constrain((int)(g_cpuLoad * 100.0f), 0, 100);
        _cpuHistPos = (uint8_t)((_cpuHistPos + 1) & 63);
    }
    drawAll();
}

// ── Frame ───────────────────────────────────────────────────────────────────
void UI::drawTopBar() {
    Sequencer& s = seq();
    // The whole bar turns amber while the active track is muted (manual 03).
    uint8_t bar = s.muted() ? AMBER : LIME;
    canvas.fill(0, 0, W, TOP_H, bar);

    char buf[40];
    snprintf(buf, sizeof(buf), "CARPENTER / T%u %s", (unsigned)(*_track + 1),
             (*_track == INTERNAL_TRACK) ? "SYNTH" : "MIDI");
    canvas.setFont(FONT_BOLD);
    canvas.text(4, 13, buf, INK);

    // Right to left: pattern number, transport, REC lamp, level meters.
    snprintf(buf, sizeof(buf), "P%02u", (unsigned)(s.currentPattern() + 1));
    int x = canvas.textRight(316, 13, buf, INK) - canvas.textW(buf) - 14;
    if (s.playState() == PlayState::PLAYING)      triRight(x, 4, 10, INK);
    else if (s.playState() == PlayState::PAUSED) { canvas.fill(x, 4, 3, 10, INK); canvas.fill(x + 5, 4, 3, 10, INK); }
    else                                          canvas.fill(x, 5, 8, 8, INK);
    x -= 8;
    if (g_recArm) {
        canvas.setFont(FONT_BOLD);
        int w = canvas.textW("REC");
        canvas.text(x - w, 13, "REC", RED);
        x -= w + 12;
        if (blinkOn()) canvas.disc(x + 5, 8, 4, RED);
        canvas.circle(x + 5, 8, 4, RED);
        x -= 8;
    }
    // L/R meters: 12 segments each, the last two red near clipping.
    static float pl = 0.0f, pr = 0.0f;
    float l = g_peakL, r = g_peakR;
    pl = (l > pl) ? l : pl * 0.85f;
    pr = (r > pr) ? r : pr * 0.85f;
    const int segs = 12, mx = x - segs * 3;
    for (int i = 0; i < segs; i++) {
        bool hot = i >= segs - 2;
        uint8_t on = hot ? RED : INK;
        canvas.fill(mx + i * 3, 4, 2, 4, (pl * segs > i + 0.5f) ? on : (uint8_t)LIME_D);
        canvas.fill(mx + i * 3, 10, 2, 4, (pr * segs > i + 0.5f) ? on : (uint8_t)LIME_D);
    }
    if (bar == AMBER) {                 // keep unlit segments visible on amber
        for (int i = 0; i < segs; i++) {
            if (!(pl * segs > i + 0.5f)) canvas.fill(mx + i * 3, 4, 2, 4, AMBER_D);
            if (!(pr * segs > i + 0.5f)) canvas.fill(mx + i * 3, 10, 2, 4, AMBER_D);
        }
    }
}

void UI::titleFor(char* left, size_t n, char* right, size_t m) {
    char nb[8];
    unsigned pn = (unsigned)(seq().currentPattern() + 1);
    const char* patch = (g_currentPreset >= 0 && g_currentPreset < engineNumPresets())
                        ? enginePresetName((uint8_t)g_currentPreset) : "USER PATCH";
    switch (_screen) {
    case Screen::MAIN:         snprintf(left, n, "SEQUENCER / %s", patch); break;
    case Screen::MENU:         snprintf(left, n, "MAIN MENU"); break;
    case Screen::STEP_EDIT:
        if (curStep().active()) { noteName(curStep().notes[0], nb);
                                  snprintf(left, n, "STEP EDIT / %02u %s", (unsigned)(_selStep + 1), nb); }
        else snprintf(left, n, "STEP EDIT / %02u OFF", (unsigned)(_selStep + 1));
        break;
    case Screen::PLOCK_EDIT:   snprintf(left, n, "P-LOCKS / STEP %02u", (unsigned)(_selStep + 1)); break;
    case Screen::PATTERN_SEL:  snprintf(left, n, "PATTERNS / T%u", (unsigned)(*_track + 1)); break;
    case Screen::PATTERN_OPTS: snprintf(left, n, "PAT OPTS / PATTERN %u", pn); break;
    case Screen::CHAIN_EDIT:   snprintf(left, n, "CHAIN / REEL T%u", (unsigned)(*_track + 1)); break;
    case Screen::TOOLS:        snprintf(left, n, "TOOLS / PATTERN %u", pn); break;
    case Screen::ENGINE_SEL:   snprintf(left, n, "ENGINE / %s", ENGINE_NAMES[currentEngine]); break;
    case Screen::SYNTH_PAGES:  snprintf(left, n, "SYNTH / %s", patch); break;
    case Screen::SYNTH_EDIT:   snprintf(left, n, "%s / %s", enginePage(_synthPage)->name, ENGINE_NAMES[currentEngine]); break;
    case Screen::PRESETS:      snprintf(left, n, "PRESETS / %s", ENGINE_NAMES[currentEngine]); break;
    case Screen::PRESET_SAVE:  snprintf(left, n, "SAVE PRESET / U%02u", (unsigned)(_saveSlot + 1)); break;
    case Screen::MIXER:        snprintf(left, n, "MIXER / %d TRACKS", NUM_TRACKS); break;
    case Screen::MIDI_SET:     snprintf(left, n, "MIDI / DIN"); break;
    case Screen::SETTINGS:     snprintf(left, n, "SETTINGS / SYSTEM"); break;
    case Screen::USB_MODE:     snprintf(left, n, "USB DRIVE"); break;
    case Screen::PERF:         snprintf(left, n, "SYSTEM MONITOR"); break;
    case Screen::HELP:         snprintf(left, n, "HELP / TOUCH PANEL"); break;
    case Screen::SONG_BROWSE:  snprintf(left, n, "SONGS / %s", _songSaving ? "SAVE" : "LOAD"); break;
    case Screen::SONG_SAVE_NAME: snprintf(left, n, "SAVE SONG / %03u", (unsigned)(_songSlot + 1)); break;
    }
    if (seq().externalSync()) snprintf(right, m, "EXT SYNC");
    else                      snprintf(right, m, "%u BPM", (unsigned)seq().bpm());
}

void UI::drawTitle() {
    char left[48], right[16];
    titleFor(left, sizeof(left), right, sizeof(right));
    canvas.setFont(FONT_BOLD);
    if (_toastUntil > millis()) {
        // Messages flash across the title box in amber.
        canvas.fill(4, TITLE_Y, W - 8, TITLE_H, AMBER);
        canvas.textCenter(W / 2, TITLE_Y + 15, _toastMsg, INK);
        return;
    }
    canvas.frame(4, TITLE_Y, W - 8, TITLE_H, GRID);
    int rw = canvas.textW(right);
    canvas.textRight(W - 10, TITLE_Y + 15, right, LIME);
    // Position box for lists: "03/13", dim, just left of the tempo.
    int limit = W - 10 - rw - 10;
    int n = rowCount();
    if (n > 0 && _screen != Screen::MAIN && _screen != Screen::HELP) {
        char pos[24];
        snprintf(pos, sizeof(pos), "%02d/%02d", _row + 1, n);
        canvas.setFont(FONT_SMALL);
        int pw = canvas.textW(pos);
        canvas.text(limit - pw, TITLE_Y + 14, pos, DIM);
        limit -= pw + 8;
        canvas.setFont(FONT_BOLD);
    }
    canvas.textFit(10, TITLE_Y + 15, upper(left), limit - 10, LIME);
}

void UI::drawSoftKeys() {
    canvas.fill(0, SK_Y - 2, W, H - SK_Y + 2, FOOT);
    canvas.hline(0, SK_Y - 2, W, GRID);
    const bool sh = shift();
    const bool playing = seq().playState() == PlayState::PLAYING;
    static const char* const BASE[SK_COUNT]  = { "PAGE", "PLAY", "SHIFT", "TRACK", "MUTE", "", "", "PUSH" };
    static const char* const SHIFTED[SK_COUNT] = { "PAGE", "REC", "SHIFT", "PAT+", "M.TRK", "", "", "PUSH" };
    canvas.setFont(FONT_KEY);
    for (uint8_t k = 0; k < SK_COUNT; k++) {
        int x = k * SK_W + 1, y = SK_Y + 1, w = SK_W - 2, h = SK_H - 3;
        bool pressed = _tDown && _tKey == k;
        bool latched = (k == SK_SHIFT) && sh;
        uint8_t fg = MUTED;
        if (pressed)      { canvas.fill(x, y, w, h, LIME);  fg = INK; }
        else if (latched) { canvas.fill(x, y, w, h, AMBER); fg = INK; }
        else              { canvas.frame(x, y, w, h, GRID); }
        if (!pressed && !latched) {
            if (sh && k != SK_SHIFT && strcmp(BASE[k], SHIFTED[k]) != 0) fg = AMBER;
            if (k == SK_PLAY && playing && !sh) fg = LIME;
        }
        int cx = x + w / 2, cy = y + h / 2;
        if (k == SK_DOWN)      { triLeft(cx - 5, cy - 5, 11, fg); if (sh) { canvas.setFont(FONT_SMALL); canvas.text(x + 2, y + h - 2, "x8", AMBER); canvas.setFont(FONT_KEY); } }
        else if (k == SK_UP)   { triRight(cx - 2, cy - 5, 11, fg); if (sh) { canvas.setFont(FONT_SMALL); canvas.textRight(x + w - 2, y + h - 2, "x8", AMBER); canvas.setFont(FONT_KEY); } }
        else canvas.textCenter(cx, cy + 5, sh ? SHIFTED[k] : BASE[k], fg);
        if (k == SK_PLAY && playing && !pressed) canvas.fill(x + 3, y + 3, 3, 3, LIME);
    }
}

// ── Lists ───────────────────────────────────────────────────────────────────
// One row of the standard list, NightShift style: label, a dotted leader
// running to a right-aligned value; the selected row gets a lit band and a
// chevron; while editing, the value goes solid amber and the chevron becomes
// a blinking block. Pass `bar` (0..1) for values that run along a range and
// the leader becomes a segment bar.
void UI::drawListRow(int i, int idx, const char* label, const char* value,
                     float bar, uint8_t valueCol) {
    int y = CONTENT_Y + i * ROW_H;
    bool sel = (idx == _row);
    int base = y + 18;
    if (sel) {
        canvas.fill(LIST_X, y + 2, LIST_R - LIST_X + 3, ROW_H - 4, HILITE);
        if (_editing) { if (blinkOn()) canvas.fill(LIST_X + 3, y + 8, 6, 11, AMBER); }
        else          chevron(LIST_X + 3, y + 9, AMBER);
    }
    canvas.setFont(FONT_BODY);
    int lx = canvas.text(LIST_X + 14, base, upper(label), sel ? LIME : TEXT);
    int vx = LIST_R;
    if (value && *value) {
        int vw = canvas.textW(value);
        vx = LIST_R - vw;
        uint8_t vc = (valueCol != 0xFF) ? valueCol : (uint8_t)(sel ? AMBER : LIME);
        if (sel && _editing) {
            canvas.fill(vx - 3, y + 5, vw + 6, 17, AMBER);
            canvas.text(vx, base, value, INK);
        } else {
            canvas.text(vx, base, value, vc);
        }
    }
    if (bar >= 0.0f) {
        int bx = 150, bw = vx - 14 - bx;
        if (bw > 20) canvas.segBar(bx, y + 11, bw, 8, bar, sel ? LIME : BAR, BAR_D);
    } else if (value && *value) {
        int a = lx + 6, b = vx - 8;
        if (b > a) canvas.dotH(a, base, b - a, BAR_D, 1, 3);
    }
}

void UI::drawScrollbar() {
    int n = rowCount(), vis = VIS_ROWS;
    int top = CONTENT_Y + 2, h = VIS_ROWS * ROW_H - 4;
    if (n <= vis) return;
    canvas.dotV(W - 5, top, h, GRID, 1, 2);
    int th = h * vis / n; if (th < 8) th = 8;
    int ty = top + (h - th) * _scroll / (n - vis);
    canvas.fill(W - 6, ty, 3, th, LIME);
}

// ── MAIN: the step grid ─────────────────────────────────────────────────────
// Sixteen cells in two rows of eight. Bar height is velocity, so a page reads
// as a loudness contour; every fourth cell is a shade lighter for the beat.
// Amber double frame = cursor, lime frame = playhead, checkerboard bar =
// probability under 100%, light cap = accent, lime bridge = slide, light
// bridge = tie, amber "x3" = ratchet, amber dot top-right = parameter lock,
// dotted frame = past the pattern's length. (NightShift manual, section 04.)
void UI::drawStepCell(uint8_t step, int x, int y, int w, int h, bool cursor, bool playing) {
    const Step& st = pat().steps[step];
    bool inLen = step < pat().length;
    char num[4];
    snprintf(num, sizeof(num), "%02u", (unsigned)(step + 1));

    if (!inLen) {
        canvas.dotFrame(x, y, w, h, GRID);
        canvas.setFont(FONT_SMALL);
        canvas.text(x + 3, y + 9, num, GRID);
    } else {
        bool beat = (step % 4) == 0;
        canvas.fill(x, y, w, h, beat ? HILITE : PANEL);
        canvas.frame(x, y, w, h, GRID);
        canvas.setFont(FONT_SMALL);
        canvas.text(x + 3, y + 9, num, cursor ? AMBER : playing ? LIME : DIM);
    }

    if (inLen && st.active()) {
        int maxH = h - 18;
        int bh = 3 + (int)st.velocity * (maxH - 3) / 127;
        int bx = x + 5, bw = w - 10, by = y + h - 5 - bh;
        uint8_t c = (cursor || playing) ? LIME : BAR;
        if (st.probability < 100) canvas.checker(bx, by, bw, bh, c);
        else                      canvas.fill(bx, by, bw, bh, c);
        if (st.accent()) canvas.fill(bx, by, bw, 2, TEXT);
        if (st.slide() || st.tie()) {
            // Bridge into the next cell: lime for a slide, light for a tie.
            canvas.fill(bx + bw, by + 4, (x + w + CELL_GAP + 5) - (bx + bw), 3,
                        st.slide() ? LIME : TEXT);
        }
        if (st.ratchet > 1) {
            char r[6]; snprintf(r, sizeof(r), "x%u", (unsigned)st.ratchet);
            canvas.setFont(FONT_SMALL);
            canvas.textRight(x + w - 3, y + 9, r, AMBER);
        }
        bool locked = false;
        for (uint8_t k = 0; k < STEP_LOCKS; k++) if (st.lockId[k] != LOCK_NONE) locked = true;
        if (locked) canvas.fill(x + w - 6, y + 13, 3, 3, AMBER);
        if (st.noteCount > 1) {                   // chord: a tick per extra note
            for (uint8_t k = 1; k < st.noteCount; k++) canvas.fill(x + 4, y + 13 + k * 3, 3, 2, MUTED);
        }
    }

    if (cursor)       cursorFrame(x - 1, y - 1, w + 2, h + 2, AMBER);
    else if (playing) canvas.frame(x, y, w, h, LIME);
}

void UI::drawMain() {
    Sequencer& s = seq();
    const Pattern& p = pat();
    uint8_t page = _selStep / STEPS_PER_PAGE;
    uint8_t base = page * STEPS_PER_PAGE;

    // Status row: MUTE tag, root + scale, length (+ swing).
    canvas.setFont(FONT_BOLD);
    int x = LIST_X + 2;
    if (s.muted()) {
        canvas.fill(x, CONTENT_Y + 1, 42, 15, RED);
        canvas.text(x + 5, CONTENT_Y + 13, "MUTE", TEXT);
        x += 48;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "%s %s", NOTE_NAMES[p.rootNote % 12], SCALES[p.scaleIdx].name);
    canvas.text(x, CONTENT_Y + 13, buf, LIME);
    if (p.swing) snprintf(buf, sizeof(buf), "SW %u%%  %u STEPS", (unsigned)p.swing, (unsigned)p.length);
    else         snprintf(buf, sizeof(buf), "%u STEPS", (unsigned)p.length);
    canvas.textRight(W - 6, CONTENT_Y + 13, buf, LIME);

    for (uint8_t i = 0; i < STEPS_PER_PAGE; i++) {
        uint8_t st = base + i;
        if (st >= NUM_STEPS) break;
        int cx = CELL_X0 + (i % 8) * (CELL_W + CELL_GAP);
        int cy = (i < 8) ? CELL_Y1 : CELL_Y2;
        bool playing = (s.playState() == PlayState::PLAYING) && (st == s.currentStep());
        drawStepCell(st, cx, cy, CELL_W, CELL_H, st == _selStep, playing);
    }

    // Selected-step readout: STEP 16 C4 ....... VEL 100 | GATE 75%
    const Step& st = pat().steps[_selStep];
    char nb[8]; noteName(st.notes[0], nb);
    int by = CELL_Y2 + CELL_H + 18;
    canvas.setFont(FONT_BOLD);
    snprintf(buf, sizeof(buf), "STEP %02u ", (unsigned)(_selStep + 1));
    int ex = canvas.text(LIST_X + 2, by, buf, LIME);
    ex = canvas.text(ex, by, st.active() ? nb : "OFF", AMBER);
    if (st.noteCount > 1) { snprintf(buf, sizeof(buf), " +%u", (unsigned)(st.noteCount - 1)); canvas.text(ex, by, buf, MUTED); }
    snprintf(buf, sizeof(buf), "GATE %u%%", (unsigned)st.gate);
    int gx = canvas.textRight(W - 6, by, buf, TEXT) - canvas.textW(buf);
    canvas.vline(gx - 8, by - 10, 12, GRID);
    snprintf(buf, sizeof(buf), "VEL %u", (unsigned)st.velocity);
    canvas.textRight(gx - 16, by, buf, TEXT);
    // Page dots: which 16 of the 64 this is.
    uint8_t pages = (uint8_t)((p.length + STEPS_PER_PAGE - 1) / STEPS_PER_PAGE);
    if (pages > 1) for (uint8_t k = 0; k < pages; k++)
        canvas.fill(W / 2 - pages * 5 + k * 10, by - 5, 6, 3, k == page ? LIME : GRID);
}

// ── MENU: all thirteen pages on one screen ──────────────────────────────────
void UI::drawMenu() {
    canvas.setFont(FONT_BODY);
    for (int i = 0; i < (int)MenuItem::COUNT; i++) {
        int col = (i < 7) ? 0 : 1, r = (i < 7) ? i : i - 7;
        int x = col ? W / 2 + 2 : LIST_X, y = CONTENT_Y + 2 + r * 23, w = W / 2 - 6;
        bool sel = (i == _row);
        char num[4]; snprintf(num, sizeof(num), "%02d", i + 1);
        if (sel) {
            canvas.fill(x, y, w, 21, LIME);
            canvas.text(x + 6, y + 15, num, INK);
            canvas.text(x + 30, y + 15, MENU_LABELS[i], INK);
        } else {
            canvas.text(x + 6, y + 15, num, DIM);
            canvas.text(x + 30, y + 15, MENU_LABELS[i], TEXT);
        }
    }
}

void UI::drawStepEdit() {
    Step& st = curStep();
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= (int)StepField::COUNT) break;
        val[0] = 0;
        float bar = -1.0f;
        uint8_t vc = 0xFF;
        switch ((StepField)idx) {
        case StepField::ACTIVE:  snprintf(val, sizeof(val), "%s", st.active() ? "ON" : "OFF"); break;
        case StepField::NOTE1: case StepField::NOTE2:
        case StepField::NOTE3: case StepField::NOTE4: {
            uint8_t n = (uint8_t)idx - (uint8_t)StepField::NOTE1;
            if (n < st.noteCount) noteName(st.notes[n], val);
            else                  { snprintf(val, sizeof(val), "--"); vc = DIM; }
            break;
        }
        case StepField::NOTECOUNT: snprintf(val, sizeof(val), "%u", (unsigned)st.noteCount); break;
        case StepField::VELOCITY:  snprintf(val, sizeof(val), "%u", (unsigned)st.velocity); bar = st.velocity / 127.0f; break;
        case StepField::GATE:      snprintf(val, sizeof(val), "%u%%", (unsigned)st.gate); bar = st.gate / 200.0f; break;
        case StepField::PROB:      snprintf(val, sizeof(val), "%u%%", (unsigned)st.probability); bar = st.probability / 100.0f; break;
        case StepField::MICRO:     snprintf(val, sizeof(val), "%+d", (int)st.micro); break;
        case StepField::RATCHET:   snprintf(val, sizeof(val), "x%u", (unsigned)st.ratchet); break;
        case StepField::ACCENT:    snprintf(val, sizeof(val), "%s", st.accent() ? "ON" : "OFF"); break;
        case StepField::SLIDE:     snprintf(val, sizeof(val), "%s", st.slide() ? "ON" : "OFF"); break;
        case StepField::TIE:       snprintf(val, sizeof(val), "%s", st.tie() ? "ON" : "OFF"); break;
        case StepField::LOCKS: {
            uint8_t used = 0;
            for (uint8_t k = 0; k < STEP_LOCKS; k++) if (st.lockId[k] != LOCK_NONE) used++;
            snprintf(val, sizeof(val), "%u/%u  >", (unsigned)used, (unsigned)STEP_LOCKS);
            break;
        }
        default: break;
        }
        drawListRow(i, idx, STEP_FIELD_LABELS[idx], val, bar, vc);
    }
}

void UI::drawStepLocks() {
    Step& st = curStep();
    char lbl[16], val[20];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= rowCount()) break;
        if (idx == STEP_LOCKS * 2) {
            drawListRow(i, idx, "CLEAR ALL", nullptr);
            continue;
        }
        uint8_t slot = (uint8_t)(idx / 2);
        float bar = -1.0f;
        if ((idx & 1) == 0) {
            snprintf(lbl, sizeof(lbl), "LOCK %u PARAM", (unsigned)(slot + 1));
            if (st.lockId[slot] == LOCK_NONE) snprintf(val, sizeof(val), "--");
            else snprintf(val, sizeof(val), "%s", engineParamInfo(st.lockId[slot])->name);
        } else {
            snprintf(lbl, sizeof(lbl), "LOCK %u VALUE", (unsigned)(slot + 1));
            if (st.lockId[slot] == LOCK_NONE) snprintf(val, sizeof(val), "--");
            else { snprintf(val, sizeof(val), "%u", (unsigned)(st.lockVal[slot] * 100 / 255));
                   bar = st.lockVal[slot] / 255.0f; }
        }
        drawListRow(i, idx, lbl, val, bar, st.lockId[slot] == LOCK_NONE ? DIM : 0xFF);
    }
}

// ── PATTERN: one card per pattern, with a map of its steps ──────────────────
void UI::drawPatternSel() {
    Sequencer& s = seq();
    for (int idx = 0; idx < NUM_PATTERNS; idx++) {
        int c = idx & 1, r = idx >> 1;
        int x = c ? W / 2 + 2 : LIST_X, y = CONTENT_Y + r * 41, w = W / 2 - 6, h = 38;
        Pattern& p = s.getPattern((uint8_t)idx);
        bool loaded = (idx == s.currentPattern());
        bool sel = (idx == _row);
        canvas.fill(x, y, w, h, PANEL);
        canvas.frame(x, y, w, h, GRID);
        if (loaded) canvas.fill(x + 2, y + 2, w - 4, 14, HILITE);
        char t[16]; snprintf(t, sizeof(t), "PATTERN %d", idx + 1);
        canvas.setFont(FONT_BOLD);
        canvas.text(x + 5, y + 13, t, loaded ? LIME : TEXT);
        uint8_t used = 0;
        for (uint8_t k = 0; k < p.length; k++) if (p.steps[k].active()) used++;
        canvas.setFont(FONT_SMALL);
        if (loaded) {
            const char* tag = (s.playState() == PlayState::PLAYING) ? "PLAYING" : "LOADED";
            int tw = canvas.textW(tag);
            canvas.frame(x + w - tw - 9, y + 3, tw + 6, 11, LIME);
            canvas.text(x + w - tw - 6, y + 11, tag, LIME);
        }
        // Step map: up to 32 per line, two lines for 64.
        for (uint8_t k = 0; k < p.length; k++) {
            int mx = x + 5 + (k % 32) * 4 + ((k % 32) / 16) * 4, my = y + 20 + (k / 32) * 5;
            bool on = p.steps[k].active();
            bool play = loaded && s.playState() == PlayState::PLAYING && k == s.currentStep();
            if (on || play) canvas.fill(mx, my, 3, 3, play ? LIME : BAR);
            else            canvas.pixel(mx + 1, my + 1, GRID);
        }
        snprintf(t, sizeof(t), "%u/%u", (unsigned)used, (unsigned)p.length);
        canvas.textRight(x + w - 5, y + h - 4, t, DIM);
        if (sel) cursorFrame(x - 1, y - 1, w + 2, h + 2, AMBER);
    }
}

void UI::drawPatternOpts() {
    Pattern& p = pat();
    char val[20];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_PATOPT_ROWS) break;
        float bar = -1.0f;
        switch (idx) {
        case 0: snprintf(val, sizeof(val), "%u", (unsigned)p.length); bar = p.length / (float)NUM_STEPS; break;
        case 1: snprintf(val, sizeof(val), "%u%%", (unsigned)p.swing); bar = p.swing / (float)SWING_MAX; break;
        case 2: snprintf(val, sizeof(val), "%s", SUBDIVS[p.subdiv % NUM_SUBDIVS].name); break;
        case 3: snprintf(val, sizeof(val), "%s", SCALES[p.scaleIdx].name); break;
        case 4: snprintf(val, sizeof(val), "%s", NOTE_NAMES[p.rootNote % 12]); break;
        case 5: snprintf(val, sizeof(val), "%+d", (int)p.transpose); break;
        case 6: snprintf(val, sizeof(val), "%u", (unsigned)p.midiChannel); break;
        case 7: snprintf(val, sizeof(val), "%s",
                         (p.presetIdx == 0xFF) ? "--" : enginePresetName(p.presetIdx % engineNumPresets())); break;
        case 8: snprintf(val, sizeof(val), "%s", TIME_SIGS[p.timeSigIdx % NUM_TIME_SIGS].name); break;
        }
        drawListRow(i, idx, PATOPT_LABELS[idx], val, bar);
    }
}

// ── CHAIN: frames on a strip of film ────────────────────────────────────────
void UI::drawChain() {
    Sequencer& s = seq();
    // Sprocket holes, top and bottom.
    for (int x = 6; x < W - 4; x += 12) {
        canvas.fill(x, 51, 7, 4, GRID);
        canvas.fill(x, 121, 7, 4, GRID);
    }
    char t[12];
    for (int i = 0; i < CHAIN_LEN; i++) {
        int x = 4 + i * 39, y = 58, w = 36, h = 60;
        ChainEntry& c = _gs->chain[*_track][i];
        bool sel = (i == _row);
        bool live = s.isChaining() && i < s.chainLen() && i == s.chainPos();
        canvas.setFont(FONT_SMALL);
        if (c.patternIdx < 0) {
            canvas.checker(x + 1, y + 1, w - 2, h - 2, PANEL);
            canvas.frame(x, y, w, h, GRID);
            snprintf(t, sizeof(t), "%d", i + 1);
            canvas.text(x + 3, y + 9, t, DIM);
            canvas.setFont(FONT_BIG);
            canvas.textCenter(x + w / 2, y + 36, "--", DIM);
        } else {
            canvas.fill(x, y, w, h, PANEL);
            canvas.frame(x, y, w, h, live ? LIME : GRID);
            snprintf(t, sizeof(t), "%d", i + 1);
            canvas.text(x + 3, y + 9, t, DIM);
            canvas.setFont(FONT_BIG);
            snprintf(t, sizeof(t), "P%d", c.patternIdx + 1);
            canvas.textCenter(x + w / 2, y + 34, t, LIME);
            canvas.setFont(FONT_BODY);
            snprintf(t, sizeof(t), "x%u", (unsigned)c.repeats);
            canvas.textCenter(x + w / 2, y + 51, t, (sel && _editing && shift()) ? AMBER : TEXT);
        }
        if (sel) {
            cursorFrame(x - 1, y - 1, w + 2, h + 2, AMBER);
            if (_editing && blinkOn()) canvas.fill(x + 3, y + h - 6, w - 6, 2, AMBER);
        }
    }

    // Three buttons: APPLY/STOP CHAIN, LOAD SONG, SAVE SONG.
    static const char* const BTN[3] = { nullptr, "LOAD SONG", "SAVE SONG" };
    canvas.setFont(FONT_BOLD);
    for (int b = 0; b < 3; b++) {
        int x = 8 + b * 102, y = 132, w = 96, h = 26;
        bool sel = (_row == CHAIN_LEN + b);
        const char* lbl = BTN[b] ? BTN[b] : (s.isChaining() ? "STOP CHAIN" : "APPLY CHAIN");
        canvas.fill(x, y, w, h, sel ? HILITE : PANEL);
        canvas.frame(x, y, w, h, sel ? AMBER : GRID);
        if (sel) canvas.frame(x + 1, y + 1, w - 2, h - 2, AMBER);
        canvas.textCenter(x + w / 2, y + 17, lbl, sel ? LIME : TEXT);
    }

    // Summary: how many frames, how many plays in all, and the state.
    int takes = 0, plays = 0;
    for (int i = 0; i < CHAIN_LEN; i++) {
        const ChainEntry& c = _gs->chain[*_track][i];
        if (c.patternIdx < 0) break;
        takes++; plays += c.repeats;
    }
    char buf[40];
    snprintf(buf, sizeof(buf), "%d TAKES / %d PLAYS   CHAIN %s", takes, plays,
             s.isChaining() ? "ON" : "OFF");
    canvas.setFont(FONT_BODY);
    canvas.textCenter(W / 2, 186, buf, MUTED);
    canvas.setFont(FONT_SMALL);
    canvas.textCenter(W / 2, 202, "TURN: PATTERN   SHIFT+TURN: REPEATS", DIM);
}

void UI::drawTools() {
    char val[20];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_TOOL_ROWS) break;
        val[0] = 0;
        float bar = -1.0f;
        switch (idx) {
        case 0: snprintf(val, sizeof(val), "%u", (unsigned)_eucHits); break;
        case 1: snprintf(val, sizeof(val), "%u", (unsigned)_eucRot); break;
        case 3: snprintf(val, sizeof(val), "%u%%", (unsigned)_randDensity); bar = _randDensity / 100.0f; break;
        case 5: snprintf(val, sizeof(val), "%u", (unsigned)_humanAmt); bar = _humanAmt / 60.0f; break;
        case 7: snprintf(val, sizeof(val), "%s", RIFFS[_riffSel % NUM_RIFFS].name); break;
        default: break;
        }
        char lbl[24];
        if (idx == 7) snprintf(lbl, sizeof(lbl), "RIFF %.9s", RIFF_CATEGORY_NAMES[_riffCatFilter]);
        else          snprintf(lbl, sizeof(lbl), "%s", TOOL_LABELS[idx]);
        drawListRow(i, idx, lbl, val[0] ? val : nullptr, bar);
    }
}

void UI::drawEngineSel() {
    char who[40];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_ENGINES) break;
        const char* v;
        uint8_t vc = 0xFF;
        if (ENGINE_IS_STUB[idx])          { v = "SOON"; vc = DIM; }
        else if (idx == currentEngine)    { v = "LIVE"; vc = LIME; }
        else                              { composerOf((uint8_t)idx, who, sizeof(who));
                                            // Composer credit only, clipped to the space
                                            // the name leaves free.
                                            canvas.setFont(FONT_BODY);
                                            int room = LIST_R - (LIST_X + 14 + canvas.textW(ENGINE_NAMES[idx])) - 20;
                                            while (canvas.textW(who) > room && strlen(who) > 3) {
                                                who[strlen(who) - 2] = '.'; who[strlen(who) - 1] = 0;
                                            }
                                            v = who; vc = DIM; }
        drawListRow(i, idx, ENGINE_NAMES[idx], v, -1.0f, (idx == _row && vc == DIM) ? (uint8_t)MUTED : vc);
    }
}

void UI::drawSynthPages() {
    uint8_t numPages = engineNumPages();
    char val[8];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= numPages) break;
        snprintf(val, sizeof(val), "%u", (unsigned)enginePage((uint8_t)idx)->count);
        drawListRow(i, idx, enginePage((uint8_t)idx)->name, val);
    }
}

void UI::drawSynthEdit() {
    const EnginePageDef& pg = *enginePage(_synthPage);
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= pg.count) break;
        uint8_t id = pg.first + (uint8_t)idx;
        engineFormatParameter(id, val, sizeof(val));
        const ParamInfo& pi = *engineParamInfo(id);
        // Continuous parameters also get a segment bar, so a sweep is
        // readable without parsing the number.
        float bar = -1.0f;
        if (pi.kind == PK_CONT || pi.kind == PK_HZ || pi.kind == PK_MS ||
            pi.kind == PK_BIPOLAR || pi.kind == PK_CENTS || pi.kind == PK_INT)
            bar = engineGetParameter(id);
        drawListRow(i, idx, pi.name, val, bar);
    }
}

void UI::drawPresets() {
    char lbl[24];
    uint8_t numPresets = engineNumPresets();
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= rowCount()) break;
        if (idx < numPresets)
            snprintf(lbl, sizeof(lbl), "F%02d %s", idx + 1, enginePresetName((uint8_t)idx));
        else
            snprintf(lbl, sizeof(lbl), "U%02d USER SLOT", idx - numPresets + 1);
        drawListRow(i, idx, lbl, (idx == g_currentPreset) ? "LIVE" : nullptr, -1.0f,
                    (idx == g_currentPreset) ? LIME : 0xFF);
    }
    canvas.setFont(FONT_SMALL);
    canvas.textRight(LIST_R, SK_Y - 5, "SHIFT+PUSH: SAVE TO SLOT", DIM);
}

// Caret under the name character being edited. BODY is 8px monospace and
// names are always padded to 13 characters, so the position is fixed.
void UI::drawNameCaret(int rowSlot) {
    canvas.setFont(FONT_BODY);
    int x = LIST_R - canvas.textW(_nameBuf) + _nameCursor * 8;
    canvas.fill(x, CONTENT_Y + rowSlot * ROW_H + 21, 7, 2, LIME);
}

void UI::drawPresetSave() {
    char val[16];
    for (int i = 0; i < 3; i++) {
        const char* lbl = (i == 0) ? "SLOT" : (i == 1) ? "NAME" : "SAVE";
        val[0] = 0;
        if (i == 0) snprintf(val, sizeof(val), "U%02u", (unsigned)(_saveSlot + 1));
        else if (i == 1) snprintf(val, sizeof(val), "%s", _nameBuf);
        drawListRow(i, i, lbl, val[0] ? val : nullptr);
    }
    if (_row == 1 && _editing) drawNameCaret(1);
    canvas.setFont(FONT_SMALL);
    canvas.textRight(LIST_R, SK_Y - 5, "TURN: LETTER   SHIFT+TURN: CARET", DIM);
}

// ── MIXER: one channel strip per track ──────────────────────────────────────
void UI::drawMixer() {
    char buf[40];
    for (int t = 0; t < NUM_TRACKS; t++) {
        Sequencer& s = _seqs[t];
        int x = 4 + t * 78, y = CONTENT_Y + 2, w = 74, h = 104;
        bool sel = (t == _row);
        canvas.fill(x, y, w, h, PANEL);
        canvas.frame(x, y, w, h, GRID);
        canvas.setFont(FONT_BOLD);
        snprintf(buf, sizeof(buf), "T%d", t + 1);
        canvas.text(x + 5, y + 14, buf, (t == *_track) ? LIME : TEXT);
        canvas.setFont(FONT_SMALL);
        canvas.textRight(x + w - 5, y + 12, (t == INTERNAL_TRACK) ? "SYNTH" : "MIDI", MUTED);
        // Activity lamp: lit while the playhead sits on an active step.
        bool act = s.playState() == PlayState::PLAYING &&
                   s.currentPatternRef().steps[s.currentStep()].active() && !s.muted();
        canvas.fill(x + 5, y + 20, w - 10, 3, act ? LIME : GRID);
        // Level ladder: pattern density, a stand-in for a level a MIDI lane
        // doesn't have -- how busy this track is.
        const Pattern& p = s.currentPatternRef();
        int used = 0;
        for (uint8_t k = 0; k < p.length; k++) if (p.steps[k].active()) used++;
        int rungs = 12, lit = p.length ? (used * rungs + p.length - 1) / p.length : 0;
        for (int k = 0; k < rungs; k++)
            canvas.fill(x + 12, y + 72 - k * 4, w - 24, 2, (k < lit) ? (uint8_t)(s.muted() ? DIM : LIME_D) : (uint8_t)BAR_D);
        // Mute key.
        int mx = x + w / 2 - 8, my = y + 78;
        if (s.muted()) canvas.fill(mx, my, 17, 12, RED);
        else           canvas.frame(mx, my, 17, 12, GRID);
        canvas.setFont(FONT_SMALL);
        canvas.textCenter(mx + 9, my + 9, "M", s.muted() ? TEXT : DIM);
        snprintf(buf, sizeof(buf), "P%u", (unsigned)(s.currentPattern() + 1));
        canvas.textCenter(x + w / 2, y + h - 3, buf, DIM);
        if (sel) cursorFrame(x - 1, y - 1, w + 2, h + 2, AMBER);
    }

    // Detail box for the selected strip.
    int t = constrain((int)_row, 0, NUM_TRACKS - 1);
    Sequencer& s = _seqs[t];
    int y = CONTENT_Y + 112;
    canvas.frame(4, y, W - 8, 50, GRID);
    canvas.setFont(FONT_BOLD);
    if (t == INTERNAL_TRACK) {
        const char* patch = (g_currentPreset >= 0 && g_currentPreset < engineNumPresets())
                            ? enginePresetName((uint8_t)g_currentPreset) : "USER PATCH";
        snprintf(buf, sizeof(buf), "T%d SYNTH / %s", t + 1, patch);
    } else {
        snprintf(buf, sizeof(buf), "T%d MIDI / OUTBOARD", t + 1);
    }
    canvas.textFit(10, y + 17, upper(buf), 220, LIME);
    snprintf(buf, sizeof(buf), "CH %02u", (unsigned)s.currentPatternRef().midiChannel);
    bool ed = _editing && t == _mixRow;
    int bw = 60, bx = W - 12 - bw;
    if (ed) canvas.fill(bx, y + 5, bw, 18, AMBER);
    else    cursorFrame(bx, y + 5, bw, 18, AMBER);
    canvas.textCenter(bx + bw / 2, y + 18, buf, ed ? INK : AMBER);
    canvas.setFont(FONT_BODY);
    if (t == INTERNAL_TRACK)
        snprintf(buf, sizeof(buf), "PAT %u   %s   %u-VOICE", (unsigned)(s.currentPattern() + 1),
                 ENGINE_NAMES[currentEngine], (unsigned)engineNumVoices());
    else
        snprintf(buf, sizeof(buf), "PAT %u   %s", (unsigned)(s.currentPattern() + 1),
                 s.muted() ? "MUTED" : "MIDI OUT");
    canvas.textFit(10, y + 40, buf, W - 24, MUTED);
}

void UI::drawMidiSet() {
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_MIDI_ROWS) break;
        val[0] = 0;
        switch (idx) {
        case 0:
            if (_gs->midiInChannel == 0) snprintf(val, sizeof(val), "OMNI");
            else                         snprintf(val, sizeof(val), "%u", (unsigned)_gs->midiInChannel);
            break;
        case 1: snprintf(val, sizeof(val), "%s", _gs->midiOutEnable ? "ON" : "OFF"); break;
        case 2: snprintf(val, sizeof(val), "%s", _gs->midiClockOut ? "ON" : "OFF"); break;
        case 3: snprintf(val, sizeof(val), "%s", _gs->extSync ? "ON" : "OFF"); break;
        case 4: snprintf(val, sizeof(val), "%s", _gs->midiThru ? "ON" : "OFF"); break;
        case 5: snprintf(val, sizeof(val), "%s", _gs->midiKbdPlay ? "ON" : "OFF"); break;
        case 6: snprintf(val, sizeof(val), "%+d", uiKbdOctave(0)); break;
        default: break;
        }
        drawListRow(i, idx, MIDI_LABELS[idx], val[0] ? val : nullptr, -1.0f,
                    idx == MIDI_ROW_PANIC ? (uint8_t)RED : (uint8_t)0xFF);
    }
}

void UI::drawSettings() {
    char val[16];
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= NUM_SETTING_ROWS) break;
        val[0] = 0;
        float bar = -1.0f;
        uint8_t vc = 0xFF;
        switch ((SetRow)idx) {
        case SetRow::MASTER: {
            float v = engineGetParameter(engineMasterParamId());
            snprintf(val, sizeof(val), "%d%%", (int)(v * 100.0f)); bar = v; break;
        }
        case SetRow::BACKLIGHT:
            snprintf(val, sizeof(val), "%d%%", (int)_gs->contrast * 100 / 255); bar = _gs->contrast / 255.0f; break;
        case SetRow::METRONOME: snprintf(val, sizeof(val), "%s", _gs->metronome ? "ON" : "OFF"); break;
        case SetRow::CRT:       snprintf(val, sizeof(val), "%s", _gs->crtLines ? "ON" : "OFF"); break;
        case SetRow::PHOSPHOR:  snprintf(val, sizeof(val), "%s", PHOSPHOR_NAMES[_gs->phosphor % NUM_PHOSPHORS]); break;
        case SetRow::TOUCH_CAL: snprintf(val, sizeof(val), "3 POINT"); vc = DIM; break;
        case SetRow::SAVE_ALL:  snprintf(val, sizeof(val), "%s", _store->mounted() ? "" : "NO FS"); break;
        case SetRow::FORMAT_FS: snprintf(val, sizeof(val), "%s", shift() ? "READY" : "+SHIFT"); vc = RED; break;
        case SetRow::DEF_TIMESIG: snprintf(val, sizeof(val), "%s", TIME_SIGS[_gs->defaultTimeSigIdx % NUM_TIME_SIGS].name); break;
        case SetRow::USB_DRIVE: snprintf(val, sizeof(val), "%s", g_diskMode ? "MOUNTED" : uiUsbAvailable() ? "" : "OTG ONLY"); break;
        default: break;
        }
        drawListRow(i, idx, SETTING_LABELS[idx], val[0] ? val : nullptr, bar, vc);
    }
}

// Static message screen: an explicit user action (SETTINGS -> USB DRIVE)
// parked the transport and silenced the engine for it, so nothing behind it
// is moving.
void UI::drawUsbMode() {
    canvas.frame(20, 60, W - 40, 110, GRID);
    canvas.frame(23, 63, W - 46, 104, GRID);
    canvas.setFont(FONT_BIG);
    canvas.textCenter(W / 2, 90, "USB DRIVE", LIME);
    canvas.setFont(FONT_BODY);
    canvas.textCenter(W / 2, 112, g_diskMode ? "THE COMPUTER HAS THE DRIVE" : "EJECTED - DRIVE IS BACK",
                      g_diskMode ? AMBER : LIME);
    canvas.textCenter(W / 2, 134, "COPY .SYX BANKS INTO /BANKS,", MUTED);
    canvas.textCenter(W / 2, 150, "EJECT, THEN PRESS PAGE", MUTED);
}

// ── PERF: system monitor ────────────────────────────────────────────────────
void UI::drawPerf() {
    // CPU graph, the last 16 seconds.
    int gx = 4, gy = CONTENT_Y + 1, gw = W - 8, gh = 70;
    canvas.frame(gx, gy, gw, gh, GRID);
    for (int k = 1; k < 4; k++) canvas.dotH(gx + 1, gy + k * gh / 4, gw - 2, GRID, 1, 3);
    for (int k = 1; k < 8; k++) canvas.dotV(gx + k * gw / 8, gy + 1, gh - 2, GRID, 1, 3);
    canvas.setFont(FONT_SMALL);
    canvas.text(gx + 4, gy + 9, "CPU AUDIO CORE / 16 S", DIM);
    int prevX = -1, prevY = 0;
    for (int i = 0; i < 64; i++) {
        uint8_t v = _cpuHist[(_cpuHistPos + i) & 63];
        int px = gx + 2 + i * (gw - 4) / 63;
        int py = gy + gh - 3 - v * (gh - 14) / 100;
        if (prevX >= 0) { canvas.line(prevX, prevY, px, py, LIME); canvas.line(prevX, prevY + 1, px, py + 1, LIME); }
        prevX = px; prevY = py;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "%d%%", (int)(g_cpuLoad * 100.0f));
    canvas.setFont(FONT_BOLD);
    canvas.textRight(gx + gw - 5, gy + 15, buf, g_cpuLoad > 0.9f ? RED : AMBER);

    // Six boxes, two columns.
    struct Box { const char* cap; char val[20]; float bar; };
    Box b[6];
    int maxV = engineNumVoices();
    b[0] = { "SYNTH VOICES", "", maxV ? (float)g_activeVoices / maxV : 0.0f };
    snprintf(b[0].val, sizeof(b[0].val), "%d/%d", g_activeVoices, maxV);
    b[1] = { "ENGINE", "", -1.0f };
    snprintf(b[1].val, sizeof(b[1].val), "%s", ENGINE_NAMES[currentEngine]);
    b[2] = { "FREE RAM", "", -1.0f };
    snprintf(b[2].val, sizeof(b[2].val), "%u KB", (unsigned)(panelFreeHeap() / 1024));
    b[3] = { "AUDIO XRUNS", "", -1.0f };
    snprintf(b[3].val, sizeof(b[3].val), "%u", (unsigned)g_xruns);
    b[4] = { "CLOCK", "", -1.0f };
    snprintf(b[4].val, sizeof(b[4].val), "%u MHZ", (unsigned)panelCpuMhz());
    b[5] = { "UPTIME", "", -1.0f };
    unsigned long up = millis() / 1000;
    snprintf(b[5].val, sizeof(b[5].val), "%lu:%02lu:%02lu", up / 3600, (up / 60) % 60, up % 60);
    for (int i = 0; i < 6; i++) {
        int x = (i & 1) ? W / 2 + 2 : 4, y = gy + gh + 4 + (i >> 1) * 31, w = W / 2 - 6, h = 28;
        canvas.frame(x, y, w, h, GRID);
        canvas.setFont(FONT_SMALL);
        canvas.text(x + 4, y + 9, b[i].cap, DIM);
        canvas.setFont(FONT_BOLD);
        uint8_t c = (i == 3 && g_xruns) ? AMBER : LIME;
        canvas.textFit(x + 4, y + 23, b[i].val, w - 8, c);
        if (b[i].bar >= 0.0f) canvas.segBar(x + w - 60, y + 15, 56, 8, b[i].bar, LIME, BAR_D);
    }
}

void UI::drawHelp() {
    canvas.setFont(FONT_BODY);
    for (int i = 0; i < NUM_HELP_LINES; i++) {
        const char* s = HELP_LINES[i];
        canvas.text(LIST_X + 6, CONTENT_Y + 12 + i * 12, s, (s[0] == ' ') ? MUTED : TEXT);
    }
}

void UI::drawSongBrowse() {
    char lbl[16], val[16];
    Song s;
    for (int i = 0; i < VIS_ROWS; i++) {
        int idx = _scroll + i;
        if (idx >= SONG_SLOTS) break;
        snprintf(lbl, sizeof(lbl), "SONG %03d", idx + 1);
        // Only the visible rows ever touch the filesystem.
        bool have = _store->loadSong((uint8_t)idx, s) && s.name[0];
        if (have) snprintf(val, sizeof(val), "%s", s.name);
        else      snprintf(val, sizeof(val), "--");
        drawListRow(i, idx, lbl, val, -1.0f, have ? 0xFF : DIM);
    }
    canvas.setFont(FONT_SMALL);
    canvas.textRight(LIST_R, SK_Y - 5, _songSaving ? "PUSH: SAVE   SHIFT+PUSH: DELETE"
                                                   : "PUSH: LOAD   SHIFT+PUSH: DELETE", DIM);
}

void UI::drawSongSaveName() {
    char val[16];
    for (int i = 0; i < 2; i++) {
        const char* lbl = (i == 0) ? "NAME" : "SAVE";
        val[0] = 0;
        if (i == 0) snprintf(val, sizeof(val), "%s", _nameBuf);
        drawListRow(i, i, lbl, val[0] ? val : nullptr);
    }
    if (_row == 0 && _editing) drawNameCaret(0);
    canvas.setFont(FONT_SMALL);
    canvas.textRight(LIST_R, SK_Y - 5, "TURN: LETTER   SHIFT+TURN: CARET", DIM);
}

void UI::drawAll() {
    canvas.clear(BG);
    drawTopBar();
    drawTitle();
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
    if (isListScreen()) drawScrollbar();
    drawSoftKeys();
    canvas.flush(panelSink());
}

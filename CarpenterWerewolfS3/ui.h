#pragma once
#include <Arduino.h>
#include "config.h"
#include "gfx.h"
#include "sequencer.h"
#include "engine.h"
#include "storage.h"

// ============================================================================
//  ui.h — the panel UI, redrawn for the werewolf's 320x240 ILI9341.
//
//  The screen graph, the input model and every action are the xtro Carpenter
//  Synth's own (ui.cpp's onXxx()/activateRow()/editValue() are carried over
//  intact); what changed is the drawing, which is now a native 320x240
//  colour panel in the NightShift manual's visual language, and a second
//  input path: the touchscreen.
//
//  Touch works two ways at once:
//   * a soft-key strip along the bottom edge stands in for the physical
//     buttons (PAGE PLAY SHIFT TRACK MUTE, two encoder-step keys and PUSH),
//     with the same short/long-press behaviour; SHIFT latches, since a
//     resistive panel can only report one finger;
//   * the content itself is touchable: tap a row or a step to put the cursor
//     on it, tap it again to push, drag up/down to turn the encoder.
//  An encoder and buttons wired to the S3 (config.h) keep working alongside.
// ============================================================================

enum class Screen : uint8_t {
    MAIN = 0,
    MENU,
    STEP_EDIT,
    PLOCK_EDIT,
    PATTERN_SEL,
    PATTERN_OPTS,
    CHAIN_EDIT,
    TOOLS,
    ENGINE_SEL,         // pick which synth engine is live (CS-80/Minimoog/...)
    SYNTH_PAGES,
    SYNTH_EDIT,
    PRESETS,
    PRESET_SAVE,
    MIXER,
    MIDI_SET,
    SETTINGS,
    USB_MODE,           // SETTINGS -> USB DRIVE: exports the FS partition over USB
    PERF,
    HELP,
    SONG_BROWSE,        // 200 song slots, reached from CHAIN_EDIT's LOAD/SAVE SONG rows
    SONG_SAVE_NAME       // name editor + SAVE, for the slot picked in SONG_BROWSE
};

// Main-menu order must match MENU_LABELS in ui.cpp.
enum class MenuItem : uint8_t {
    STEP_EDIT = 0, PATTERN, PAT_OPTS, CHAIN, TOOLS,
    ENGINE, SYNTH, PRESETS, MIXER, MIDI, SETTINGS, PERF, HELP,
    COUNT
};

// STEP EDIT rows.
enum class StepField : uint8_t {
    ACTIVE = 0, NOTE1, NOTE2, NOTE3, NOTE4, NOTECOUNT,
    VELOCITY, GATE, PROB, MICRO, RATCHET,
    ACCENT, SLIDE, TIE, LOCKS,
    COUNT
};

// SETTINGS rows.
enum class SetRow : uint8_t {
    MASTER = 0, BACKLIGHT, METRONOME, CRT, PHOSPHOR, TOUCH_CAL,
    SAVE_ALL, FORMAT_FS, DEF_TIMESIG, USB_DRIVE,
    COUNT
};

// Colour schemes for the whole panel. GREEN is the NightShift reference.
enum Phosphor : uint8_t { PHOS_GREEN = 0, PHOS_AMBER, PHOS_BLOOD, NUM_PHOSPHORS };

// Soft keys along the bottom edge, left to right.
enum SoftKey : uint8_t {
    SK_PAGE = 0, SK_PLAY, SK_SHIFT, SK_TRACK, SK_MUTE, SK_DOWN, SK_UP, SK_PUSH,
    SK_COUNT, SK_NONE = 0xFF
};

class UI {
public:
    // Display bring-up is deliberately split from everything else and is the
    // FIRST thing setup() does.  If any later stage stalls, the panel is
    // already alive and bootStatus() can say where — a blank screen is the one
    // failure mode that tells you nothing.
    void beginDisplay();
    void bootStatus(const char* msg);
    void bind(Sequencer* seqs, uint8_t* activeTrack, Storage* storage, GlobalSettings* gs);
    void applyContrast(uint8_t c);           // backlight, 0..255 (floored)
    void applyLook(uint8_t phosphor, bool crt);

    void update();                 // throttled redraw
    void markDirty() { _dirty = true; }
    void toast(const char* msg);

    // ── Input events, dispatched from loop() ───────────────────────────────
    void onEncoder(int delta);
    void onEncPress();
    void onEncLong();
    void onPlay();       void onPlayLong();
    void onPage();       void onPageLong();
    void onTrack();      void onTrackLong();
    void onMute();       void onMuteLong();
    // SHIFT is held on a physical button, latched on the soft key; either
    // counts.
    void setShift(bool held) { if (held != _physShift) { _physShift = held; _dirty = true; } }
    bool shift() const { return _physShift || _shiftLatch; }

    // Touch, already in screen pixels. Call every loop with the current
    // state; the UI does its own press/hold/drag tracking.
    void touch(bool down, int x, int y);

    // Blocking three-point touch calibration (tests the raw axes, so it also
    // works out rotation/mirroring). Used at boot and from SETTINGS.
    void runTouchCalibration();

    // Finger on the glass at power-up: choose by HOW LONG it stays there,
    // not where -- so it works with a wrong or missing touch calibration.
    // Release in the first band = normal boot, second = safe boot (skip the
    // filesystem), third = recalibrate the touchscreen.
    enum BootChoice : uint8_t { BOOT_NORMAL = 0, BOOT_SAFE, BOOT_TOUCH_CAL };
    BootChoice bootHoldMenu();

    Screen screen() const { return _screen; }
    void   gotoScreen(Screen s);

private:
    // ── Bound state ────────────────────────────────────────────────────────
    Sequencer*      _seqs = nullptr;
    uint8_t*        _track = nullptr;
    Storage*        _store = nullptr;
    GlobalSettings* _gs = nullptr;

    Sequencer& seq()      { return _seqs[*_track]; }
    Pattern&   pat()      { return seq().currentPatternRef(); }
    Step&      curStep()  { return pat().steps[_selStep % NUM_STEPS]; }

    // ── Navigation ─────────────────────────────────────────────────────────
    Screen  _screen = Screen::MAIN;
    bool    _dirty  = true;
    bool    _physShift = false;
    bool    _shiftLatch = false;
    bool    _editing = false;         // encoder edits the value, not the cursor
    unsigned long _lastDraw = 0;

    int16_t _row = 0;                 // selected row on the current screen
    int16_t _scroll = 0;

    uint8_t _selStep = 0;
    uint8_t _synthPage = 0;
    uint8_t _saveSlot = 0;
    uint8_t _nameCursor = 0;
    char    _nameBuf[14] = "USER 01";
    uint8_t _eucHits = 4, _eucRot = 0, _randDensity = 45, _humanAmt = 12, _riffSel = 0;
    uint8_t _riffCatFilter = 0;       // RIFF_CAT_ALL
    uint8_t _mixRow = 0;

    bool    _songSaving = false;      // SONG_BROWSE reached via LOAD SONG vs SAVE SONG
    uint8_t _songSlot = 0;            // slot chosen in SONG_BROWSE, for SONG_SAVE_NAME

    Pattern _clip;                    // pattern clipboard (copy/paste across tracks)
    bool    _clipFull = false;

    char          _toastMsg[26] = {0};
    unsigned long _toastUntil = 0;

    // ── Touch state ────────────────────────────────────────────────────────
    bool          _tDown = false;
    int16_t       _tX0 = 0, _tY0 = 0, _tX = 0, _tY = 0;
    unsigned long _tT0 = 0;
    uint8_t       _tKey = SK_NONE;    // soft key held, if the touch began on one
    bool          _tLongFired = false;
    bool          _tDragging = false;
    int16_t       _tDragAcc = 0;      // pixels of drag not yet turned into detents
    int16_t       _tHitRow = -1;      // content row/step under the finger at touch-down
    unsigned long _tRepeatAt = 0;

    // ── PERF history ───────────────────────────────────────────────────────
    uint8_t       _cpuHist[64] = {0};
    uint8_t       _cpuHistPos = 0;
    unsigned long _cpuHistT = 0;

    uint8_t       _bootStage = 0;

    // ── Helpers ────────────────────────────────────────────────────────────
    int  rowCount() const;
    bool isListScreen() const;
    int  visRows() const;
    void moveRow(int delta);
    void editValue(int delta);
    void activateRow();               // encoder press on a non-edit row
    void ensureVisible(int visibleRows);
    void consumeShift();              // a SHIFT-latched action happened

    // Touch
    void touchDown(int x, int y);
    void touchHeld(int x, int y);
    void touchUp(int x, int y);
    void softKeyShort(uint8_t k);
    void softKeyLong(uint8_t k);
    int  hitContent(int x, int y) const;   // row / step index under a point, or -1
    void tapContent(int hit);
    void dragDetents(int dy);

    // Drawing
    void drawAll();
    void drawTopBar();
    void drawTitle();
    void drawSoftKeys();
    void titleFor(char* left, size_t n, char* right, size_t m);
    void drawMain();
    void drawMenu();
    void drawStepEdit();
    void drawStepLocks();
    void drawPatternSel();
    void drawPatternOpts();
    void drawChain();
    void drawTools();
    void drawEngineSel();
    void drawSynthPages();
    void drawSynthEdit();
    void drawPresets();
    void drawPresetSave();
    void drawMixer();
    void drawMidiSet();
    void drawSettings();
    void drawUsbMode();
    void drawPerf();
    void drawHelp();
    void drawSongBrowse();
    void drawSongSaveName();
    void drawListRow(int i, int idx, const char* label, const char* value,
                     float bar = -1.0f, uint8_t valueCol = 0xFF);
    void drawScrollbar();
    void drawStepCell(uint8_t step, int x, int y, int w, int h, bool cursor, bool playing);
    void drawNameCaret(int rowSlot);
};

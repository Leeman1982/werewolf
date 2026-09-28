#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "config.h"
#include "sequencer.h"
#include "engine.h"
#include "storage.h"

// OLED controller — selected in config.h.  Full-buffer (_F_) so the whole UI is
// composed in RAM and pushed in one transfer: 1 KB of SRAM in exchange for a
// clearBuffer/sendBuffer workflow with no flicker and no page-tearing.
#if defined(OLED_DRIVER_SH1106)
  typedef U8G2_SH1106_128X64_NONAME_F_HW_I2C   SynthOled;
#elif defined(OLED_DRIVER_SSD1306)
  typedef U8G2_SSD1306_128X64_NONAME_F_HW_I2C  SynthOled;
#else
  typedef U8G2_SSD1309_128X64_NONAME0_F_HW_I2C SynthOled;   // confirmed working
#endif

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

class UI {
public:
    // Display bring-up is deliberately split from everything else and is the
    // FIRST thing setup() does.  If any later stage stalls, the panel is
    // already alive and bootStatus() can say where — a blank screen is the one
    // failure mode that tells you nothing.
    void beginDisplay();
    void bootStatus(const char* msg);
    void bind(Sequencer* seqs, uint8_t* activeTrack, Storage* storage, GlobalSettings* gs);
    void applyContrast(uint8_t c);

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
    void setShift(bool held) { _shift = held; }
    bool shift() const { return _shift; }

    Screen screen() const { return _screen; }
    void   gotoScreen(Screen s);

private:
    // ── Bound state ────────────────────────────────────────────────────────
    Sequencer*      _seqs = nullptr;
    uint8_t*        _track = nullptr;
    Storage*        _store = nullptr;
    GlobalSettings* _gs = nullptr;
    SynthOled       _oled{U8G2_R0, U8X8_PIN_NONE};

    Sequencer& seq()      { return _seqs[*_track]; }
    Pattern&   pat()      { return seq().currentPatternRef(); }
    Step&      curStep()  { return pat().steps[_selStep % NUM_STEPS]; }

    // ── Navigation ─────────────────────────────────────────────────────────
    Screen  _screen = Screen::MAIN;
    bool    _dirty  = true;
    bool    _shift  = false;
    bool    _editing = false;         // encoder edits the value, not the cursor
    unsigned long _lastDraw = 0;

    int8_t  _row = 0;                 // selected row on the current screen
    int8_t  _scroll = 0;

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

    char          _toastMsg[22] = {0};
    unsigned long _toastUntil = 0;

    // ── Helpers ────────────────────────────────────────────────────────────
    int  rowCount() const;
    void moveRow(int delta);
    void editValue(int delta);
    void activateRow();               // encoder press on a non-edit row
    void ensureVisible(int visibleRows);

    void drawAll();
    void drawHeader();
    void drawFooter(const char* hint);
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
    void drawListRow(int y, bool sel, const char* label, const char* value);
    void drawBar(int x, int y, int w, float v);
    void drawStepCell(uint8_t step, int x, int y, bool cursor, bool playing);
};

// ============================================================================
//  tools/preview/preview.cpp -- render the real panel UI to image files.
//
//  Links the firmware's own ui.cpp, gfx.cpp, sequencer and all seventeen
//  engines on the host, with the hardware (TFT, touch, storage, the sketch's
//  hooks) replaced by the small stand-ins below. Each screen is driven into
//  place through the UI's public input events -- the same calls the encoder,
//  buttons and touch panel make -- and the canvas is dumped as it would be
//  sent to the ILI9341. So the images are not mockups of the panel; they are
//  the panel.
//
//  Build + run:  tools/preview/build.sh   (writes tools/preview/out/*.png)
// ============================================================================
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "ui.h"
#include "panel.h"
#include "midi.h"
#include "dsp_tables.h"
#include "voicealloc.h"

// ── Clock ──────────────────────────────────────────────────────────────────
static unsigned long s_us = 1000000;
unsigned long millis() { return s_us / 1000; }
unsigned long micros() { return s_us; }
void hostAdvanceUs(unsigned long us) { s_us += us; }
void delay(unsigned long ms) { s_us += ms * 1000; }

// ── Sketch state the UI reads ──────────────────────────────────────────────
MidiIO  midi;
volatile float g_cpuLoad = 0.46f;
volatile int   g_activeVoices = 3;
volatile float g_peakL = 0.55f, g_peakR = 0.62f;
volatile uint32_t g_xruns = 0;
int  g_currentPreset = 0;
bool g_recArm = false;
volatile bool g_diskMode = false;
VoiceAlloc va;

Sequencer seqs[NUM_TRACKS];
GlobalSettings gs;
Storage storage;
uint8_t activeTrack = 0;
UI ui;

void uiTogglePlayAll() {}
void uiStopAll() {}
void uiApplySync() {}
void uiSaveAll() {}
void uiLoadFactoryPreset(int idx) { engineLoadPreset((uint8_t)idx); g_currentPreset = idx; }
void uiLoadUserPreset(uint8_t) {}
void uiSaveUserPreset(uint8_t, const char*) {}
void uiLoadSong(uint8_t) {}
void uiSaveSong(uint8_t, const char*) {}
void uiPanic() {}
void uiPreviewNote(uint8_t, bool) {}
void uiSetEngine(EngineId id) { engineSwitchTo(id); }
void uiUsbBegin() {}
void uiUsbEnd() {}
bool uiUsbAvailable() { return true; }
void uiBanksMayHaveChanged() {}
int  uiKbdOctave(int) { return 0; }
void audioFlashBegin() {}
void audioFlashEnd() {}
void seqEmitClock() {}
void seqEmitStart() {}
void seqEmitStop() {}
void seqEmitContinue() {}

// Sequencer output hooks (sequencer.cpp calls these through pointers).
// ── Storage: an empty, mounted filesystem with two named songs ─────────────
bool Storage::begin() { _mounted = true; return true; }
bool Storage::savePattern(uint8_t, uint8_t, const Pattern&) { return true; }
bool Storage::loadPattern(uint8_t, uint8_t, Pattern&) { return false; }
bool Storage::patternExists(uint8_t, uint8_t) { return false; }
bool Storage::saveSettings(const GlobalSettings&) { return true; }
bool Storage::loadSettings(GlobalSettings&) { return false; }
bool Storage::saveUserPreset(uint8_t, uint8_t, const UserPreset&) { return true; }
bool Storage::loadUserPreset(uint8_t, uint8_t, UserPreset&) { return false; }
bool Storage::userPresetExists(uint8_t, uint8_t) { return false; }
bool Storage::saveSong(uint8_t, const Song&) { return true; }
bool Storage::loadSong(uint8_t slot, Song& s) {
    if (slot == 0) { strcpy(s.name, "THE FOG"); return true; }
    if (slot == 2) { strcpy(s.name, "ASSAULT 13"); return true; }
    return false;
}
bool Storage::songExists(uint8_t slot) { return slot == 0 || slot == 2; }
bool Storage::deleteSong(uint8_t) { return true; }
void Storage::formatAll() {}

// ── Panel: capture the flushed bands into an RGB image ─────────────────────
static uint8_t s_rgb[240][320][3];
struct CaptureSink : gfx::Sink {
    void pushRect(int x, int y, int w, int h, const uint16_t* px) override {
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) {
                uint16_t be = px[j * w + i];
                uint16_t c = (uint16_t)((be >> 8) | (be << 8));
                uint8_t* d = s_rgb[y + j][x + i];
                d[0] = (uint8_t)(((c >> 11) & 31) * 255 / 31);
                d[1] = (uint8_t)(((c >> 5) & 63) * 255 / 63);
                d[2] = (uint8_t)((c & 31) * 255 / 31);
            }
    }
};
static CaptureSink s_sink;
void panelBegin() {}
void panelBacklight(uint8_t) {}
gfx::Sink& panelSink() { return s_sink; }
bool touchRaw(uint16_t&, uint16_t&) { return false; }
bool touchRead(int&, int&) { return false; }
bool touchCalLoad() { return true; }
void touchCalSet(const TouchCal&, bool) {}
uint32_t panelFreeHeap() { return 212 * 1024; }
uint32_t panelCpuMhz() { return 240; }

static void dump(const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "out/%s.ppm", name);
    FILE* f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", path); exit(1); }
    fprintf(f, "P6\n320 240\n255\n");
    fwrite(s_rgb, 1, sizeof(s_rgb), f);
    fclose(f);
    printf("  %s\n", name);
}

// Advance past the UI's frame interval and redraw, forcing a full repaint.
static void frame(const char* name) {
    hostAdvanceUs(40000);
    // Park the blink phase on "on" so cursors and lamps show up.
    while ((millis() / 350) & 1) hostAdvanceUs(10000);
    gfx::canvas.invalidate();
    ui.update();
    dump(name);
}

static void demoPattern() {
    Pattern& p = seqs[0].getPattern(0);
    p.length = 16; p.rootNote = 9; p.scaleIdx = 1; p.swing = 0;
    static const uint8_t N[16] = {45,0,0,57,0,0,52,0,45,0,0,55,0,57,0,60};
    static const uint8_t V[16] = {118,0,0,82,0,0,96,0,127,0,0,64,0,74,0,55};
    for (int i = 0; i < 16; i++) {
        Step& s = p.steps[i];
        s = Step();
        s.notes[0] = N[i] ? N[i] : 60; s.velocity = V[i] ? V[i] : 100;
        if (N[i]) s.flags |= ST_ACTIVE;
    }
    p.steps[0].flags |= ST_ACCENT;
    p.steps[3].flags |= ST_SLIDE;
    p.steps[6].ratchet = 3;
    p.steps[8].lockId[0] = 1; p.steps[8].lockVal[0] = 200;
    p.steps[11].probability = 50;
    p.steps[13].flags |= ST_TIE;
    p.steps[15].noteCount = 3;
    for (int t = 1; t < NUM_TRACKS; t++) {
        Pattern& q = seqs[t].getPattern(0);
        q.length = 16;
        for (int i = 0; i < 16; i++) if ((i * (t + 2)) % 5 == 0) q.steps[i].flags |= ST_ACTIVE;
    }
}

int main() {
    dspTablesInit();
    engineSwitchTo(ENGINE_CS80);
    engineLoadPreset(0);
    for (uint8_t t = 0; t < NUM_TRACKS; t++) seqs[t].begin(t);
    demoPattern();
    for (uint8_t t = 0; t < NUM_TRACKS; t++)
        for (uint8_t i = 0; i < CHAIN_LEN; i++) gs.chain[t][i].patternIdx = -1;
    gs.chain[0][0] = { 0, 2 }; gs.chain[0][1] = { 2, 1 }; gs.chain[0][2] = { 1, 4 };
    storage.begin();

    printf("rendering:\n");
    ui.beginDisplay();
    for (int i = 0; i < 4; i++) ui.bootStatus("SEQUENCER");
    dump("00_splash");

    ui.bind(seqs, &activeTrack, &storage, &gs);
    ui.onEncoder(3);                                   // cursor to step 4
    frame("01_main");
    g_recArm = true;
    ui.onEncoder(-3);
    frame("02_main_rec");
    g_recArm = false;

    ui.onPage();                                       // MAIN -> MENU
    frame("03_menu");
    ui.onEncPress();                                   // row 0: STEP EDIT
    frame("04_step_edit");
    ui.onEncoder(6); ui.onEncPress();                  // edit VELOCITY
    frame("05_step_edit_editing");
    ui.onEncPress();
    ui.gotoScreen(Screen::PATTERN_SEL);  frame("06_patterns");
    ui.gotoScreen(Screen::CHAIN_EDIT); ui.onEncoder(1); frame("07_chain");
    ui.gotoScreen(Screen::TOOLS);        frame("08_tools");
    ui.gotoScreen(Screen::ENGINE_SEL); ui.onEncoder(11); frame("09_engine");
    ui.gotoScreen(Screen::SYNTH_PAGES); ui.onEncoder(1); frame("10_synth_pages");
    ui.onEncPress();                     frame("11_synth_edit");
    ui.gotoScreen(Screen::PRESETS); ui.onEncoder(2); frame("12_presets");
    ui.gotoScreen(Screen::MIXER);        frame("13_mixer");
    ui.gotoScreen(Screen::MIDI_SET);     frame("14_midi");
    ui.gotoScreen(Screen::SETTINGS);     frame("15_settings");
    ui.gotoScreen(Screen::PERF);
    for (int i = 0; i < 64; i++) { g_cpuLoad = 0.40f + 0.12f * sinf(i * 0.35f) + 0.03f * (i % 3);
                                   hostAdvanceUs(260000); ui.update(); }
    g_cpuLoad = 0.46f;
    frame("16_perf");
    ui.gotoScreen(Screen::HELP);         frame("17_help");
    ui.gotoScreen(Screen::SONG_BROWSE);  frame("18_songs");
    seqs[0].setMuted(true);
    ui.gotoScreen(Screen::MAIN);         frame("19_main_muted");
    seqs[0].setMuted(false);
    ui.toast("PATTERN SAVED");           frame("20_toast");
    ui.setShift(true);                   frame("21_shift");
    ui.setShift(false);
    gs.phosphor = PHOS_AMBER; ui.applyLook(PHOS_AMBER, true); frame("22_main_amber");
    ui.applyLook(PHOS_BLOOD, true);      frame("23_main_blood");
    ui.applyLook(PHOS_GREEN, false);     frame("24_main_nocrt");
    return 0;
}

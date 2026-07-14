/*******************************************************************
  WEREWOLF — AMY synth + GM SoundFont workstation for ESP32-S3

  Port of the werewolf/ZombieSynth sequencer & synth to:
    * ESP32-S3-WROOM DevKitC (N16R8 recommended)
    * 3.2" ILI9341 320x240 SPI touch screen (XPT2046)
    * PCM5102A I2S DAC
    * 2x CD74HC4067 muxes: 16 pots + 16 momentary switches
    * DIN MIDI in/out (UART1) + native USB MIDI

  Sound engines:
    * WOLF — AMY synthesizer engine: fully controllable 3-oscillator
      8-voice analog-style voice (memory patch) + the whole AMY factory
      bank (128 Juno-106 + 128 DX7 patches) + user presets
    * GM — 16-channel General MIDI via TinySoundFont, playing an
      embedded SoundFont memory-mapped from the "sf2" flash partition

  Sequencing (48 PPQ master clock from AMY, int/ext sync):
    * WOLF SEQ — 4 tracks x 16 steps, 8 patterns (ZombieSynth port)
    * GM SEQ — 16 tracks x 16 steps, 8 patterns + chains (Medusa port)
    * ARP — 50-pattern arpeggiator (ZombieSynth port)

  Everything is wired through a 6-source x 4-destination MIDI router.

  Build: see README.md (Arduino IDE or PlatformIO). Flash a GM
  SoundFont into the sf2 partition with scripts/flash_soundfont.sh.
 *******************************************************************/

#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>

#include "config.h"
#include "theme.h"
#include "ui_common.h"

extern "C" {
#include "amy.h"
}

#include "clock.h"
#include "wolf_synth.h"
#include "wolf_presets.h"
#include "gm_engine.h"
#include "midi_router.h"
#include "arp_engine.h"
#include "wolf_seq.h"
#include "gm_seq.h"
#include "controls.h"
#include "storage.h"

#include "mode_menu.h"
#include "mode_wolf.h"
#include "mode_presets.h"
#include "mode_arp.h"
#include "mode_wolfseq.h"
#include "mode_gmseq.h"
#include "mode_gmmix.h"
#include "mode_route.h"
#include "mode_setup.h"

// ── Global objects ───────────────────────────────────────────────────────────
TFT_eSPI tft = TFT_eSPI();
SPIClass touchSpi(HSPI);
XPT2046_Touchscreen ts(PIN_TOUCH_CS, PIN_TOUCH_IRQ);
TouchState touch;
TouchCal touchCal = { TOUCH_X_MIN, TOUCH_X_MAX, TOUCH_Y_MIN, TOUCH_Y_MAX };

WolfSynth   wolf;
GMEngine    gm;
MidiRouter  router;
MasterClock masterClock;
WolfSeq     wolfSeq;
GMSeq       gmSeq;
ArpEngine   arp;
Panel       panel;
Storage     storage;
#if WW_USB_MIDI_AVAILABLE
USBMIDI     usbMIDI;
#endif

AppMode currentMode = MODE_MENU;

// AMY's I2S plumbing (i2s.c) — we drive it ourselves so the GM engine can be
// mixed into the same stream.
extern "C" {
  amy_err_t esp32_setup_i2s();
  size_t amy_i2s_write(const uint8_t* buffer, size_t nbytes);
}

// ── Hooks ────────────────────────────────────────────────────────────────────
static void midiInputHook(uint8_t* bytes, uint16_t len, uint8_t is_sysex) {
  router.receiveDin(bytes, len, is_sysex);
}

// ── Audio pump (own task): AMY render → GM mix → I2S ────────────────────────
static void audioTask(void*) {
  while (true) {
    int16_t* buf = amy_update();
    gm.render(buf);
    amy_i2s_write((const uint8_t*)buf, AMY_BLOCK_SIZE * AMY_NCHANS * sizeof(int16_t));
  }
}

// ── Clock task: consumes AMY sequencer ticks (48 PPQ) ───────────────────────
static void clockTask(void*) {
  uint32_t processed = 0;
  masterClock.taskHandle = xTaskGetCurrentTaskHandle();
  while (true) {
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5));
    uint32_t now = masterClock.tick;
    while ((int32_t)(now - processed) > 0) {
      processed++;
      wolfSeq.onTick(processed);
      gmSeq.onTick(processed);
      arp.onTick(processed);
      if ((wolfSeq.playing || gmSeq.playing) &&
          router.clockSource == CLK_INTERNAL &&
          (processed % MIDI_CLOCK_DIV) == 0)
        router.sendClockTick();
    }
  }
}

// ── Mode dispatch ────────────────────────────────────────────────────────────
void enterMode(AppMode m) {
  currentMode = m;
  switch (m) {
    case MODE_MENU:    menuInit();    break;
    case MODE_WOLF:    wolfInit();    break;
    case MODE_PRESETS: presetsInit(); break;
    case MODE_ARP:     arpInit();     break;
    case MODE_WSEQ:    wseqInit();    break;
    case MODE_GMSEQ:   gseqInit();    break;
    case MODE_GMMIX:   gmixInit();    break;
    case MODE_ROUTE:   routeInit();   break;
    case MODE_SETUP:   setupInit();   break;
    default: break;
  }
}

static void dispatchMode() {
  switch (currentMode) {
    case MODE_MENU:    menuLoop();    break;
    case MODE_WOLF:    wolfLoop();    break;
    case MODE_PRESETS: presetsLoop(); break;
    case MODE_ARP:     arpLoop();     break;
    case MODE_WSEQ:    wseqLoop();    break;
    case MODE_GMSEQ:   gseqLoop();    break;
    case MODE_GMMIX:   gmixLoop();    break;
    case MODE_ROUTE:   routeLoop();   break;
    case MODE_SETUP:   setupLoop();   break;
    default: break;
  }
}

// ── Panel events ─────────────────────────────────────────────────────────────
static void handlePanel() {
  uint8_t v;
  for (int i = 0; i < NUM_POTS; i++) {
    if (panel.potMoved(i, &v)) {
      wolf.handleCC(POT_CC_MAP[i], v);
      // panel doubles as a MIDI controller on the local channel
      router.send(SRC_LOCAL, 0xB0 | (router.localChannel - 1), POT_CC_MAP[i], v);
    }
  }
  static uint32_t lastTap = 0;
  for (int i = 0; i < NUM_SWITCHES; i++) {
    if (!panel.switchPressed(i)) continue;
    switch ((SwitchAction)i) {
      case SW_OSC1_WAVE: wolf.setOscWave(0, nextWave(wolf.p.osc[0].wave)); wolf.needsRedraw = true; break;
      case SW_OSC2_WAVE: wolf.setOscWave(1, nextWave(wolf.p.osc[1].wave)); wolf.needsRedraw = true; break;
      case SW_OSC3_WAVE: wolf.setOscWave(2, nextWave(wolf.p.osc[2].wave)); wolf.needsRedraw = true; break;
      case SW_FILTER_TYPE:
        wolf.p.filterType = (wolf.p.filterType + 1) % 5;
        wolf.setFilter(); wolf.needsRedraw = true;
        break;
      case SW_LFO_WAVE:
        wolf.p.lfoWave = nextWave(wolf.p.lfoWave);
        wolf.setLFO(); wolf.needsRedraw = true;
        break;
      case SW_ARP_TOGGLE: arp.setEnabled(!arp.enabled); break;
      case SW_ARP_LATCH:  arp.latch = !arp.latch; if (!arp.latch) arp.clearLatch(); break;
      case SW_WSEQ_PLAY:  wolfSeq.toggle(masterClock.tick); wseqDirty = true; break;
      case SW_GMSEQ_PLAY: gmSeq.toggle(masterClock.tick); gseqDirty = true; break;
      case SW_TAP_TEMPO: {
        uint32_t now = millis();
        if (lastTap && now - lastTap > 200 && now - lastTap < 2000)
          masterClock.setBPM((uint16_t)(60000 / (now - lastTap)));
        lastTap = now;
        break;
      }
      case SW_PRESET_PREV:
        applyWolfPreset(wolf, (wolf.currentPreset + NUM_WOLF_PRESETS - 1) % NUM_WOLF_PRESETS);
        wolf.needsRedraw = true; presetsDirty = true;
        break;
      case SW_PRESET_NEXT:
        applyWolfPreset(wolf, (wolf.currentPreset + 1) % NUM_WOLF_PRESETS);
        wolf.needsRedraw = true; presetsDirty = true;
        break;
      case SW_MODE_SYNTH: enterMode(MODE_WOLF); break;
      case SW_MODE_GMSEQ: enterMode(MODE_GMSEQ); break;
      case SW_SHIFT: break;  // modifier, read via panel.shiftHeld()
      case SW_PANIC: router.panic(); break;
    }
  }
}

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.println("WEREWOLF AMY S3 — booting");

  // Panel first (cheap, no dependencies)
  panel.begin();

  // Touch on its own SPI bus
  touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, PIN_TOUCH_CS);
  ts.begin(touchSpi);
  ts.setRotation(1);

  // Display
  pinMode(PIN_TFT_BL, OUTPUT);
  digitalWrite(PIN_TFT_BL, HIGH);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(THEME_BG);
  tft.setTextColor(THEME_PRIMARY, THEME_BG);
  tft.drawCentreString(WEREWOLF_NAME, 160, 80, 4);
  tft.setTextColor(THEME_ACCENT, THEME_BG);
  tft.drawCentreString("AMY + GM SOUNDFONT WORKSTATION", 160, 116, 2);
  tft.setTextColor(THEME_TEXT_DIM, THEME_BG);
  tft.drawCentreString("starting engines...", 160, 150, 2);

  // ── AMY ────────────────────────────────────────────────────────────────
  amy_config_t cfg = amy_default_config();
  cfg.features.default_synths = 0;
  cfg.features.startup_bleep = 0;
  cfg.features.audio_in = 0;
  cfg.platform.multicore = 1;
  cfg.platform.multithread = 1;
  cfg.audio = AMY_AUDIO_IS_NONE;      // we pump I2S ourselves (GM mix-in)
  cfg.i2s_bclk = PIN_I2S_BCLK;
  cfg.i2s_lrc  = PIN_I2S_LRCLK;
  cfg.i2s_dout = PIN_I2S_DOUT;
  cfg.i2s_din  = -1;
  cfg.i2s_mclk = -1;                  // PCM5102A generates its own
  cfg.midi = AMY_MIDI_IS_UART;        // DIN MIDI; USB handled by USBMIDI class
  cfg.midi_uart = MIDI_UART_NUM;
  cfg.midi_in  = PIN_MIDI_IN;
  cfg.midi_out = PIN_MIDI_OUT;
  cfg.max_oscs = 120;
  cfg.max_voices = 24;
  cfg.max_synths = 8;
  cfg.max_memory_patches = 8;
  if (psramFound()) {
    cfg.ram_caps_events = MALLOC_CAP_SPIRAM;
    cfg.ram_caps_synth  = MALLOC_CAP_SPIRAM;
    cfg.ram_caps_delay  = MALLOC_CAP_SPIRAM;
    cfg.ram_caps_sample = MALLOC_CAP_SPIRAM;
    cfg.ram_caps_sysex  = MALLOC_CAP_SPIRAM;
  }
  cfg.amy_external_sequencer_hook = amySequencerHook;
  cfg.amy_external_midi_input_hook = midiInputHook;
  amy_start(cfg);
  esp32_setup_i2s();
  Serial.println("AMY OK");

  // ── engines & plumbing ─────────────────────────────────────────────────
  router.begin();
  storage.loadSettings();
  masterClock.setBPM(masterClock.bpm);

  bool gmOk = gm.begin();
  Serial.println(gmOk ? "GM SoundFont OK" : "GM disabled (no soundfont)");

  wolf.begin();
  applyWolfPreset(wolf, 0);
  gmSeq.begin();
  storage.loadWolfSeq(&wolfSeq);
  storage.loadGMSeq(&gmSeq);
  Serial.println("Engines OK");

  // ── tasks ──────────────────────────────────────────────────────────────
  xTaskCreatePinnedToCore(audioTask, "wwAudio", 6144, nullptr, 15, nullptr, 0);
  xTaskCreatePinnedToCore(clockTask, "wwClock", 4096, nullptr, 8, nullptr, 0);
  Serial.println("Tasks OK");

  delay(300);
  enterMode(MODE_MENU);
  Serial.println("WEREWOLF ready");
}

// ── Main loop (core 1): UI + input ──────────────────────────────────────────
void loop() {
  updateTouch();
  router.pollUsb();
  panel.scan();
  panel.scan();
  handlePanel();
  dispatchMode();
  delay(5);   // ~100 Hz UI/input rate; redraws are change-driven
}

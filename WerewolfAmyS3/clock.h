// WerewolfAmyS3 — master clock
//
// AMY's built-in sequencer is the timebase: 48 PPQ, driven by its own timer
// (or by incoming MIDI clock when external sync is selected — AMY interpolates
// 24 PPQN → 48 PPQ). Its per-tick hook wakes a dedicated FreeRTOS task, which
// steps the arpeggiator and both sequencers and emits MIDI clock out.
#pragma once
#include <Arduino.h>
#include "config.h"

extern "C" {
#include "amy.h"
}

class MasterClock {
public:
  volatile uint32_t tick = 0;         // 48 PPQ, monotonic since boot
  TaskHandle_t taskHandle = nullptr;
  uint16_t bpm = BPM_DEFAULT;

  void setBPM(uint16_t newBpm) {
    bpm = constrain(newBpm, (uint16_t)BPM_MIN, (uint16_t)BPM_MAX);
    amy_event e = amy_default_event();
    e.tempo = (float)bpm;
    amy_add_event(&e);
  }
  void nudgeBPM(int d) { setBPM((int)bpm + d); }

  // called from AMY's sequencer context — keep minimal
  void onAmyTick(uint32_t t) {
    tick = t;
    if (taskHandle) xTaskNotifyGive(taskHandle);
  }
};

extern MasterClock masterClock;

// AMY hook (C signature)
inline void amySequencerHook(uint32_t tick_count) {
  masterClock.onAmyTick(tick_count);
}

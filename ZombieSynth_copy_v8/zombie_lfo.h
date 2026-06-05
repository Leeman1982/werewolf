#ifndef ZOMBIE_LFO_H
#define ZOMBIE_LFO_H

#include <Arduino.h>
#include <math.h>

#define LFO_SAMPLE_RATE 200.0f  // Run at 200Hz, called from UI task

enum LFOWave  { LFO_SINE, LFO_TRIANGLE, LFO_SAW, LFO_SQUARE, LFO_RANDOM };
enum LFOTarget{ LFO_TARGET_FILTER=0, LFO_TARGET_PITCH=1, LFO_TARGET_AMP=2 };

const char* lfoWaveNames[]   = {"SINE","TRI","SAW","SQR","S&H"};
const char* lfoTargetNames[] = {"FILTER","PITCH","AMP"};

class LFOEngine {
public:
  LFOWave   wave     = LFO_SINE;
  LFOTarget target   = LFO_TARGET_FILTER;
  float     rate     = 2.0f;   // Hz
  float     depth    = 0.0f;   // 0-1
  bool      enabled  = false;

  float phase  = 0.0f;
  float output = 0.0f;        // current value -1..+1
  float holdValue = 0.0f;     // for S&H

  void tick() {
    if (!enabled || depth < 0.001f) { output = 0.0f; return; }
    float dt = rate / LFO_SAMPLE_RATE;
    switch (wave) {
      case LFO_SINE:
        output = sinf(phase * 6.28318f);
        break;
      case LFO_TRIANGLE:
        output = (phase < 0.5f) ? (4.0f*phase - 1.0f) : (3.0f - 4.0f*phase);
        break;
      case LFO_SAW:
        output = 2.0f * phase - 1.0f;
        break;
      case LFO_SQUARE:
        output = (phase < 0.5f) ? 1.0f : -1.0f;
        break;
      case LFO_RANDOM:
        if (phase + dt >= 1.0f) holdValue = ((float)random(0,200) / 100.0f) - 1.0f;
        output = holdValue;
        break;
    }
    output *= depth;
    phase += dt;
    if (phase >= 1.0f) phase -= 1.0f;
  }
};

#endif

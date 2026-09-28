#pragma once
// ============================================================================
//  bigmem.h — where the werewolf S3 keeps its large buffers.
//
//  The xtro build kept everything in the RP2350's 520 KB of flat SRAM. The
//  ESP32-S3's internal RAM is smaller once the IDF has its share, and the
//  prebuilt Arduino core does not allow static (.bss) data in PSRAM, so the
//  few buffers that are both large and tolerant of PSRAM latency -- the delay
//  lines, the reverb, the PPG wavetables and the UI canvas -- are allocated
//  from PSRAM once, at boot, instead of being static arrays. The DSP that
//  reads them is unchanged; only where the memory comes from differs.
//
//  PSRAM is therefore required on the S3 (an N16R8 module: 16 MB flash, 8 MB
//  PSRAM). Without it bigAlloc() falls back to internal RAM, which will not
//  hold all of them; the sketch reports that on screen at boot.
//
//  On the host (tests, preview) the buffers stay static arrays, so nothing
//  here is compiled there.
// ============================================================================
#include <stddef.h>

#if defined(ESP_PLATFORM)
  #define WOLF_BIGMEM 1
  #include <esp_heap_caps.h>
  // Zeroed. PSRAM first, internal RAM as a last resort; nullptr if neither.
  static inline void* bigAlloc(size_t n) {
      void* p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
      if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_8BIT);
      return p;
  }
  // Set by any bigAlloc() caller that got nothing: the sketch checks it once
  // after init and puts the reason on screen instead of crashing later.
  extern bool g_bigAllocFailed;
#else
  #define WOLF_BIGMEM 0
#endif

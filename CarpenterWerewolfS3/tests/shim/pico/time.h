#pragma once
// Host stand-in for pico/time.h.  Synth_Dexed uses millis() only to age its
// note-stealing timers, which nothing in the host tests exercises.
#include <stdint.h>
static inline uint64_t get_absolute_time(void) { return 0; }
static inline uint32_t to_ms_since_boot(uint64_t t) { (void)t; return 0; }

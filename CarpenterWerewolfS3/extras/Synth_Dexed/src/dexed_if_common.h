#ifndef _dexed_if_common_h
#define _dexed_if_common_h

#if __has_include(<pico.h>)
#include <pico.h>
#include <pico/time.h>
#define DEXED_PICO_TIME 1
#else
// Werewolf S3 (ESP-IDF underneath arduino-esp32): millis() below comes from
// the IDF's microsecond timer instead of the Pico SDK's.
#include <stdint.h>
#include <stddef.h>
#include <esp_timer.h>
#endif

// This header is pulled into two different kinds of translation units:
//  1. Synth_Dexed's own .cpp files (dexed.cpp etc.) — plain pico-sdk code,
//     no Arduino.h anywhere in that translation unit, so constrain()/millis()
//     must be supplied here (dexed.cpp alone calls constrain() ~90 times and
//     millis() twice).
//  2. MedusaDexed's synth.cpp — which #includes <Arduino.h> (bringing in the
//     real constrain macro and the real millis() from the arduino-pico core)
//     *before* pulling in this library, so redefining them here would
//     conflict.
//
// `ARDUINO` is not a usable discriminator: the Arduino build system passes
// -DARDUINO=<ver> as a global compiler flag to every translation unit in a
// sketch build, library sources included, whether or not that file itself
// includes Arduino.h. So each symbol below is guarded on its own identity
// instead (constrain via #ifndef, since it's a macro either way; millis via
// a macro that only ever shadows a *later* bare call in the same file, and
// nothing after this point in either kind of translation unit calls the
// non-shim millis()).

typedef bool boolean;   // matches Arduino's typedef exactly -- redeclaring an
                         // identical typedef is legal, so no guard needed.

#ifndef constrain
#define constrain(amt, low, high) ({ \
  __typeof__(amt) _amt = (amt); \
  __typeof__(low) _low = (low); \
  __typeof__(high) _high = (high); \
  (_amt < _low) ? _low : ((_amt > _high) ? _high : _amt); \
})
#endif

static inline int32_t signed_saturate_rshift(int32_t val, int bits, int rshift)
{
  int32_t out, max;

  out = val >> rshift;
  max = 1 << (bits - 1);
  if (out >= 0)
  {
    if (out > max - 1) out = max - 1;
  }
  else
  {
    if (out < -max) out = -max;
  }
  return out;
}

#ifndef millis
#ifdef DEXED_PICO_TIME
#define millis() ((uint32_t)to_ms_since_boot(get_absolute_time()))
#else
#define millis() ((uint32_t)(esp_timer_get_time() / 1000))
#endif
#endif

#endif

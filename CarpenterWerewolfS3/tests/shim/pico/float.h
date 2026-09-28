#pragma once
// Host stand-in for pico/float.h -- the vendored arm_math.h includes it for
// float32_t's underlying types and the libm declarations.
#include <stdint.h>
#include <stddef.h>
#include <math.h>

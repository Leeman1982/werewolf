// Minimal Arduino shim so the pure-logic translation units build natively.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <class T, class U, class V> static inline T constrain(T x, U lo, V hi) {
    return (x < (T)lo) ? (T)lo : ((x > (T)hi) ? (T)hi : x);
}
using std::min;
using std::max;

unsigned long millis();
unsigned long micros();
void hostAdvanceUs(unsigned long us);

static inline long randomN(long n) { return n > 0 ? (long)(rand() % n) : 0; }
static inline long random(long n) { return randomN(n); }
static inline long random(long lo, long hi) { return hi > lo ? lo + randomN(hi - lo) : lo; }
static inline void randomSeed(unsigned long s) { srand((unsigned)s); }

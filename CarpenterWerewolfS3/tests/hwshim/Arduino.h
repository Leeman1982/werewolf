// Host-side Arduino shim for SYNTAX-CHECKING the hardware layer only
// (ui.cpp / controls.cpp / display.h against the real U8g2 headers).
// Declarations only -- nothing here is ever linked or run.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include "Print.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

template <class T, class U, class V> static inline T constrain(T x, U lo, V hi) {
    return (x < (T)lo) ? (T)lo : ((x > (T)hi) ? (T)hi : x);
}
using std::min;
using std::max;

#define HIGH 1
#define LOW 0
#define INPUT 0
#define OUTPUT 1
#define INPUT_PULLUP 2
#define INPUT_PULLDOWN 3
#define LSBFIRST 0
#define MSBFIRST 1
#define SERIAL_8N1 0x06
#define CHANGE 1
#define FALLING 2
#define RISING 3
#define PROGMEM
#define A0 26

typedef bool boolean;
typedef uint8_t byte;

unsigned long millis();
unsigned long micros();
void delay(unsigned long);
void delayMicroseconds(unsigned int);
void pinMode(uint8_t, uint8_t);
void digitalWrite(uint8_t, uint8_t);
int  digitalRead(uint8_t);
int  analogRead(uint8_t);
void analogWrite(uint8_t, int);
void analogReadResolution(int);
void attachInterrupt(uint8_t, void (*)(), int);
void detachInterrupt(uint8_t);
uint8_t digitalPinToInterrupt(uint8_t);
void noInterrupts();
void interrupts();
long map(long, long, long, long, long);
long random(long);
long random(long, long);
void randomSeed(unsigned long);
uint16_t makeWord(uint16_t);

// Just enough Serial/String for the call sites the hardware layer makes.
class SerialShim {
public:
    operator bool() const { return true; }
    void begin(unsigned long) {}
    void begin(unsigned long, uint16_t) {}
    void end() {}
    void flush() {}
    size_t print(const char*) { return 0; }
    size_t println(const char*) { return 0; }
    size_t printf(const char*, ...) { return 0; }
    int available() { return 0; }
    int read() { return -1; }
    size_t write(uint8_t) { return 1; }
    size_t write(const uint8_t*, size_t n) { return n; }
};
extern SerialShim Serial;
extern SerialShim Serial1;
extern SerialShim Serial2;

class String {
public:
    String() {}
    String(const char*) {}
    const char* c_str() const { return ""; }
    size_t length() const { return 0; }
};

// arduino-pico core extras the UI's PERF page reads.
#ifndef F_CPU
#define F_CPU 150000000L
#endif
class RP2040Shim {
public:
    int getFreeHeap() { return 0; }
    int getTotalHeap() { return 0; }
    int getUsedHeap() { return 0; }
    void idleOtherCore() {}
    void resumeOtherCore() {}
    void restart() {}
    void reboot() {}
    uint32_t getCycleCount() { return 0; }
    void fifo_push(uint32_t) {}
    uint32_t hwrand32() { return 0; }
};
extern RP2040Shim rp2040;

// Pico SDK bits the sketch uses directly.
#define __no_inline_not_in_flash_func(f) f
#define __not_in_flash_func(f) f
static inline void __dmb() {}
static inline void __dsb() {}
static inline void tight_loop_contents() {}

#pragma once
#include <Arduino.h>
#define OUTPUT_PIN 0
enum { I2S_PHILIPS_MODE = 0, OUTPUT_WITH_MCLK = 1 };
class I2S {
public:
    I2S(int) {}
    bool setBCLK(int) { return true; }
    bool setDATA(int) { return true; }
    bool setMCLK(int) { return true; }
    bool setBitsPerSample(int) { return true; }
    bool setFrequency(int) { return true; }
    bool setBuffers(size_t, size_t) { return true; }
    bool setBuffers(size_t, size_t, int32_t) { return true; }
    bool setLSBJFormat() { return true; }
    bool begin() { return true; }
    void end() {}
    size_t write(int16_t) { return 1; }
    size_t write(int16_t, bool) { return 1; }
    size_t write32(int32_t, bool) { return 1; }
    bool write(int16_t l, int16_t r) { (void)l; (void)r; return true; }
    size_t availableForWrite() { return 0; }
};

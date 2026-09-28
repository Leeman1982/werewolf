#pragma once
#include <stdint.h>
#include <stddef.h>
class TwoWire {
public:
  void begin() {}
  void begin(uint8_t) {}
  void end() {}
  void setSDA(uint8_t) {}
  void setSCL(uint8_t) {}
  void setClock(uint32_t) {}
  void beginTransmission(uint8_t) {}
  uint8_t endTransmission() { return 0; }
  uint8_t endTransmission(uint8_t) { return 0; }
  size_t write(uint8_t) { return 1; }
  size_t write(const uint8_t*, size_t n) { return n; }
  uint8_t requestFrom(uint8_t, uint8_t) { return 0; }
  int available() { return 0; }
  int read() { return -1; }
};
extern TwoWire Wire;
extern TwoWire Wire1;

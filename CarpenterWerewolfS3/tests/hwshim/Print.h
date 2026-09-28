#pragma once
#include <stddef.h>
#include <stdint.h>
class Print {
public:
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t*, size_t) { return 0; }
  size_t print(const char*) { return 0; }
};

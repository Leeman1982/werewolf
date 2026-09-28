#pragma once
#include <Arduino.h>
class SerialPIO : public SerialShim {
public:
    SerialPIO(int, int) {}
    SerialPIO(int, int, int) {}
    static const int NOPIN = -1;
};

#pragma once
#include <Arduino.h>
// Matches the real FatFSUSBClass (libraries/FatFSUSB/src/FatFSUSB.h) closely
// enough for -fsyntax-only: same public method names/signatures. Nothing
// here is ever linked or run, see the banner in Arduino.h.
class FatFSUSBClassShim {
public:
    bool begin() { return true; }
    void end() {}
    void driveReady(bool (*)(uint32_t), uint32_t = 0) {}
    void onPlug(void (*)(uint32_t), uint32_t = 0) {}
    void onUnplug(void (*)(uint32_t), uint32_t = 0) {}
};
extern FatFSUSBClassShim FatFSUSB;

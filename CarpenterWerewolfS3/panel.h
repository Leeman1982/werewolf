#pragma once
// ============================================================================
//  panel.h — the werewolf's display and touch hardware, as ui.cpp sees it.
//
//  3.2" ILI9341 320x240 SPI TFT + XPT2046 resistive touch on its own SPI bus
//  (pins in config.h). Implemented for the board in panel.cpp; the host
//  preview (tools/preview/) supplies its own stand-ins, which is what lets
//  ui.cpp be rendered to PNG without hardware.
// ============================================================================
#include <stdint.h>
#include "gfx.h"

void       panelBegin();                 // SPI, reset, init sequence, backlight on
void       panelBacklight(uint8_t level);  // 0..255
gfx::Sink& panelSink();

// Touch. Calibration is an affine map from raw 12-bit XPT2046 readings to
// screen pixels -- three points solve it exactly, including a rotated or
// mirrored panel, so no per-module axis flags are needed.
struct TouchCal {
    float ax, bx, cx;     // x = ax*rawX + bx*rawY + cx
    float ay, by, cy;     // y = ay*rawX + by*rawY + cy
};
bool touchRaw(uint16_t& rx, uint16_t& ry);   // averaged raw sample; false = not pressed
bool touchRead(int& x, int& y);              // calibrated; false = not pressed
bool touchCalLoad();                          // from NVS; false = never calibrated
void touchCalSet(const TouchCal& c, bool save);

uint32_t panelFreeHeap();
uint32_t panelCpuMhz();

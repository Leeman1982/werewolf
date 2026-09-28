#pragma once
// ============================================================================
//  Carpenter Synth — controls.h
//  The xtro board's physical control set -- rotary encoder (ISR quadrature
//  decode + debounced push), five panel buttons with short/long-press
//  detection, two performance switches (OCT DOWN/UP) and two performance
//  pots (CUTOFF/RESONANCE) -- all OPTIONAL on the werewolf, whose primary
//  input is the touchscreen (ui.h). Any control whose pin is -1 in config.h
//  simply never reports anything.
// ============================================================================
#include <Arduino.h>
#include "config.h"

class RotaryEncoder {
public:
    RotaryEncoder(int8_t pinA, int8_t pinB, int8_t pinSW);
    void begin();
    int  getDelta();            // detents since last call (signed)
    void update();              // poll push switch (call from loop)
    bool wasPressed();          // short press (consumed)
    bool wasLongPress();        // long press (consumed)
    bool isHeld();

    static void isr();

private:
    int8_t _pinA, _pinB, _pinSW;
    volatile int _count = 0;
    volatile uint8_t _last = 0;
    // push switch state
    bool _swRaw = false, _swState = false, _longFired = false;
    bool _pressedFlag = false, _longFlag = false;
    unsigned long _swDebounceT = 0, _swPressT = 0;
};

class Button {
public:
    explicit Button(int8_t pin, bool activeLow = true);
    void begin();
    void update();
    bool wasPressed();
    bool wasReleased();
    bool wasLongPress();
    bool isHeld();

private:
    int8_t _pin; bool _activeLow;
    bool _lastState = false, _state = false;
    bool _pressedFlag = false, _releasedFlag = false;
    bool _longFired = false, _lpReported = false;
    unsigned long _lastDebounce = 0, _pressTime = 0;
};

// A hands-on hardware knob, not menu-mediated: continuously read and
// smoothed against ADC noise, reported as "changed" only once it has moved
// more than a small deadband — a knob sitting still (its usual state) does
// not keep re-asserting the same value every loop tick.
class Pot {
public:
    explicit Pot(int8_t pin);
    void  begin();
    void  update();          // call every loop() tick
    float value() const { return _smoothed; }
    bool  changed();         // true once per meaningful movement since the last call

private:
    int8_t  _pin;
    float   _smoothed = 0.0f;
    float   _lastReported = -1.0f;
};

class Controls {
public:
    Controls();
    void begin();
    void update();

    RotaryEncoder encoder;
    Button play, shift, page, track, mute;
    Button octDown, octUp;
    Pot     cutoffPot, resonancePot;
};

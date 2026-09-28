#include "controls.h"
#include <math.h>

// ─── RotaryEncoder ──────────────────────────────────────────────────────────
// arduino-pico ISRs are void(*)() with no argument; use a static instance ptr.
static RotaryEncoder* _encInstance = nullptr;

RotaryEncoder::RotaryEncoder(uint8_t pinA, uint8_t pinB, uint8_t pinSW)
    : _pinA(pinA), _pinB(pinB), _pinSW(pinSW) {}

void RotaryEncoder::begin() {
    _encInstance = this;
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);
    _last = (digitalRead(_pinA) << 1) | digitalRead(_pinB);
    attachInterrupt(digitalPinToInterrupt(_pinA), isrA, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), isrB, CHANGE);
}

void RotaryEncoder::isrA() {
    RotaryEncoder* e = _encInstance;
    if (!e) return;
    uint8_t a = digitalRead(e->_pinA);
    uint8_t b = digitalRead(e->_pinB);
    uint8_t s = (a << 1) | b;
    if (s != e->_last) {
        if ((e->_last == 0b11 && s == 0b01) || (e->_last == 0b01 && s == 0b00) ||
            (e->_last == 0b00 && s == 0b10) || (e->_last == 0b10 && s == 0b11))
            e->_count++;
        else
            e->_count--;
        e->_last = s;
    }
}
void RotaryEncoder::isrB() { isrA(); }

int RotaryEncoder::getDelta() {
    noInterrupts();
    int d = _count / ENC_TICKS_PER_DETENT;
    _count -= d * ENC_TICKS_PER_DETENT;
    interrupts();
    return d;
}

void RotaryEncoder::update() {
    bool raw = (digitalRead(_pinSW) == LOW);
    unsigned long now = millis();
    if (raw != _swRaw) { _swDebounceT = now; _swRaw = raw; }
    if ((now - _swDebounceT) > DEBOUNCE_MS) {
        if (raw && !_swState) { _swState = true; _swPressT = now; _longFired = false; }
        else if (!raw && _swState) {
            _swState = false;
            if (!_longFired) _pressedFlag = true;
        }
    }
    if (_swState && !_longFired && (now - _swPressT) >= LONG_PRESS_MS) {
        _longFired = true; _longFlag = true;
    }
}

bool RotaryEncoder::wasPressed()   { if (_pressedFlag) { _pressedFlag = false; return true; } return false; }
bool RotaryEncoder::wasLongPress() { if (_longFlag)    { _longFlag = false;    return true; } return false; }
bool RotaryEncoder::isHeld()       { return _swState; }

// ─── Button ─────────────────────────────────────────────────────────────────
Button::Button(uint8_t pin, bool activeLow) : _pin(pin), _activeLow(activeLow) {}

void Button::begin() { pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN); }

void Button::update() {
    uint8_t raw = digitalRead(_pin);
    bool active = _activeLow ? (raw == LOW) : (raw == HIGH);
    unsigned long now = millis();
    if (active != _lastState) _lastDebounce = now;
    _lastState = active;
    if ((now - _lastDebounce) > DEBOUNCE_MS) {
        if (active && !_state) { _state = true; _pressTime = now; _longFired = false; _lpReported = false; }
        else if (!active && _state) {
            _state = false; _releasedFlag = true;
            if (!_longFired) _pressedFlag = true;
        }
    }
    if (_state && !_longFired && (now - _pressTime) >= LONG_PRESS_MS) _longFired = true;
}

bool Button::wasPressed()  { if (_pressedFlag)  { _pressedFlag = false;  return true; } return false; }
bool Button::wasReleased() { if (_releasedFlag) { _releasedFlag = false; return true; } return false; }
bool Button::isHeld()      { return _state; }
bool Button::wasLongPress() {
    if (_longFired && !_lpReported) { _lpReported = true; return true; }
    if (!_state) _lpReported = false;
    return false;
}

// ─── Pot ────────────────────────────────────────────────────────────────────
Pot::Pot(uint8_t pin) : _pin(pin) {}

void Pot::begin() {
    pinMode(_pin, INPUT);
    _smoothed = (float)analogRead(_pin) / POT_ADC_MAX;
    _lastReported = _smoothed;
}

void Pot::update() {
    float raw = (float)analogRead(_pin) / POT_ADC_MAX;
    // One-pole smoothing tames ADC noise without adding audible lag to a
    // knob sweep.
    _smoothed += (raw - _smoothed) * 0.25f;
}

bool Pot::changed() {
    if (fabsf(_smoothed - _lastReported) > POT_DEADBAND) {
        _lastReported = _smoothed;
        return true;
    }
    return false;
}

// ─── Controls ───────────────────────────────────────────────────────────────
Controls::Controls()
    : encoder(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW),
      play(PIN_BTN_PLAY), shift(PIN_BTN_SHIFT), page(PIN_BTN_PAGE),
      track(PIN_BTN_TRACK), mute(PIN_BTN_MUTE),
      octDown(PIN_BTN_OCT_DOWN), octUp(PIN_BTN_OCT_UP),
      cutoffPot(PIN_POT_CUTOFF), resonancePot(PIN_POT_RESONANCE) {}

void Controls::begin() {
    analogReadResolution(POT_ADC_BITS);
    encoder.begin();
    play.begin(); shift.begin(); page.begin(); track.begin(); mute.begin();
    octDown.begin(); octUp.begin();
    cutoffPot.begin(); resonancePot.begin();
}

void Controls::update() {
    encoder.update();
    play.update(); shift.update(); page.update(); track.update(); mute.update();
    octDown.update(); octUp.update();
    cutoffPot.update(); resonancePot.update();
}

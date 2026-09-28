#include "controls.h"
#include <math.h>
#include "soc/gpio_reg.h"

// ─── RotaryEncoder ──────────────────────────────────────────────────────────
// One ISR shared by both encoder pins; attachInterrupt() takes no argument,
// hence the static instance pointer.
static RotaryEncoder* _encInstance = nullptr;
static int8_t s_encA = -1, s_encB = -1;

// Read a GPIO straight from the input registers. The ISR must be IRAM-safe
// (it can fire while a flash write has the cache disabled), and
// digitalRead() is not guaranteed to be in IRAM; a register read is.
static inline IRAM_ATTR uint8_t fastRead(int8_t pin) {
    if (pin < 32) return (uint8_t)((REG_READ(GPIO_IN_REG) >> pin) & 1);
    return (uint8_t)((REG_READ(GPIO_IN1_REG) >> (pin - 32)) & 1);
}

RotaryEncoder::RotaryEncoder(int8_t pinA, int8_t pinB, int8_t pinSW)
    : _pinA(pinA), _pinB(pinB), _pinSW(pinSW) {}

void RotaryEncoder::begin() {
    _encInstance = this;
    s_encA = _pinA; s_encB = _pinB;
    if (_pinSW >= 0) pinMode(_pinSW, INPUT_PULLUP);
    if (_pinA < 0 || _pinB < 0) return;
    pinMode(_pinA, INPUT_PULLUP);
    pinMode(_pinB, INPUT_PULLUP);
    _last = (uint8_t)((fastRead(_pinA) << 1) | fastRead(_pinB));
    attachInterrupt(digitalPinToInterrupt(_pinA), isr, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), isr, CHANGE);
}

void IRAM_ATTR RotaryEncoder::isr() {
    RotaryEncoder* e = _encInstance;
    if (!e) return;
    uint8_t s = (uint8_t)((fastRead(s_encA) << 1) | fastRead(s_encB));
    if (s != e->_last) {
        if ((e->_last == 0b11 && s == 0b01) || (e->_last == 0b01 && s == 0b00) ||
            (e->_last == 0b00 && s == 0b10) || (e->_last == 0b10 && s == 0b11))
            e->_count = e->_count + 1;
        else
            e->_count = e->_count - 1;
        e->_last = s;
    }
}

int RotaryEncoder::getDelta() {
    noInterrupts();
    int d = _count / ENC_TICKS_PER_DETENT;
    _count -= d * ENC_TICKS_PER_DETENT;
    interrupts();
    return d;
}

void RotaryEncoder::update() {
    if (_pinSW < 0) return;
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
Button::Button(int8_t pin, bool activeLow) : _pin(pin), _activeLow(activeLow) {}

void Button::begin() {
    if (_pin < 0) return;
    pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
}

void Button::update() {
    if (_pin < 0) return;
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
Pot::Pot(int8_t pin) : _pin(pin) {}

void Pot::begin() {
    if (!POTS_FITTED || _pin < 0) return;
    pinMode(_pin, INPUT);
    _smoothed = (float)analogRead(_pin) / POT_ADC_MAX;
    _lastReported = _smoothed;
}

void Pot::update() {
    if (!POTS_FITTED || _pin < 0) return;
    float raw = (float)analogRead(_pin) / POT_ADC_MAX;
    // One-pole smoothing tames ADC noise without adding audible lag to a
    // knob sweep.
    _smoothed += (raw - _smoothed) * 0.25f;
}

bool Pot::changed() {
    if (!POTS_FITTED || _pin < 0) return false;
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
    if (POTS_FITTED) analogReadResolution(POT_ADC_BITS);
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

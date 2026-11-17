#include "../core/Config.h"

#if F_USE_STEPPER

#include "A4988.h"

namespace fn {

bool A4988::begin(uint8_t stepPin, uint8_t dirPin,
                   int8_t enablePin,
                   int8_t ms1, int8_t ms2, int8_t ms3) {
    _step.init(stepPin, OUTPUT); _step.low();
    _dirPin.init(dirPin, OUTPUT); _dirPin.low();

    if (enablePin >= 0) {
        _en.init(uint8_t(enablePin), OUTPUT);
        _en.high();   // disabled
        _hasEn = true;
    }
    if (ms1 >= 0) { _ms1.init(uint8_t(ms1), OUTPUT); _hasMs[0] = true; }
    if (ms2 >= 0) { _ms2.init(uint8_t(ms2), OUTPUT); _hasMs[1] = true; }
    if (ms3 >= 0) { _ms3.init(uint8_t(ms3), OUTPUT); _hasMs[2] = true; }

    _enabled = false;
    return _step.valid() && _dirPin.valid();
}

bool A4988::step(bool forward) {
    if (!_enabled) return false;
    _dir = forward;
    _dirPin.write(forward);

    _step.high();
    if (_pulseUs) delayUs(_pulseUs);
    _step.low();
    if (_pulseUs) delayUs(_pulseUs);

    _pos += forward ? 1 : -1;
    return true;
}

void A4988::enable(bool on) {
    if (_hasEn) {
        _en.write(!on);   // active-low
        if (on) delayUs(_enableDelayUs);
    }
    _enabled = on;
}

void A4988::setMicrostep(Microstep m) {
    if (_hasMs[0]) _ms1.write(m == Microstep::Half || m == Microstep::Eighth || m == Microstep::Sixteenth);
    if (_hasMs[1]) _ms2.write(m == Microstep::Quarter || m == Microstep::Eighth || m == Microstep::Sixteenth);
    if (_hasMs[2]) _ms3.write(m == Microstep::Sixteenth);
    _microstep = m;
}

}  // namespace fn

#endif  // F_USE_STEPPER

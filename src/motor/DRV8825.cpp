#include "../core/Config.h"

#if F_USE_STEPPER && F_USE_DRV8825

#include "DRV8825.h"

namespace fn {

bool DRV8825::begin(uint8_t stepPin, uint8_t dirPin,
                    int8_t enablePin,
                    int8_t mode0,
                    int8_t mode1,
                    int8_t mode2) {
    _step.init(stepPin, OUTPUT);
    _step.low();

    _dirPin.init(dirPin, OUTPUT);
    _dirPin.low();

    if (enablePin >= 0) {
        _en.init(uint8_t(enablePin), OUTPUT);
        _en.high();   // disabled
        _hasEn = true;
    }

    if (mode0 >= 0) {
        _mode0.init(uint8_t(mode0), OUTPUT);
        _mode0.low();
        _hasMode[0] = true;
    }

    if (mode1 >= 0) {
        _mode1.init(uint8_t(mode1), OUTPUT);
        _mode1.low();
        _hasMode[1] = true;
    }

    if (mode2 >= 0) {
        _mode2.init(uint8_t(mode2), OUTPUT);
        _mode2.low();
        _hasMode[2] = true;
    }

    _enabled = false;
    return _step.valid() && _dirPin.valid();
}

bool DRV8825::step(bool forward) {
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

void DRV8825::enable(bool on) {
    if (_hasEn) {
        _en.write(!on);   // active-low
        if (on) delayUs(_enableDelayUs);
    }
    _enabled = on;
}

void DRV8825::setMicrostep(Microstep m) {
    bool m0 = false;
    bool m1 = false;
    bool m2 = false;
    microstepToPins(m, m0, m1, m2);

    if (_hasMode[0]) _mode0.write(m0);
    if (_hasMode[1]) _mode1.write(m1);
    if (_hasMode[2]) _mode2.write(m2);

    _microstep = m;
}

}  // namespace fn

#endif  // F_USE_STEPPER && F_USE_DRV8825

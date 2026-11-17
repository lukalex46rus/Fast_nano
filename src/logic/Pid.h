#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Конфигурация PID (битовые флаги).
enum PidConfig : uint8_t {
    PidPInput     = 1u << 0,   // пропорционально входу (интегрирующие процессы)
    PidKiInside   = 1u << 1,   // Ki внутри интеграла
    PidBackCalc   = 1u << 2,   // back calculation
    PidSaturate   = 1u << 3,   // conditional integration
    PidResetI     = 1u << 4,   // сброс интеграла при достижении уставки
    PidDInput     = 1u << 5,   // дифференциал по входу
    PidReverse    = 1u << 6,   // обратное направление
};

// PID-регулятор. Cfg — constexpr-флаги. T = float по умолчанию.
template <uint8_t Cfg = 0, typename T = float>
class Pid {
   public:
    Pid(T kp = 0, T ki = 0, T kd = 0, uint16_t dtMs = 30) {
        setDtMs(dtMs);
        setGains(kp, ki, kd);
    }

    void setGains(T kp, T ki, T kd) {
        _kp = kp;
        _ki = ki * _dt;
        _kd = kd / _dt;
    }
    void setDtMs(uint16_t dtMs) {
        if (!dtMs) dtMs = 1;
        T dt = dtMs / T(1000);
        T r = dt / _dt;
        _dt = dt;
        _ki *= r;
        _kd /= r;
    }
    void setOutputLimits(T min, T max) { _min = min; _max = max; }
    void setSetpoint(T sp) { _sp = sp; }
    T    setpoint() const { return _sp; }
    void reset() { _integral = 0; _prev = 0; _first = true; }

    T compute(T input) {
        T error = _sp - input;
        T deriv = (Cfg & PidDInput) ? (_prev - input) : (error - _prev);
        _prev = (Cfg & PidDInput) ? input : error;
        if (_first) { _first = false; deriv = 0; }

        if (Cfg & PidResetI) {
            bool atSp = (Cfg & PidReverse) ? (input <= _sp) : (input >= _sp);
            if (atSp) _integral = 0;
        }

        T prop = (Cfg & PidPInput)
               ? ((Cfg & PidReverse) ? _kp * input : -_kp * input)
               : _kp * error;
        T integ = (Cfg & PidKiInside) ? _integral : _ki * _integral;
        T out   = prop + integ + _kd * deriv;

        if (out >= _max) {
            if ((Cfg & PidBackCalc) && _kbc) error += (_max - out) * _kbc;
            out = _max;
            if ((Cfg & PidSaturate) && error > 0) return out;
        } else if (out <= _min) {
            if ((Cfg & PidBackCalc) && _kbc) error += (_min - out) * _kbc;
            out = _min;
            if ((Cfg & PidSaturate) && error < 0) return out;
        }
        _integral += (Cfg & PidKiInside) ? (_ki * error) : error;
        return out;
    }

    void setBackCalc(T kbc) { _kbc = kbc; }

   private:
    T _kp = 0, _ki = 0, _kd = 0;
    T _sp = 0, _integral = 0, _prev = 0;
    T _dt = T(0.03);
    T _min = 0, _max = 255;
    T _kbc = 0;
    bool _first = true;
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC

#pragma once
// Автотюнинг PID методом релейного эксперимента (Åström–Hägglund)
// с расчётом коэффициентов по Ziegler–Nichols.
//
// Схема:
//   1. setOutputLimits(min, max) — пределы ШИМ/ЦАП.
//   2. begin(setpoint, hysteresis, timeoutMs, cycles).
//   3. В цикле с постоянным периодом: output = tune.update(sensor);
//      полученное значение подаётся на исполнительный механизм.
//   4. tune.isFinished() -> tune.succeeded() -> tune.getGains(kp, ki, kd).
//
// Реле переключается между min и max при пересечении уставки с гистерезисом.
// По периодам и амплитуде колебаний вычисляются Ku и Tu, затем gains.

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC && F_USE_PID_AUTOTUNE

#include "../timer/Tick.h"

#ifndef F_PID_AUTOTUNE_MAX_CYCLES
#define F_PID_AUTOTUNE_MAX_CYCLES 8
#endif

namespace fn {
namespace logic {

class PidAutoTuner {
   public:
    enum class State : uint8_t { Idle, Running, Done, Failed };
    enum class Rule : uint8_t {
        ZieglerNichols,    // classic PID: Kp=0.6Ku, Ti=0.5Tu, Td=0.125Tu
        ZieglerNicholsPI,  // PI:          Kp=0.45Ku, Ti=0.83Tu
        TyreusLuyben,      // PID:         Kp=0.45Ku, Ti=2.2Tu, Td=Tu/6.3
    };

    void setOutputLimits(float minOut, float maxOut) {
        _outMin = minOut;
        _outMax = maxOut;
    }

    void begin(float setpoint, float hysteresis, uint32_t timeoutMs = 60000,
               uint8_t cycles = 3) {
        _sp = setpoint;
        _hyst = (hysteresis < 0.0f) ? -hysteresis : hysteresis;
        _timeout = timeoutMs;
        if (cycles < 1) cycles = 1;
        if (cycles > F_PID_AUTOTUNE_MAX_CYCLES) cycles = F_PID_AUTOTUNE_MAX_CYCLES;
        _cycles = cycles;
        _maxPoints = uint8_t(2 * _cycles + 1);
        _n = 0;
        _t0 = fn::millis();
        _ku = 0.0f;
        _tu = 0.0f;
        _started = false;
        _relayHigh = false;
        _out = 0.0f;
        _lastOut = 0.0f;
        _state = (_outMax > _outMin) ? State::Running : State::Failed;
    }

    // Вызывать с постоянным периодом. Возвращает управляющее воздействие.
    float update(float input) {
        if (_state == State::Idle) return 0.0f;
        if (_state != State::Running) return _lastOut;

        uint32_t now = fn::millis();
        if (_timeout && uint32_t(now - _t0) >= _timeout) {
            _state = State::Failed;
            return _lastOut;
        }

        if (!_started) {
            _started = true;
            _relayHigh = (input < _sp);
            _out = _relayHigh ? _outMax : _outMin;
        }

        if (_relayHigh) {
            if (input > _sp + _hyst) {
                _relayHigh = false;
                _out = _outMin;
                record(input);
            }
        } else {
            if (input < _sp - _hyst) {
                _relayHigh = true;
                _out = _outMax;
                record(input);
            }
        }

        _lastOut = _out;
        return _out;
    }

    bool isFinished() const { return _state == State::Done || _state == State::Failed; }
    bool succeeded() const { return _state == State::Done; }
    State state() const { return _state; }

    float ku() const { return _ku; }   // ultimate gain
    float tu() const { return _tu; }   // ultimate period, seconds

    bool getGains(float& kp, float& ki, float& kd,
                  Rule rule = Rule::ZieglerNichols) const {
        if (_state != State::Done) return false;
        if (_ku <= 0.0f || _tu <= 0.0f) return false;

        float ti = 0.0f;
        float td = 0.0f;
        switch (rule) {
            case Rule::ZieglerNichols:
                kp = 0.6f * _ku;
                ti = 0.5f * _tu;
                td = 0.125f * _tu;
                break;
            case Rule::ZieglerNicholsPI:
                kp = 0.45f * _ku;
                ti = 0.83f * _tu;
                td = 0.0f;
                break;
            case Rule::TyreusLuyben:
                kp = 0.45f * _ku;
                ti = 2.2f * _tu;
                td = _tu / 6.3f;
                break;
            default:
                return false;
        }

        ki = (ti > 0.0f) ? (kp / ti) : 0.0f;
        kd = kp * td;
        return true;
    }

    void cancel() {
        _state = State::Idle;
        _out = 0.0f;
        _lastOut = 0.0f;
    }

    uint8_t cycles() const { return _cycles; }

   private:
    void record(float input) {
        if (_n >= _maxPoints) return;
        _peak[_n] = input;
        _time[_n] = fn::millis();
        _n++;
        if (_n >= _maxPoints) finish();
    }

    void finish() {
        uint8_t points = _n;
        if (points < 3) {
            _state = State::Failed;
            return;
        }
        uint8_t periods = uint8_t((points - 1) / 2);
        if (!periods) {
            _state = State::Failed;
            return;
        }

        uint32_t total = uint32_t(_time[points - 1] - _time[0]);
        _tu = (float(total) / 1000.0f) / float(periods);

        float mn = _peak[0];
        float mx = _peak[0];
        for (uint8_t i = 1; i < points; i++) {
            if (_peak[i] < mn) mn = _peak[i];
            if (_peak[i] > mx) mx = _peak[i];
        }

        float a = (mx - mn) * 0.5f;              // амплитуда колебаний
        float d = (_outMax - _outMin) * 0.5f;    // амплитуда реле
        if (a <= 0.0001f || d <= 0.0f || _tu <= 0.0f) {
            _state = State::Failed;
            return;
        }

        _ku = (4.0f * d) / (3.14159265f * a);
        _state = State::Done;
    }

    float _sp = 0.0f;
    float _hyst = 0.0f;
    float _outMin = 0.0f;
    float _outMax = 255.0f;
    float _out = 0.0f;
    float _lastOut = 0.0f;
    float _ku = 0.0f;
    float _tu = 0.0f;

    uint32_t _timeout = 60000;
    uint32_t _t0 = 0;
    uint32_t _time[2 * F_PID_AUTOTUNE_MAX_CYCLES + 1] = {};
    float    _peak[2 * F_PID_AUTOTUNE_MAX_CYCLES + 1] = {};

    uint8_t _cycles = 3;
    uint8_t _maxPoints = 7;
    uint8_t _n = 0;
    bool    _started = false;
    bool    _relayHigh = false;
    State   _state = State::Idle;
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC && F_USE_PID_AUTOTUNE

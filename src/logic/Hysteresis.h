#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Гистерезис: state() переключается только при выходе за границы.
// Например: low=250, high=300. При значении <250 → state=false,
// при >300 → state=true, между — не меняется.
template <typename T>
class Hysteresis {
   public:
    Hysteresis(T low, T high, bool initialState = false)
        : _low(low), _high(high), _state(initialState) {
        if (_low > _high) { T t = _low; _low = _high; _high = t; }
    }

    // true — состояние изменилось.
    bool update(T value) {
        bool prev = _state;
        if (_state) {
            if (value < _low) _state = false;
        } else {
            if (value > _high) _state = true;
        }
        return _state != prev;
    }

    bool state() const { return _state; }
    T low()  const { return _low; }
    T high() const { return _high; }
    void setThresholds(T low, T high) {
        _low = low; _high = high;
        if (_low > _high) { T t = _low; _low = _high; _high = t; }
    }
    void reset(bool s = false) { _state = s; }

   private:
    T _low, _high;
    bool _state;
};

// Специальный случай для кнопки/датчика: active-low вход.
// Используйте только если нужен простой гистерезис без антидребезга —
// для кнопок лучше fn::logic::Button.
using HysteresisInt = Hysteresis<int32_t>;

}  // namespace logic
}  // namespace fn

#endif

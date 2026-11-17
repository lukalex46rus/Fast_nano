#pragma once
#include <stdint.h>

#include "../core/Features.h"
#include "../timer/Tick.h"

namespace fn {
namespace logic {

// Универсальный антидребезг для произвольного значения.
// update(raw) возвращает true, когда стабильное значение изменилось.
// Время хранится в младших 16 битах millis(), поэтому окно ограничено
// ~65 секундами — этого достаточно для кнопок и клавиатур.
template <typename T>
class DebounceValue {
   public:
    explicit DebounceValue(uint16_t windowMs = 20, T initial = T())
        : _windowMs(windowMs),
          _stable(initial),
          _raw(initial),
          _since(uint16_t(millis())) {}

    void setWindow(uint16_t ms) { _windowMs = ms; }
    uint16_t window() const { return _windowMs; }

    // Принять новое измерение. true — стабильное значение изменилось.
    bool update(T raw) {
        uint16_t now = uint16_t(millis());

        if (raw != _raw) {
            _raw = raw;
            _since = now;
        }

        if (raw != _stable && uint16_t(now - _since) >= _windowMs) {
            _stable = raw;
            return true;
        }

        return false;
    }

    T state() const { return _stable; }
    T raw() const { return _raw; }

    // Сбросить в значение сразу, без ожидания окна.
    void reset(T value = T()) {
        _raw = value;
        _stable = value;
        _since = uint16_t(millis());
    }

    // Принудительно установить стабильное значение (например, из ISR).
    void force(T value) {
        _raw = value;
        _stable = value;
        _since = uint16_t(millis());
    }

   private:
    uint16_t _windowMs;
    T _stable;
    T _raw;
    uint16_t _since;
};

using DebounceCore = DebounceValue<bool>;

}  // namespace logic
}  // namespace fn

// DebounceValue доступен всегда: он не зависит от пинов и нужен модулям
// вне F_USE_LOGIC (например, input/Keypad4x4.h). Привязка к пину — ниже.
#if F_USE_LOGIC

#include "../pin/PinRef.h"

namespace fn {
namespace logic {

// Готовый дебаунсер для кнопки на пине.
// tick() возвращает true, если стабильное состояние изменилось.
template <uint16_t WindowMs = 20>
class Debouncer {
   public:
    Debouncer(uint8_t pin, bool activeLow = true)
        : _pin(pin, activeLow ? INPUT_PULLUP : INPUT),
          _activeLow(activeLow) {
        _deb.reset(readRaw());
    }

    bool tick() { return _deb.update(readRaw()); }

    bool state() const { return _deb.state(); }

   private:
    bool readRaw() const {
        bool v = _pin.read();
        return _activeLow ? !v : v;
    }

    PinRef _pin;
    bool _activeLow;
    DebounceValue<bool> _deb{WindowMs};
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC

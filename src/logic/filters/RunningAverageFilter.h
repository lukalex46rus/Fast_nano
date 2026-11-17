#pragma once
// Экспоненциальное бегущее среднее с необязательным периодом обновления.
// update() — на каждый вызов, updateTimed() — не чаще периода в миллисекундах.

#include <stdint.h>

#include "../../core/Features.h"
#include "../../timer/Tick.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

class RunningAverageFilter {
   public:
    RunningAverageFilter() = default;

    explicit RunningAverageFilter(float coefficient, uint16_t periodMs = 0) {
        setCoefficient(coefficient);
        setPeriod(periodMs);
    }

    // Коэффициент 0..1: чем меньше, тем сильнее сглаживание.
    void setCoefficient(float coefficient) {
        if (coefficient < 0.0f) coefficient = 0.0f;
        if (coefficient > 1.0f) coefficient = 1.0f;
        _coefficient = coefficient;
    }

    float coefficient() const { return _coefficient; }

    void setPeriod(uint16_t periodMs) { _period = periodMs; }
    uint16_t period() const { return _period; }

    // Обновить немедленно.
    float update(float value) {
        _value += (value - _value) * _coefficient;
        return _value;
    }

    // Обновить, если прошёл период. Возвращает текущее значение.
    float updateTimed(float value) {
        if (_period == 0) return update(value);

        uint32_t now = fn::millis();
        uint32_t elapsed = now - _updatedAt;
        if (elapsed >= _period) {
            _updatedAt += _period;
            // После долгой паузы не догоняем пропущенные периоды.
            if (now - _updatedAt >= _period) _updatedAt = now;
            update(value);
        }
        return _value;
    }

    float value() const { return _value; }

    void reset(float value = 0.0f) {
        _value = value;
        _updatedAt = fn::millis();
    }

   private:
    float _coefficient = 0.0f;
    float _value = 0.0f;
    uint32_t _updatedAt = 0;
    uint16_t _period = 0;
};

}  // namespace logic
}  // namespace fn

#endif

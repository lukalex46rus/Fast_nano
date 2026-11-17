#pragma once
// Быстрый целочисленный экспоненциальный фильтр (сдвиг вместо умножения).
// smoothing = 0..31: чем больше значение, тем сильнее сглаживание.
// Работает по таймеру fn::millis().

#include <stdint.h>

#include "../../core/Features.h"
#include "../../timer/Tick.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Режим пропуска пиков.
enum class ExponentialPassMode : uint8_t {
    None = 0,
    Maximum,
    Minimum,
};

class FastExponentialFilter {
   public:
    explicit FastExponentialFilter(uint8_t smoothing = 20, uint16_t periodMs = 0) {
        setSmoothing(smoothing);
        setPeriod(periodMs);
    }

    // Коэффициент сглаживания 0..31 (значения выше 31 обрезаются).
    void setSmoothing(uint8_t smoothing) {
        if (smoothing > 31) smoothing = 31;
        _smoothing = smoothing;
        _inputWeight = uint8_t(32 - smoothing);
    }

    uint8_t smoothing() const { return _smoothing; }

    // Период обновления, мс. 0 — обновлять при каждом вызове update().
    void setPeriod(uint16_t periodMs) { _period = periodMs; }
    uint16_t period() const { return _period; }

    void setPassMode(ExponentialPassMode mode) { _passMode = mode; }
    ExponentialPassMode passMode() const { return _passMode; }

    // Текущее входное значение.
    void setInput(long value) { _input = value; }
    long input() const { return _input; }

    // Начальное значение фильтра.
    void setValue(long value) { _filtered = value; }
    long value() const { return _filtered; }

    // Пропустить пик: при режиме Maximum/Minimum новое значение
    // заменяет текущее, если оно выше/ниже. Возвращает true при замене.
    bool applyPeak(long value) {
        if (_passMode == ExponentialPassMode::Maximum && value > _filtered) {
            _filtered = value;
            return true;
        }
        if (_passMode == ExponentialPassMode::Minimum && value < _filtered) {
            _filtered = value;
            return true;
        }
        return false;
    }

    // Обновление по внутреннему таймеру.
    void update() {
        if (_period == 0 || fn::millis() - _updatedAt >= _period) {
            _updatedAt = fn::millis();
            updateNow();
        }
    }

    // Обновление немедленно.
    void updateNow() {
        _filtered = (long(_smoothing) * _filtered + long(_inputWeight) * _input) >> 5;
    }

   private:
    uint32_t _updatedAt = 0;
    uint16_t _period = 0;
    uint8_t _smoothing = 20;
    uint8_t _inputWeight = 12;
    ExponentialPassMode _passMode = ExponentialPassMode::None;
    long _filtered = 0;
    long _input = 0;
};

}  // namespace logic
}  // namespace fn

#endif

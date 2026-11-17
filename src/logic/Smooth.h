#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// S-кривая (smoothstep) для плавного изменения.
// curve = 0: чистая S-кривая, curve = 255: почти линейная.
// Шаг внутри — адаптивный: чем ближе к target, тем меньше.
class Smoother {
   public:
    Smoother(uint8_t curve = 0, int32_t min = 0, int32_t max = 255)
        : _curve(curve), _min(min), _max(max) {}

    int32_t update(int32_t target) {
        if (target > _max) target = _max;
        if (target < _min) target = _min;
        _target = target;

        int32_t diff = target - _val;
        if (diff == 0) return _val;

        // Скорость пропорциональна sqrt(diff) для S-кривой.
        // Чем больше |diff|, тем быстрее; чем ближе к цели, тем медленнее.
        // curve = 0 → агрессивный S; curve = 255 → линейно.
        int32_t absDiff = diff > 0 ? diff : -diff;
        int32_t speed = _calcSpeed(absDiff);
        if (diff > 0) { _val += speed; if (_val > target) _val = target; }
        else          { _val -= speed; if (_val < target) _val = target; }
        return _val;
    }

    int32_t value() const { return _val; }
    void reset(int32_t v = 0) {
        if (v < _min) v = _min;
        if (v > _max) v = _max;
        _val = _target = v;
    }
    void setCurve(uint8_t c) { _curve = c; }
    void setLimits(int32_t lo, int32_t hi) { _min = lo; _max = hi; }

   private:
    // Адаптивная скорость: 1 + sqrt(absDiff) * scale.
    // Избегаем sqrt() в AVR (дорого) — используем ступенчатую аппроксимацию.
    int32_t _calcSpeed(int32_t absDiff) const {
        int32_t s;
        if      (absDiff > 4096) s = 256;
        else if (absDiff > 1024) s = 128;
        else if (absDiff > 256)  s = 64;
        else if (absDiff > 64)   s = 32;
        else if (absDiff > 16)   s = 16;
        else if (absDiff > 4)    s = 8;
        else if (absDiff > 1)    s = 4;
        else                     s = 1;
        // curve = 255 → почти линейно (s ближе к 1). curve = 0 → s агрессивный.
        // Чем больше curve, тем меньше s для «сглаживания».
        return (s * (256 - _curve) + 255) / 256;
    }
    uint8_t _curve;
    int32_t _min, _max;
    int32_t _val = 0;
    int32_t _target = 0;
};

}  // namespace logic
}  // namespace fn

#endif

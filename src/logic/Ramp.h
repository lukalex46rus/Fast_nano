#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Линейный ramp: update(target) двигает текущее значение к target с шагом step.
// Типично: плавный пуск мотора, плавное изменение яркости.
class Ramp {
   public:
    Ramp(int32_t step = 1, int32_t min = -32768, int32_t max = 32767)
        : _step(step), _min(min), _max(max) {}

    // Двигает текущее значение к target. Возвращает новое текущее.
    int32_t update(int32_t target) {
        if (target > _max) target = _max;
        if (target < _min) target = _min;
        _target = target;
        if (_val < target) {
            _val += _step;
            if (_val > target) _val = target;
        } else if (_val > target) {
            _val -= _step;
            if (_val < target) _val = target;
        }
        return _val;
    }

    int32_t value()  const { return _val; }
    bool    atTarget() const { return _val == _target; }
    int32_t target() const { return _target; }

    void reset(int32_t v = 0) {
        if (v < _min) v = _min;
        if (v > _max) v = _max;
        _val = _target = v;
    }

    void setStep(int32_t s) { _step = s > 0 ? s : 1; }
    void setLimits(int32_t lo, int32_t hi) { _min = lo; _max = hi; }

   private:
    int32_t _step;
    int32_t _min, _max;
    int32_t _val = 0;
    int32_t _target = 0;
};

}  // namespace logic
}  // namespace fn

#endif

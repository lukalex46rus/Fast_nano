#pragma once
#include <stdint.h>

#include "../../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Экспоненциальное сглаживание: alpha = 1 / 2^shift.
// shift = 4 — alpha 0.0625 (сильное сглаживание), shift = 0 — без сглаживания.
class ExponentialAverage {
   public:
    explicit ExponentialAverage(uint8_t shift = 4) {
        setShift(shift);
    }

    void setShift(uint8_t shift) {
        _shift = (shift > 31) ? 31 : shift;
    }

    uint8_t shift() const { return _shift; }

    int32_t update(int32_t value) {
        _value += (value - _value) >> _shift;
        return _value;
    }

    int32_t value() const { return _value; }

    void reset(int32_t value = 0) { _value = value; }

   private:
    uint8_t _shift = 4;
    int32_t _value = 0;
};

}  // namespace logic
}  // namespace fn

#endif

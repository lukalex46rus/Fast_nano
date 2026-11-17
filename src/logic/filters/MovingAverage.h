#pragma once
#include <stdint.h>

#include "../../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Скользящее среднее по N отсчётам.
template <uint8_t N>
class MovingAverage {
    static_assert(N > 0, "MovingAverage: N must be > 0");

   public:
    explicit MovingAverage(int32_t initial = 0) {
        reset(initial);
    }

    int32_t update(int32_t value) {
        _sum += value - _samples[_index];
        _samples[_index] = value;
        if (++_index >= N) _index = 0;
        _value = _sum / N;
        return _value;
    }

    int32_t value() const { return _value; }

    void reset(int32_t value = 0) {
        for (uint8_t i = 0; i < N; i++) _samples[i] = value;
        _sum = value * int32_t(N);
        _index = 0;
        _value = value;
    }

   private:
    int32_t _samples[N] = {};
    int32_t _sum = 0;
    int32_t _value = 0;
    uint8_t _index = 0;
};

// Бегущее среднее с кольцевым буфером. Сумма поддерживается инкрементально,
// поэтому один отсчёт стоит O(1) без пересчёта всего окна.
template <typename TYPE, uint8_t N>
class RingAverageFilter {
    static_assert(N > 0, "RingAverageFilter: N must be > 0");

   public:
    TYPE update(TYPE value) {
        _sum -= _samples[_index];
        _sum += value;
        _samples[_index] = value;
        if (++_index >= N) _index = 0;
        _value = TYPE(_sum / N);
        return _value;
    }

    float updateFloat(TYPE value) {
        update(value);
        return float(_sum) / float(N);
    }

    TYPE value() const { return _value; }

    void reset() {
        for (uint8_t i = 0; i < N; i++) _samples[i] = TYPE();
        _sum = TYPE();
        _index = 0;
        _value = TYPE();
    }

   private:
    TYPE _samples[N] = {};
    TYPE _sum = TYPE();
    TYPE _value = TYPE();
    uint8_t _index = 0;
};

}  // namespace logic
}  // namespace fn

#endif

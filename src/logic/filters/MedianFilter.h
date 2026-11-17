#pragma once
#include <stdint.h>

#include "../../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

// Медианный фильтр по трём отсчётам. Отсекает одиночные выбросы.
template <typename TYPE>
class MedianFilter3 {
   public:
    TYPE update(TYPE value) {
        _samples[_index] = value;
        if (++_index >= 3) _index = 0;
        _value = median(_samples[0], _samples[1], _samples[2]);
        return _value;
    }

    TYPE value() const { return _value; }

    void reset(TYPE value = TYPE()) {
        _samples[0] = _samples[1] = _samples[2] = value;
        _index = 0;
        _value = value;
    }

   private:
    static TYPE median(TYPE a, TYPE b, TYPE c) {
        if ((a <= b && b <= c) || (c <= b && b <= a)) return b;
        if ((b <= a && a <= c) || (c <= a && a <= b)) return a;
        return c;
    }

    TYPE _samples[3] = {};
    TYPE _value = TYPE();
    uint8_t _index = 0;
};

// Медианный фильтр по N отсчётам. Окно намеренно небольшое:
// сортировка выполняется на стеке при каждом отсчёте.
template <uint8_t N, typename TYPE>
class MedianFilter {
    static_assert(N > 0, "MedianFilter: N must be > 0");
    static_assert(N <= 15, "MedianFilter: N should be <= 15");

   public:
    TYPE update(TYPE value) {
        _samples[_index] = value;
        if (++_index >= N) _index = 0;

        TYPE sorted[N];
        for (uint8_t i = 0; i < N; i++) sorted[i] = _samples[i];
        sort(sorted);
        _value = sorted[N / 2];
        return _value;
    }

    TYPE value() const { return _value; }

    void reset(TYPE value = TYPE()) {
        for (uint8_t i = 0; i < N; i++) _samples[i] = value;
        _index = 0;
        _value = value;
    }

   private:
    static void sort(TYPE* values) {
        for (uint8_t i = 1; i < N; i++) {
            TYPE value = values[i];
            int8_t j = int8_t(i) - 1;
            while (j >= 0 && values[j] > value) {
                values[j + 1] = values[j];
                j--;
            }
            values[j + 1] = value;
        }
    }

    TYPE _samples[N] = {};
    TYPE _value = TYPE();
    uint8_t _index = 0;
};

}  // namespace logic
}  // namespace fn

#endif

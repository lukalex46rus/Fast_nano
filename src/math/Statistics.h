#pragma once
// ============================================================================
//  Statistics.h — накопительная статистика без окна и динамической памяти
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Статистика за всё время наблюдения: O(1) по памяти и времени на отсчёт.
//    Отличие от фильтров слоя logic (MovingAverage): здесь нет окна, все
//    значения равноправны. Три класса: целочисленный, float по Уэлфорду,
//    и целочисленный с дисперсией без float.
//
//  API (namespace fn::math)
//    class Statistics — int32 вход, int64 сумма
//      reset() / add(int32_t)
//      count() / sum() / minValue() / maxValue() / lastValue() / range()
//      meanInt() -> int32_t           усечение к нулю
//      meanRound() -> int32_t         к ближайшему
//      mean() -> float                точное среднее (сумма/count во float)
//    class IntVariance — int32 вход, дисперсия без float
//      reset() / add(int32_t)
//      count() / meanInt() / varianceInt() -> uint32_t
//      stdDevInt() -> uint16_t        целочисленный корень
//    class FloatStatistics — float вход, алгоритм Уэлфорда
//      reset() / add(float) / add(int32_t)
//      count() / mean() / variance() / stdDev()
//      minValue() / maxValue() / lastValue()
//    Свободные функции по массивам int32_t (inline):
//      arraySum / arrayMean / arrayMinimum / arrayMaximum
//      arrayVariance / arrayStdDev
//      arrayRms -> uint16_t           целочисленный RMS без float
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - count == 0 -> все значения 0: деления на ноль нет ни в одном методе.
//    - Имена minValue/maxValue, а НЕ min/max: Arduino.h определяет min()/max()
//      функциональными макросами. Прежняя версия снимала их через #undef —
//      это ломало сторонний код, включённый после заголовка. Теперь #undef нет.
//    - NaN игнорируется в FloatStatistics::add (count не растёт). ±Inf
//      попадает в статистику как есть — специально не обрабатывается.
//    - Statistics::mean() считает float(int64-суммы)/float(count): при суммах
//      свыше 2^24 возможна потеря точности. Для точного целого — meanInt()
//      или meanRound(), они считают в int64.
//    - IntVariance накапливает сумму квадратов в int64: при |value| > 3e9
//      или count > 2e9 возможно переполнение. Для типовых датчиков недостижимо.
//      Дисперсия считается как E[x²] - E[x]² — катастрофическая потеря
//      точности при большом среднем и малом разбросе. Если средний много
//      больше СКО, берите FloatStatistics (Уэлфорд устойчив).
//    - arrayVariance делает два прохода (среднее, затем квадраты отклонений):
//      устойчивее однопроходной формулы, но читает массив дважды.
//    - Все свободные функции inline: без этого включение из двух .cpp дало
//      бы multiple definition на линковке.
//    - sizeof(Statistics) == 24, sizeof(IntVariance) == 20,
//      sizeof(FloatStatistics) == 24 байта.
//
//  СТОИМОСТЬ (AVR)
//    add() в Statistics: int64-сложение ~10 тактов. IntVariance::add:
//    умножение int32 в int64 ~20 тактов. stdDevInt: isqrt32 ~40 тактов.
//    FloatStatistics::add: soft-float деление ~30 мкс. stdDev: sqrtf ~15 мкс.
//    В ISR использовать только Statistics и IntVariance.
//
//  ПРИМЕР
//    fn::math::IntVariance noise;
//    for (;;) {
//        noise.add(adcRead());
//        if (noise.count() >= 1000) {
//            uint16_t sigma = noise.stdDevInt();     // без float
//            break;
//        }
//    }
//
//    fn::math::Statistics acc;
//    acc.add(temperature);
//    int32_t avg = acc.meanRound();
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, math/MathUtils.h, math.h (sqrtf)
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "MathUtils.h"

#include <stdint.h>
#include <math.h>

#if F_USE_MATH

namespace fn {
namespace math {

// ============================ целочисленная ================================
class Statistics {
public:
    Statistics() = default;

    void reset() {
        _count = 0; _sum = 0; _min = 0; _max = 0; _last = 0;
    }

    void add(int32_t value) {
        if (_count == 0) { _min = value; _max = value; }
        else {
            if (value < _min) _min = value;
            if (value > _max) _max = value;
        }
        _sum += value;
        _last = value;
        ++_count;
    }

    uint32_t count() const { return _count; }
    int64_t sum() const { return _sum; }
    int32_t minValue() const { return _count ? _min : 0; }
    int32_t maxValue() const { return _count ? _max : 0; }
    int32_t lastValue() const { return _count ? _last : 0; }
    int64_t range() const { return _count ? int64_t(_max) - int64_t(_min) : 0; }

    int32_t meanInt() const { return _count ? int32_t(_sum / int64_t(_count)) : 0; }

    int32_t meanRound() const {
        if (!_count) return 0;
        const int64_t n = int64_t(_count);
        return int32_t(_sum >= 0 ? (_sum + n / 2) / n : (_sum - n / 2) / n);
    }

    float mean() const { return _count ? float(_sum) / float(_count) : 0.0f; }

private:
    uint32_t _count = 0;
    int64_t _sum = 0;
    int32_t _min = 0;
    int32_t _max = 0;
    int32_t _last = 0;
};

// ============================ дисперсия без float ==========================
// E[x²] - E[x]²: быстро, но теряет точность при большом среднем и малом СКО.
class IntVariance {
public:
    IntVariance() = default;

    void reset() { _count = 0; _sum = 0; _sumSq = 0; }

    void add(int32_t value) {
        _sum += value;
        _sumSq += int64_t(value) * int64_t(value);
        ++_count;
    }

    uint32_t count() const { return _count; }
    int32_t meanInt() const { return _count ? int32_t(_sum / int64_t(_count)) : 0; }

    // Генеральная дисперсия, округлённая к ближайшему. 0 при count == 0.
    uint32_t varianceInt() const {
        if (_count == 0) return 0;
        const int64_t n = int64_t(_count);
        // var = (sumSq * n - sum²) / n²
        const int64_t num = _sumSq * n - _sum * _sum;
        if (num <= 0) return 0;                 // защита от округления
        const int64_t den = n * n;
        return uint32_t((num + den / 2) / den);
    }

    uint16_t stdDevInt() const {
        const uint32_t v = varianceInt();
        const uint16_t r = isqrt32(v);
        return r > 0xFFFFu ? uint16_t(0xFFFFu) : r;
    }

private:
    uint32_t _count = 0;
    int64_t _sum = 0;
    int64_t _sumSq = 0;
};

// ============================ float по Уэлфорду ============================
class FloatStatistics {
public:
    FloatStatistics() = default;

    void reset() {
        _count = 0; _mean = 0.0f; _m2 = 0.0f;
        _min = 0.0f; _max = 0.0f; _last = 0.0f;
    }

    void add(float value) {
        if (isnan(value)) return;
        ++_count;
        const float delta = value - _mean;
        _mean += delta / float(_count);
        _m2 += delta * (value - _mean);
        if (_count == 1) { _min = value; _max = value; }
        else {
            if (value < _min) _min = value;
            if (value > _max) _max = value;
        }
        _last = value;
    }

    void add(int32_t value) { add(float(value)); }

    uint32_t count() const { return _count; }
    float mean() const { return _count ? _mean : 0.0f; }
    float variance() const { return _count ? _m2 / float(_count) : 0.0f; }
    float stdDev() const { return sqrtf(variance()); }
    float minValue() const { return _count ? _min : 0.0f; }
    float maxValue() const { return _count ? _max : 0.0f; }
    float lastValue() const { return _count ? _last : 0.0f; }

private:
    uint32_t _count = 0;
    float _mean = 0.0f;
    float _m2 = 0.0f;
    float _min = 0.0f;
    float _max = 0.0f;
    float _last = 0.0f;
};

// ============================ функции по массивам ==========================
inline int64_t arraySum(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0;
    int64_t total = 0;
    for (uint16_t i = 0; i < n; ++i) total += v[i];
    return total;
}

inline float arrayMean(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0.0f;
    return float(arraySum(v, n)) / float(n);
}

inline int32_t arrayMinimum(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0;
    int32_t r = v[0];
    for (uint16_t i = 1; i < n; ++i) if (v[i] < r) r = v[i];
    return r;
}

inline int32_t arrayMaximum(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0;
    int32_t r = v[0];
    for (uint16_t i = 1; i < n; ++i) if (v[i] > r) r = v[i];
    return r;
}

// Два прохода: устойчивее однопроходной формулы.
inline float arrayVariance(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0.0f;
    const float avg = arrayMean(v, n);
    float acc = 0.0f;
    for (uint16_t i = 0; i < n; ++i) {
        const float d = float(v[i]) - avg;
        acc += d * d;
    }
    return acc / float(n);
}

inline float arrayStdDev(const int32_t* v, uint16_t n) {
    return sqrtf(arrayVariance(v, n));
}

// Целочисленный RMS: sqrt(sum(x²)/n), без float.
inline uint16_t arrayRms(const int32_t* v, uint16_t n) {
    if (!v || n == 0) return 0;
    int64_t sq = 0;
    for (uint16_t i = 0; i < n; ++i) sq += int64_t(v[i]) * int64_t(v[i]);
    const uint64_t mean = uint64_t(sq / int64_t(n));
    const uint32_t r = isqrt64(mean);
    return r > 0xFFFFu ? uint16_t(0xFFFFu) : uint16_t(r);
}

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
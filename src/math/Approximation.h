#pragma once
// ============================================================================
//  Approximation.h — кусочно-линейная аппроксимация по таблице точек
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Восстановление Y(X) по табличным точкам: калибровка датчиков, линеаризация
//    термистора, профиль разгона. Таблица в RAM или PROGMEM. Поиск сегмента
//    бинарный (log2 N чтений) либо линейный для коротких таблиц.
//
//  API (namespace fn::math)
//    template <typename TX, typename TY, bool PGM = false> class Approximation
//      Approximation(const TX* x, const TY* y, uint16_t size)
//      get(float x) -> float                  невалидная таблица -> 0
//      get(float x, float& out) -> bool       false только при невалидной таблице
//      getInt(int32_t x) -> int32_t           целочисленный путь, без float
//      valid() / size()
//      segment(float x) -> uint16_t           индекс левого края сегмента
//    template <typename T, bool PGM = false> class Approximation2D
//      то же для массива пар {x, y}
//    findSegment(x, size, readFn) — служебный бинарный поиск (detail)
//
//  СЕМАНТИКА (зафиксирована)
//    невалидная таблица (nullptr или size == 0) -> get() == 0, get(x,out) == false
//    size == 1                                  -> всегда Y(0)
//    x < X(0)                                   -> Y(0)      (clamp, не экстраполяция)
//    x >= X(size-1)                             -> Y(size-1)
//    внутри диапазона                           -> линейная интерполяция
//    X(i+1) <= X(i)                             -> Y(i), без деления
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Контракт вызывающего: X строго возрастает, массивы одной длины и живут
//      дольше объекта. Класс таблицей НЕ владеет.
//    - Порядок X не проверяется в рантайме (скорость). При нарушении бинарный
//      поиск найдёт не тот сегмент: результат не «плоский участок», а
//      произвольная точка. Защита xb <= xa спасает только от деления на ноль.
//      Для отладки вызовите monotonic() — он проверит таблицу за O(N).
//    - getInt использует int64-деление (~44 мкс), но без soft-float: экономит
//      ~600 Б flash, если float больше нигде не нужен.
//    - При PGM = true ОБА массива должны лежать в PROGMEM. Чтение через
//      memcpy_P: ветвление по PGM — константа шаблона, компилятор сворачивает.
//    - Approximation2D — это таблица пар, а НЕ билинейная поверхность.
//      Для поверхности используйте bilinear() из Interpolation.h.
//    - sizeof(Approximation) == 6 байт (два указателя + size),
//      sizeof(Approximation2D) == 4 байта.
//
//  СТОИМОСТЬ (AVR)
//    Поиск: log2(N) чтений (для PROGMEM — LPM по 3 такта на байт).
//    Интерполяция float: десятки мкс. getInt: ~44 мкс. В ISR не годится.
//
//  ПРИМЕР
//    // Термистор: таблица АЦП -> температура, во flash
//    static const uint16_t adcTab[] FN_PROGMEM = {120, 250, 400, 600, 800};
//    static const int8_t   degTab[] FN_PROGMEM = {-20, 0, 25, 50, 90};
//    fn::math::Approximation<uint16_t, int8_t, true> therm(adcTab, degTab, 5);
//
//    int32_t tempC = therm.getInt(adcValue);      // без float
//    if (!therm.monotonic()) { /* таблица испорчена */ }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, avr/pgmspace.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>
#include <string.h>
#include <avr/pgmspace.h>

#if F_USE_MATH

namespace fn {
namespace math {
namespace detail {

// Чтение элемента: PROGMEM или RAM. PGM — константа шаблона.
template <typename T, bool PGM>
inline T readAt(const T* p) {
    if (PGM) {
        T v;
        memcpy_P(&v, p, sizeof(T));
        return v;
    }
    return *p;
}

}  // namespace detail

// ============================ X[] -> Y[] ===================================
template <typename TX, typename TY, bool PGM = false>
class Approximation {
public:
    Approximation(const TX* x, const TY* y, uint16_t size)
        : _x(x), _y(y), _size(size) {}

    bool valid() const { return _x != nullptr && _y != nullptr && _size >= 1; }
    uint16_t size() const { return _size; }

    float get(float x) const {
        float out = 0.0f;
        return get(x, out) ? out : 0.0f;
    }

    bool get(float x, float& out) const {
        if (!valid()) return false;
        if (_size == 1) { out = float(readY(0)); return true; }

        const float x0 = float(readX(0));
        if (x < x0) { out = float(readY(0)); return true; }

        const uint16_t last = uint16_t(_size - 1);
        if (x >= float(readX(last))) { out = float(readY(last)); return true; }

        const uint16_t lo = segment(x);
        const float xa = float(readX(lo));
        const float xb = float(readX(uint16_t(lo + 1)));
        const float ya = float(readY(lo));
        const float yb = float(readY(uint16_t(lo + 1)));
        if (xb <= xa) { out = ya; return true; }
        out = (x - xa) * (yb - ya) / (xb - xa) + ya;
        return true;
    }

    // Целочисленный путь: без soft-float, int64 внутри.
    int32_t getInt(int32_t x) const {
        if (!valid()) return 0;
        if (_size == 1) return int32_t(readY(0));

        const int32_t x0 = int32_t(readX(0));
        if (x < x0) return int32_t(readY(0));

        const uint16_t last = uint16_t(_size - 1);
        if (x >= int32_t(readX(last))) return int32_t(readY(last));

        const uint16_t lo = segmentInt(x);
        const int64_t xa = int64_t(readX(lo));
        const int64_t xb = int64_t(readX(uint16_t(lo + 1)));
        const int64_t ya = int64_t(readY(lo));
        const int64_t yb = int64_t(readY(uint16_t(lo + 1)));
        if (xb <= xa) return int32_t(ya);
        return int32_t(ya + (int64_t(x) - xa) * (yb - ya) / (xb - xa));
    }

    // Индекс левого края сегмента: X(lo) <= x < X(lo+1).
    // Вызывающий гарантирует, что x внутри диапазона и size >= 2.
    uint16_t segment(float x) const {
        uint16_t lo = 0;
        uint16_t hi = uint16_t(_size - 1);
        while (uint16_t(hi - lo) > 1) {
            const uint16_t mid = uint16_t(lo + (hi - lo) / 2);
            if (float(readX(mid)) <= x) lo = mid;
            else hi = mid;
        }
        return lo;
    }

    uint16_t segmentInt(int32_t x) const {
        uint16_t lo = 0;
        uint16_t hi = uint16_t(_size - 1);
        while (uint16_t(hi - lo) > 1) {
            const uint16_t mid = uint16_t(lo + (hi - lo) / 2);
            if (int32_t(readX(mid)) <= x) lo = mid;
            else hi = mid;
        }
        return lo;
    }

    // Отладочная проверка: X строго возрастает. O(N) чтений.
    bool monotonic() const {
        if (!valid()) return false;
        for (uint16_t i = 1; i < _size; ++i) {
            if (readX(i) <= readX(uint16_t(i - 1))) return false;
        }
        return true;
    }

private:
    TX readX(uint16_t i) const { return detail::readAt<TX, PGM>(&_x[i]); }
    TY readY(uint16_t i) const { return detail::readAt<TY, PGM>(&_y[i]); }

    const TX* _x;
    const TY* _y;
    uint16_t _size;
};

// ============================ пары {x, y} ==================================
template <typename T, bool PGM = false>
class Approximation2D {
public:
    Approximation2D(const T xy[][2], uint16_t size) : _xy(xy), _size(size) {}

    bool valid() const { return _xy != nullptr && _size >= 1; }
    uint16_t size() const { return _size; }

    float get(float x) const {
        float out = 0.0f;
        return get(x, out) ? out : 0.0f;
    }

    bool get(float x, float& out) const {
        if (!valid()) return false;
        if (_size == 1) { out = float(read(0, 1)); return true; }

        if (x < float(read(0, 0))) { out = float(read(0, 1)); return true; }
        const uint16_t last = uint16_t(_size - 1);
        if (x >= float(read(last, 0))) { out = float(read(last, 1)); return true; }

        uint16_t lo = 0;
        uint16_t hi = last;
        while (uint16_t(hi - lo) > 1) {
            const uint16_t mid = uint16_t(lo + (hi - lo) / 2);
            if (float(read(mid, 0)) <= x) lo = mid;
            else hi = mid;
        }
        const float xa = float(read(lo, 0));
        const float xb = float(read(uint16_t(lo + 1), 0));
        const float ya = float(read(lo, 1));
        const float yb = float(read(uint16_t(lo + 1), 1));
        if (xb <= xa) { out = ya; return true; }
        out = (x - xa) * (yb - ya) / (xb - xa) + ya;
        return true;
    }

    int32_t getInt(int32_t x) const {
        if (!valid()) return 0;
        if (_size == 1) return int32_t(read(0, 1));
        if (x < int32_t(read(0, 0))) return int32_t(read(0, 1));
        const uint16_t last = uint16_t(_size - 1);
        if (x >= int32_t(read(last, 0))) return int32_t(read(last, 1));

        uint16_t lo = 0;
        uint16_t hi = last;
        while (uint16_t(hi - lo) > 1) {
            const uint16_t mid = uint16_t(lo + (hi - lo) / 2);
            if (int32_t(read(mid, 0)) <= x) lo = mid;
            else hi = mid;
        }
        const int64_t xa = int64_t(read(lo, 0));
        const int64_t xb = int64_t(read(uint16_t(lo + 1), 0));
        const int64_t ya = int64_t(read(lo, 1));
        const int64_t yb = int64_t(read(uint16_t(lo + 1), 1));
        if (xb <= xa) return int32_t(ya);
        return int32_t(ya + (int64_t(x) - xa) * (yb - ya) / (xb - xa));
    }

    bool monotonic() const {
        if (!valid()) return false;
        for (uint16_t i = 1; i < _size; ++i) {
            if (read(i, 0) <= read(uint16_t(i - 1), 0)) return false;
        }
        return true;
    }

private:
    T read(uint16_t i, uint8_t col) const {
        return detail::readAt<T, PGM>(&_xy[i][col]);
    }

    const T (*_xy)[2];
    uint16_t _size;
};

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
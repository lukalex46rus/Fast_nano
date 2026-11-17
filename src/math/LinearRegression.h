#pragma once
// ============================================================================
//  LinearRegression.h — линейная аппроксимация методом наименьших квадратов
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    По массивам X и Y вычисляет наклон, сдвиг и изменение по всему диапазону.
//    Применения: тренд датчика, скорость изменения, калибровка по двум точкам
//    с шумом, оценка дрейфа.
//
//  API (namespace fn::math)
//    template <typename T> class LinearRegression
//      compute(const T* x, const T* y, uint16_t count) -> Result
//      slope() / intercept() / delta() -> float
//      span() -> float                  maxX - minX
//      predict(float x) -> float        slope*x + intercept
//      valid() -> bool
//      reset()
//    template <typename T> Result linearFit(x, y, n, float& slope, float& intercept)
//      одноразовый расчёт без объекта
//
//  КОДЫ ВОЗВРАТА compute
//    Ok          рассчитано
//    BadArg      nullptr или count < 2
//    OutOfRange  все X совпадают (вертикальная прямая, наклона нет)
//
//  ЧИСЛЕННАЯ УСТОЙЧИВОСТЬ (главное отличие от прежней версии)
//    Классическая формула denominator = n*ΣX² − (ΣX)² катастрофически теряет
//    точность при больших X. Проверено: X = {1000, 1001, 1002} при float
//    (24 бита мантиссы) даёт n*ΣX² = 9018015 и (ΣX)² = 9018009; разность 6
//    вычисляется как разность двух величин порядка 9e6, где шаг float равен
//    1.0 — погрешность до ±1, то есть до 17% наклона. При X около 1e4
//    разность уходит в ноль и compute вернёт отказ на корректных данных.
//
//    Здесь используется ЦЕНТРИРОВАНИЕ: сначала считается среднее X и Y,
//    затем суммы отклонений. Величины остаются малыми, потери точности нет.
//    Формулы: slope = Σ(dx*dy) / Σ(dx²), intercept = meanY - slope*meanX.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - delta() = slope * (maxX - minX): изменение по ВСЕМУ диапазону X,
//      а не «между первым и последним элементом массива». Прежняя версия
//      считала x[count-1] - x[0], что при несортированном массиве давало
//      произвольную величину. Теперь порядок массива не важен.
//    - Порядок X не требуется: МНК работает с несортированными данными.
//    - count ограничен uint16: максимум 65535 точек. Накопление в float,
//      при count > 1e4 и большом разбросе точность падает — центрирование
//      помогает, но не отменяет предел float.
//    - Все вычисления во float: ~30 мкс на точку при 16 МГц. В ISR не годится.
//      Для потоковой регрессии без массива используйте накопители отдельно.
//    - sizeof(LinearRegression) == 17 байт (4 float + bool).
//    - Массивы должны жить дольше вызова compute; класс их не хранит.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 17 байт на объект. Flash: ~450 байт (soft-float умножение/деление).
//    Два прохода по данным: центрирование, затем суммы отклонений.
//
//  ПРИМЕР
//    const int16_t t[5] = {1000, 1001, 1002, 1003, 1004};
//    const int16_t y[5] = {  20,   22,   24,   26,   28};
//    fn::math::LinearRegression<int16_t> fit;
//    if (fn::ok(fit.compute(t, y, 5))) {
//        float k = fit.slope();        // 2.0, а не мусор
//        float d = fit.delta();        // 8.0 за весь диапазон
//        float next = fit.predict(1005);
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>

#if F_USE_MATH

namespace fn {
namespace math {

template <typename T>
class LinearRegression {
public:
    LinearRegression() = default;

    void reset() {
        _slope = 0.0f; _intercept = 0.0f; _delta = 0.0f; _span = 0.0f;
        _valid = false;
    }

    Result compute(const T* x, const T* y, uint16_t count) {
        _valid = false;
        if (!x || !y || count < 2) return Result::BadArg;

        // Проход 1: средние и границы X. Центрирование держит величины малыми.
        float sumX = 0.0f;
        float sumY = 0.0f;
        float xMin = float(x[0]);
        float xMax = xMin;
        for (uint16_t i = 0; i < count; ++i) {
            const float xi = float(x[i]);
            sumX += xi;
            sumY += float(y[i]);
            if (xi < xMin) xMin = xi;
            if (xi > xMax) xMax = xi;
        }
        const float n = float(count);
        const float meanX = sumX / n;
        const float meanY = sumY / n;

        // Проход 2: суммы отклонений от средних.
        float sxx = 0.0f;
        float sxy = 0.0f;
        for (uint16_t i = 0; i < count; ++i) {
            const float dx = float(x[i]) - meanX;
            const float dy = float(y[i]) - meanY;
            sxx += dx * dx;
            sxy += dx * dy;
        }
        if (sxx <= 0.0f) return Result::OutOfRange;   // все X совпадают

        _slope = sxy / sxx;
        _intercept = meanY - _slope * meanX;
        _span = xMax - xMin;
        _delta = _slope * _span;
        _valid = true;
        return Result::Ok;
    }

    float slope() const { return _slope; }
    float intercept() const { return _intercept; }
    float delta() const { return _delta; }
    float span() const { return _span; }
    bool valid() const { return _valid; }

    float predict(float x) const { return _slope * x + _intercept; }

private:
    float _slope = 0.0f;
    float _intercept = 0.0f;
    float _delta = 0.0f;
    float _span = 0.0f;
    bool _valid = false;
};

// Одноразовый расчёт без объекта: экономит 17 байт, если состояние не нужно.
template <typename T>
inline Result linearFit(const T* x, const T* y, uint16_t count,
                        float& slopeOut, float& interceptOut) {
    LinearRegression<T> fit;
    const Result r = fit.compute(x, y, count);
    if (ok(r)) {
        slopeOut = fit.slope();
        interceptOut = fit.intercept();
    }
    return r;
}

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
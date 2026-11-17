#pragma once
// ============================================================================
//  Interpolation.h — интерполяция между известными значениями
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Линейная и гладкая интерполяция, обратная интерполяция, билинейная
//    поверхность. Три представления: float (точность), int32 (скорость),
//    Fixed<N> (для регуляторов). Без <math.h> и без таблиц.
//    Табличная интерполяция по массиву точек — в math/Approximation.h.
//
//  API (namespace fn::math)
//    Float
//      lerp(a, b, t)                  a + (b-a)*t, экстраполяция допустима
//      lerpClamped(a, b, t)           t зажат в [0..1]
//      inverseLerp(a, b, v)           (v-a)/(b-a); a==b -> 0
//      smoothStep(t)                  3t²-2t³, t зажат
//      smootherStep(t)                6t⁵-15t⁴+10t³, t зажат
//      bilinear(v00,v10,v01,v11,tx,ty)
//    Целочисленные
//      lerpInt(a, b, t, tMax = 255)   int64 внутри, усечение к нулю
//      lerpIntRound(a, b, t, tMax)    к ближайшему
//      lerpShift(a, b, shift, bits)   t из [0..2^bits], только сдвиги
//    Fixed<N>
//      lerpFixed(a, b, t)             t: Fixed<N> в [0..1]
//      lerpFixedSat(a, b, t)          с насыщением при переполнении
//
//  ПОЛИТИКА ЭКСТРАПОЛЯЦИИ
//    lerp/bilinear/lerpInt/lerpFixed — t вне [0..1] даёт экстраполяцию
//    (это нормально для управления). lerpClamped/smoothStep/smootherStep —
//    t зажимается. Явного флага нет: выбор функции и есть выбор политики.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Деления на ноль нет: inverseLerp(a==b) -> 0, lerpInt(tMax==0) -> a.
//    - lerpInt при t > tMax экстраполирует; результат усекается до int32
//      МОЛЧА, если вышел за диапазон. Для контроля берите lerpIntRound
//      с предварительным clamp(t, 0, tMax).
//    - lerpShift — самый быстрый путь: только умножение int32 и сдвиг,
//      без деления. Требует, чтобы t был в [0..2^bits] и bits <= 15
//      (иначе произведение (b-a)*t выйдет за int32 при больших b-a).
//    - lerpFixed использует Fixed::operator*, то есть int64 внутри:
//      ~40 тактов плюс __muldi3. Для горячего цикла берите lerpShift.
//    - smoothStep/smootherStep — soft-float, десятки мкс. В ISR не годится.
//    - bilinear не зажимает tx/ty: экстраполяция по обеим осям допустима.
//
//  СТОИМОСТЬ (AVR)
//    lerpShift ~15 тактов. lerpInt ~44 мкс (int64-деление). lerpFixed ~40
//    тактов + __muldi3. float-варианты — десятки мкс. Таблиц нет.
//
//  ПРИМЕР
//    // Плавный разгон ШИМ без float
//    uint8_t duty = uint8_t(fn::math::lerpShift(dutyFrom, dutyTo, step, 8));
//
//    // Регулятор на Fixed16
//    auto target = fn::math::lerpFixedSat(setA, setB, ratio);
//
//    // S-кривая для анимации
//    float k = fn::math::smoothStep(progress);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, math/FixedPoint.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "FixedPoint.h"

#include <stdint.h>

#if F_USE_MATH

namespace fn {
namespace math {

// ---------------------------- float ----------------------------------------
inline constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline constexpr float lerpClamped(float a, float b, float t) {
    return t <= 0.0f ? a : (t >= 1.0f ? b : lerp(a, b, t));
}

inline constexpr float inverseLerp(float a, float b, float v) {
    return a == b ? 0.0f : (v - a) / (b - a);
}

inline constexpr float smoothStep(float t) {
    return t <= 0.0f ? 0.0f : (t >= 1.0f ? 1.0f : t * t * (3.0f - 2.0f * t));
}

inline constexpr float smootherStep(float t) {
    return t <= 0.0f ? 0.0f
         : (t >= 1.0f ? 1.0f : t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f));
}

inline constexpr float bilinear(float v00, float v10, float v01, float v11,
                                float tx, float ty) {
    return lerp(lerp(v00, v10, tx), lerp(v01, v11, tx), ty);
}

// ---------------------------- целочисленные --------------------------------
inline constexpr int32_t lerpInt(int32_t a, int32_t b, uint16_t t, uint16_t tMax = 255) {
    return tMax == 0 ? a
         : int32_t(int64_t(a) + (int64_t(b) - a) * int64_t(t) / int64_t(tMax));
}

inline constexpr int32_t lerpIntRound(int32_t a, int32_t b, uint16_t t, uint16_t tMax = 255) {
    return tMax == 0 ? a
         : int32_t(int64_t(a) +
                   ((int64_t(b) - a) * int64_t(t) * 2 +
                    ((int64_t(b) - a) * int64_t(t) >= 0 ? int64_t(tMax) : -int64_t(tMax))) /
                       (int64_t(tMax) * 2));
}

// Самый быстрый путь: t из [0..2^bits], только умножение и сдвиг.
// bits <= 15, t <= (1 << bits). Экстраполяция при t > 2^bits допустима.
inline int32_t lerpShift(int32_t a, int32_t b, uint16_t t, uint8_t bits) {
    const int32_t d = int32_t(b - a);
    return int32_t(a + int32_t((int64_t(d) * int32_t(t)) >> bits));
}

// ---------------------------- Fixed ----------------------------------------
#if F_USE_FIXED_POINT
template <uint8_t N>
inline constexpr Fixed<N> lerpFixed(const Fixed<N>& a, const Fixed<N>& b,
                                    const Fixed<N>& t) {
    return a + (b - a) * t;
}

template <uint8_t N>
inline constexpr Fixed<N> lerpFixedSat(const Fixed<N>& a, const Fixed<N>& b,
                                       const Fixed<N>& t) {
    return a.addSat((b.subSat(a)).mulSat(t));
}
#endif

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
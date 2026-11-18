#pragma once
// ============================================================================
//  FixedPoint.h — числа с фиксированной точкой Q(31-N).N на int32_t
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единый числовой тип слоя logic и математики. Знаковое 32-битное число
//    с N дробными битами. Позволяет писать Pid/Filter/Ramp без soft-float
//    (~600 Б flash на операцию) и без разнородных масштабов у модулей.
//    Основной рабочий формат библиотеки — Fixed16 (Q15.16).
//
//  ФОРМАТ
//    Fixed<N>: raw = value * 2^N, хранится в int32_t.
//    Fixed8  = Q23.8 : диапазон ±8388608,   разрешение 1/256      ≈ 3.9e-3
//    Fixed16 = Q15.16: диапазон ±32768,     разрешение 1/65536    ≈ 1.5e-5
//    Fixed24 = Q7.24 : диапазон ±128,       разрешение 1/16777216 ≈ 6.0e-8
//    Размер объекта всегда 4 байта независимо от N.
//
//  API
//    Типы: Fixed<N>, алиасы Fixed8 / Fixed16 / Fixed24
//    Метаданные (constexpr):
//      fractionBits() / integerBits() / scale() / maxInt() / minInt()
//      epsilon() -> Fixed            младший representable шаг (raw = 1)
//    Конструирование:
//      Fixed()                       ноль
//      Fixed(целое)                  raw = v << N
//      Fixed(float/double)           усечение к нулю
//      fromRaw(int32_t)              обёртка готового сырого значения
//      fromFloatSaturated(float)     с насыщением, NaN -> 0
//    Доступ: raw() / toInt() / toIntRound() / toFloat() / frac()
//    Арифметика Fixed-Fixed: + - * / унарный -, а также += -= *= /=
//    Арифметика со скаляром: те же операции с целым и float
//    Насыщающие операции (для интеграторов и накопителей):
//      addSat(b) / subSat(b) / mulSat(b) / negSat()
//      clamped(lo, hi) / saturated()  — свободные аналоги
//    Сравнения: == != < <= > >= с Fixed и со скаляром
//    Литералы: 2_fx8, 9.5_fx16, 1.25_fx24 (в глобальном пространстве имён)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - ОБЫЧНЫЕ + И - ПЕРЕПОЛНЯЮТСЯ МОЛЧА при выходе за диапазон. Это цена
//      скорости (2 такта против ~10 у addSat). Для интегратора PID,
//      накопления ошибки, сумматоров — ТОЛЬКО addSat/subSat.
//    - mul/div идут через int64: тянут __muldi3/__divdi3 (~200-400 Б flash
//      на всю программу, не на вызов). Без 64 бит произведение двух Q15.16
//      не помещается в 32 бита уже при значениях > 256.
//    - Умножение округляет ВНИЗ (арифметический сдвиг), не к нулю:
//      Fixed16(-0.5) * Fixed16(0.5) даст -0.25001..., а не -0.25.
//      Для симметричного округления используйте mulRound().
//    - Деление на ноль возвращает 0 (не UB, не assert). Проверяйте делитель
//      заранее, если ноль — штатная ситуация.
//    - toFloat/fromFloat тянут soft-float: только настройка и вывод,
//      не цикл регулирования.
//    - constexpr Fixed(float) работает на этапе компиляции: коэффициенты
//      вида constexpr Fixed16 Kp(2.5f) не занимают flash.
//    - Неявных конверсий в int/float нет: только toInt()/toFloat()/raw().
//      explicit operator bool() есть — проверка на ненулевое значение.
//    - Литеральные операторы обязаны быть в глобальном пространстве имён:
//      поиск operator"" идёт неквалифицированно, ADL не применяется.
//      Это единственное сознательное отклонение от «всё в fn::math».
//    - double/long double принимаются для совместимости с литералами:
//      на AVR double 32-битный, внутри приводится к float.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 4 байта на объект. Flash: +/- ~10 Б, mul ~40 Б + __muldi3,
//    div ~50 Б + __divdi3, addSat ~20 Б. constexpr-вычисления бесплатны.
//
//  ПРИМЕР
//    using fn::math::Fixed16;
//    constexpr Fixed16 Kp(2);
//    const Fixed16 Ki = Fixed16::fromFloatSaturated(0.05f);   // один раз
//
//    Fixed16 err = setpoint.subSat(measured);
//    integral = integral.addSat(Ki * err);                    // без wrap
//    Fixed16 out = (Kp * err).addSat(integral);
//    out = out.clamped(Fixed16(-1000), Fixed16(1000));
//
//    static_assert((2_fx16 * 3_fx16).raw() == 6 * 65536, "");
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>

#if F_USE_FIXED_POINT

namespace fn {
namespace math {

template <uint8_t F_BITS>
class Fixed {
    static_assert(F_BITS <= 30, "Fixed<N>: N должен быть 0..30");

    // Тег отсекает случайные int32_t от конструктора «из сырого значения».
    struct RawTag {};
    constexpr Fixed(int32_t raw, RawTag) : _raw(raw) {}

    static constexpr int32_t mulRaw(int32_t a, int32_t b) {
        return int32_t((int64_t(a) * int64_t(b)) >> F_BITS);
    }
    static constexpr int32_t divRaw(int32_t a, int32_t b) {
        return int32_t((int64_t(a) * int64_t(scale())) / int64_t(b));
    }
    static constexpr int32_t satRaw(int64_t v) {
        return v > int64_t(INT32_MAX) ? INT32_MAX
             : (v < int64_t(INT32_MIN) ? INT32_MIN : int32_t(v));
    }

public:
    // ---------------------------- метаданные -------------------------------
    static constexpr uint8_t fractionBits() { return F_BITS; }
    static constexpr uint8_t integerBits() { return uint8_t(31 - F_BITS); }
    static constexpr int32_t scale() { return int32_t(int64_t(1) << F_BITS); }
    static constexpr int32_t maxInt() { return int32_t((int64_t(1) << (31 - F_BITS)) - 1); }
    static constexpr int32_t minInt() { return int32_t(-(int64_t(1) << (31 - F_BITS))); }
    static constexpr Fixed epsilon() { return Fixed(int32_t(1), RawTag{}); }

    // ---------------------------- конструирование --------------------------
    static constexpr Fixed fromRaw(int32_t raw) { return Fixed(raw, RawTag{}); }

    static Fixed fromFloatSaturated(float v) {
        if (v != v) return Fixed();                       // NaN -> 0
        const float scaled = v * float(scale());
        if (scaled >= 2147483647.0f) return Fixed(INT32_MAX, RawTag{});
        if (scaled <= -2147483648.0f) return Fixed(INT32_MIN, RawTag{});
        return Fixed(int32_t(scaled), RawTag{});
    }

    constexpr Fixed() : _raw(0) {}

    template <typename T>
    constexpr Fixed(T v) : _raw(int32_t(int64_t(v) * int64_t(scale()))) {}

    constexpr Fixed(float v) : _raw(int32_t(v * float(scale()))) {}
    constexpr Fixed(double v) : Fixed(float(v)) {}
    constexpr Fixed(long double v) : Fixed(float(v)) {}

    // ---------------------------- доступ -----------------------------------
    constexpr int32_t raw() const { return _raw; }
    constexpr int32_t toInt() const { return int32_t(_raw / scale()); }

    // Округление к ближайшему, половина — от нуля.
    constexpr int32_t toIntRound() const {
        return _raw >= 0 ? int32_t((_raw + scale() / 2) / scale())
                         : -int32_t(((-_raw) + scale() / 2) / scale());
    }

    constexpr float toFloat() const { return float(_raw) / float(scale()); }

    // Дробная часть как беззнаковая доля единицы (0..scale()-1).
    constexpr uint32_t frac() const {
        return uint32_t((_raw >= 0 ? _raw : -_raw) & (scale() - 1));
    }

    explicit constexpr operator bool() const { return _raw != 0; }

    // ---------------------------- арифметика -------------------------------
    constexpr Fixed operator+(const Fixed& o) const { return Fixed(int32_t(_raw + o._raw), RawTag{}); }
    constexpr Fixed operator-(const Fixed& o) const { return Fixed(int32_t(_raw - o._raw), RawTag{}); }
    constexpr Fixed operator*(const Fixed& o) const { return Fixed(mulRaw(_raw, o._raw), RawTag{}); }
    constexpr Fixed operator/(const Fixed& o) const {
        return o._raw == 0 ? Fixed() : Fixed(divRaw(_raw, o._raw), RawTag{});
    }
    constexpr Fixed operator-() const { return Fixed(int32_t(-_raw), RawTag{}); }

    // Умножение с округлением к ближайшему (половина — от нуля).
    Fixed mulRound(const Fixed& o) const {
        const int64_t p = int64_t(_raw) * int64_t(o._raw);
        const int64_t half = int64_t(1) << (F_BITS - 1);
        return Fixed(int32_t(p >= 0 ? ((p + half) >> F_BITS)
                                    : -(((-p) + half) >> F_BITS)), RawTag{});
    }

    // ---------------------------- насыщение --------------------------------
    constexpr Fixed addSat(const Fixed& o) const {
        return Fixed(satRaw(int64_t(_raw) + int64_t(o._raw)), RawTag{});
    }
    constexpr Fixed subSat(const Fixed& o) const {
        return Fixed(satRaw(int64_t(_raw) - int64_t(o._raw)), RawTag{});
    }
    constexpr Fixed mulSat(const Fixed& o) const {
        return Fixed(satRaw((int64_t(_raw) * int64_t(o._raw)) >> F_BITS), RawTag{});
    }
    constexpr Fixed negSat() const {
        return _raw == INT32_MIN ? Fixed(INT32_MAX, RawTag{}) : Fixed(int32_t(-_raw), RawTag{});
    }
    constexpr Fixed clamped(const Fixed& lo, const Fixed& hi) const {
        return _raw < lo._raw ? lo : (_raw > hi._raw ? hi : *this);
    }

    // ---------------------------- накопление -------------------------------
    Fixed& operator+=(const Fixed& o) { _raw = int32_t(_raw + o._raw); return *this; }
    Fixed& operator-=(const Fixed& o) { _raw = int32_t(_raw - o._raw); return *this; }
    Fixed& operator*=(const Fixed& o) { _raw = mulRaw(_raw, o._raw); return *this; }
    Fixed& operator/=(const Fixed& o) { _raw = o._raw == 0 ? 0 : divRaw(_raw, o._raw); return *this; }

    // ---------------------------- скаляры ----------------------------------
    template <typename T> constexpr Fixed operator+(T v) const { return *this + Fixed(v); }
    template <typename T> constexpr Fixed operator-(T v) const { return *this - Fixed(v); }
    template <typename T> constexpr Fixed operator*(T v) const {
        return Fixed(int32_t(int64_t(_raw) * int64_t(v)), RawTag{});
    }
    template <typename T> constexpr Fixed operator/(T v) const {
        return v == 0 ? Fixed() : Fixed(int32_t(int64_t(_raw) / int64_t(v)), RawTag{});
    }

    // float-множитель: raw = int32(float(raw) * v). Без этой перегрузки
    // Fixed16(2) * 0.5f дал бы 0 (int64(float) обнулил бы дробь).
    constexpr Fixed operator*(float v) const { return Fixed(int32_t(float(_raw) * v), RawTag{}); }
    constexpr Fixed operator*(double v) const { return Fixed(int32_t(float(_raw) * float(v)), RawTag{}); }
    constexpr Fixed operator/(float v) const {
        return v == 0.0f ? Fixed() : Fixed(int32_t(float(_raw) / v), RawTag{});
    }
    constexpr Fixed operator/(double v) const { return *this / float(v); }

    // ---------------------------- сравнения --------------------------------
    constexpr bool operator==(const Fixed& o) const { return _raw == o._raw; }
    constexpr bool operator!=(const Fixed& o) const { return _raw != o._raw; }
    constexpr bool operator< (const Fixed& o) const { return _raw <  o._raw; }
    constexpr bool operator<=(const Fixed& o) const { return _raw <= o._raw; }
    constexpr bool operator> (const Fixed& o) const { return _raw >  o._raw; }
    constexpr bool operator>=(const Fixed& o) const { return _raw >= o._raw; }

    template <typename T> constexpr bool operator==(T v) const { return *this == Fixed(v); }
    template <typename T> constexpr bool operator!=(T v) const { return *this != Fixed(v); }
    template <typename T> constexpr bool operator< (T v) const { return *this <  Fixed(v); }
    template <typename T> constexpr bool operator<=(T v) const { return *this <= Fixed(v); }
    template <typename T> constexpr bool operator> (T v) const { return *this >  Fixed(v); }
    template <typename T> constexpr bool operator>=(T v) const { return *this >= Fixed(v); }

private:
    int32_t _raw;
};

using Fixed8  = Fixed<8>;
using Fixed16 = Fixed<16>;
using Fixed24 = Fixed<24>;

static_assert(sizeof(Fixed16) == 4, "Fixed<N> должен занимать 4 байта");

// ---------------------- операторы «скаляр слева» ---------------------------
template <uint8_t N, typename T>
constexpr Fixed<N> operator+(T lhs, const Fixed<N>& rhs) { return Fixed<N>(lhs) + rhs; }
template <uint8_t N, typename T>
constexpr Fixed<N> operator-(T lhs, const Fixed<N>& rhs) { return Fixed<N>(lhs) - rhs; }
template <uint8_t N, typename T>
constexpr Fixed<N> operator*(T lhs, const Fixed<N>& rhs) {
    return Fixed<N>::fromRaw(int32_t(int64_t(lhs) * int64_t(rhs.raw())));
}
template <uint8_t N, typename T>
constexpr Fixed<N> operator/(T lhs, const Fixed<N>& rhs) { return Fixed<N>(lhs) / rhs; }

template <uint8_t N, typename T>
constexpr bool operator==(T lhs, const Fixed<N>& rhs) { return rhs == lhs; }
template <uint8_t N, typename T>
constexpr bool operator!=(T lhs, const Fixed<N>& rhs) { return rhs != lhs; }
template <uint8_t N, typename T>
constexpr bool operator<(T lhs, const Fixed<N>& rhs) { return rhs > lhs; }
template <uint8_t N, typename T>
constexpr bool operator<=(T lhs, const Fixed<N>& rhs) { return rhs >= lhs; }
template <uint8_t N, typename T>
constexpr bool operator>(T lhs, const Fixed<N>& rhs) { return rhs < lhs; }
template <uint8_t N, typename T>
constexpr bool operator>=(T lhs, const Fixed<N>& rhs) { return rhs <= lhs; }

// ---------------------- свободные насыщающие функции -----------------------
template <uint8_t N>
constexpr Fixed<N> addSat(const Fixed<N>& a, const Fixed<N>& b) { return a.addSat(b); }
template <uint8_t N>
constexpr Fixed<N> subSat(const Fixed<N>& a, const Fixed<N>& b) { return a.subSat(b); }
template <uint8_t N>
constexpr Fixed<N> mulSat(const Fixed<N>& a, const Fixed<N>& b) { return a.mulSat(b); }
template <uint8_t N>
constexpr Fixed<N> clamp(const Fixed<N>& v, const Fixed<N>& lo, const Fixed<N>& hi) {
    return v.clamped(lo, hi);
}
template <uint8_t N>
constexpr Fixed<N> absVal(const Fixed<N>& v) {
    return v.raw() >= 0 ? v : v.negSat();
}

}  // namespace math
}  // namespace fn

// Литералы в глобальном пространстве имён: см. шапку.
constexpr fn::math::Fixed8  operator"" _fx8(unsigned long long v)  { return fn::math::Fixed8(uint32_t(v)); }
constexpr fn::math::Fixed8  operator"" _fx8(long double v)         { return fn::math::Fixed8(float(v)); }
constexpr fn::math::Fixed16 operator"" _fx16(unsigned long long v) { return fn::math::Fixed16(uint32_t(v)); }
constexpr fn::math::Fixed16 operator"" _fx16(long double v)        { return fn::math::Fixed16(float(v)); }
constexpr fn::math::Fixed24 operator"" _fx24(unsigned long long v) { return fn::math::Fixed24(uint32_t(v)); }
constexpr fn::math::Fixed24 operator"" _fx24(long double v)        { return fn::math::Fixed24(float(v)); }

#endif  // F_USE_FIXED_POINT
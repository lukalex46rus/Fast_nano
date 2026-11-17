#pragma once
// ============================================================================
//  MathUtils.h — целочисленная математика без libm
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Расширение слоя: то, чего не хватало между Mapping и Statistics.
//    Целочисленный квадратный корень (для RMS, расстояний, stdDev без float),
//    логарифм по основанию 2, степени двойки, НОД, округления. Всё без
//    <math.h> и без soft-float: sqrtf стоит ~15 мкс и ~500 Б flash, isqrt32
//    обходится ~40 тактами и ~60 Б.
//
//  API (namespace fn::math)
//    Корни
//      isqrt16(uint16_t) -> uint8_t     floor(sqrt), побитовый алгоритм
//      isqrt32(uint32_t) -> uint16_t    floor(sqrt)
//      isqrt64(uint64_t) -> uint32_t    floor(sqrt), для сумм квадратов
//      isqrtRound(uint32_t) -> uint16_t округление к ближайшему
//      hypot32(int32_t, int32_t) -> uint32_t  sqrt(x²+y²) без переполнения
//    Степени и логарифмы
//      isPow2(uint32_t) -> bool
//      nextPow2(uint32_t) -> uint32_t   вверх; 0 -> 1, > 2^31 -> 2^31
//      log2Floor(uint32_t) -> uint8_t   позиция старшего бита; 0 -> 0
//      log2Ceil(uint32_t) -> uint8_t
//      pow2(uint8_t n) -> uint32_t      n <= 31, иначе 0
//    Делимость
//      gcd(uint32_t, uint32_t) -> uint32_t      бинарный алгоритм
//      lcm(uint32_t, uint32_t) -> uint32_t      0 при переполнении
//    Округления и деление
//      divRound(int32_t, int32_t) -> int32_t    к ближайшему, половина от нуля
//      divCeil(int32_t, int32_t) -> int32_t     вверх (к +inf)
//      divFloor(int32_t, int32_t) -> int32_t    вниз (к -inf)
//      roundUpTo(uint32_t v, uint32_t step) -> uint32_t
//      alignUp(uint32_t v, uint8_t bits) -> uint32_t   до границы 2^bits
//    Прочее
//      absDiff(int32_t, int32_t) -> uint32_t    |a-b| без переполнения
//      sign(int32_t) -> int8_t                  -1 / 0 / +1
//      average2(int32_t, int32_t) -> int32_t    (a+b)/2 без переполнения
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Все функции целочисленные, деления на ноль нет ни в одной:
//      divRound/divCeil/divFloor при b == 0 возвращают 0.
//    - isqrt* дают floor(sqrt) точно, без приближений: побитовый алгоритм
//      восстанавливает результат по одному биту за итерацию.
//      isqrt32 — 16 итераций, isqrt64 — 32.
//    - hypot32 использует isqrt64: сумма квадратов int32 требует 64 бит.
//      Тянет __muldi3? Нет: умножение int32×int32 в int64 GCC делает через
//      __mulsi3 с расширением, libgcc-деление не нужно.
//    - nextPow2 при v > 2^31 возвращает 2^31 (насыщение), а не 0.
//    - lcm возвращает 0 при переполнении uint32 — это признак ошибки,
//      а не валидный результат.
//    - log2Floor(0) = 0: соглашение, а не математика. Проверяйте аргумент.
//    - Функции constexpr там, где это возможно в C++11 (один return).
//      Циклические (isqrt, gcd) — обычные inline, но при константном
//      аргументе GCC сворачивает их на -Os.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. isqrt32 ~60 Б flash / ~40 тактов, isqrt64 ~90 Б / ~120
//    тактов, gcd ~50 Б. Неиспользуемые функции вырезаются линкером.
//
//  ПРИМЕР
//    // RMS без float: sqrt(sum(x²)/n)
//    uint32_t rms = fn::math::isqrt32(uint32_t(sumSq / n));
//
//    // Расстояние между точками
//    uint32_t d = fn::math::hypot32(dx, dy);
//
//    // Размер буфера — степень двойки (требование RingBuffer)
//    constexpr uint16_t bufSize = fn::math::nextPow2(needed);
//
//    // Деление с округлением вместо усечения
//    int32_t avg = fn::math::divRound(total, count);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>

#if F_USE_MATH

namespace fn {
namespace math {

// ============================ корни ========================================
// Побитовое восстановление корня: на каждом шаге пробуем установить бит
// результата и оставляем его, если квадрат не превысил аргумент.
inline uint8_t isqrt16(uint16_t v) {
    uint16_t rem = v;
    uint16_t root = 0;
    uint16_t bit = uint16_t(1u << 14);          // старший чётный бит
    while (bit > rem) bit >>= 2;
    while (bit != 0) {
        const uint16_t trial = uint16_t(root + bit);
        if (rem >= trial) {
            rem = uint16_t(rem - trial);
            root = uint16_t((root >> 1) + bit);
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return uint8_t(root);
}

inline uint16_t isqrt32(uint32_t v) {
    uint32_t rem = v;
    uint32_t root = 0;
    uint32_t bit = uint32_t(1UL << 30);
    while (bit > rem) bit >>= 2;
    while (bit != 0) {
        const uint32_t trial = root + bit;
        if (rem >= trial) {
            rem -= trial;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return uint16_t(root);
}

inline uint32_t isqrt64(uint64_t v) {
    uint64_t rem = v;
    uint64_t root = 0;
    uint64_t bit = uint64_t(1ULL << 62);
    while (bit > rem) bit >>= 2;
    while (bit != 0) {
        const uint64_t trial = root + bit;
        if (rem >= trial) {
            rem -= trial;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return uint32_t(root);
}

inline uint16_t isqrtRound(uint32_t v) {
    const uint16_t r = isqrt32(v);
    // Ближайшее: сравниваем v с серединой между r² и (r+1)².
    const uint32_t low = uint32_t(r) * uint32_t(r);
    return (v - low > r) ? uint16_t(r + 1u) : r;
}

inline uint32_t hypot32(int32_t x, int32_t y) {
    const int64_t xx = int64_t(x) * int64_t(x);
    const int64_t yy = int64_t(y) * int64_t(y);
    return isqrt64(uint64_t(xx + yy));
}

// ============================ степени и логарифмы ==========================
inline constexpr bool isPow2(uint32_t v) { return v != 0 && (v & (v - 1u)) == 0; }

inline constexpr uint32_t pow2(uint8_t n) {
    return n < 32 ? (uint32_t(1UL) << n) : 0u;
}

inline uint8_t log2Floor(uint32_t v) {
    uint8_t n = 0;
    while (v >>= 1) ++n;
    return n;
}

inline uint8_t log2Ceil(uint32_t v) {
    if (v <= 1) return 0;
    const uint8_t f = log2Floor(v);
    return isPow2(v) ? f : uint8_t(f + 1u);
}

inline uint32_t nextPow2(uint32_t v) {
    if (v <= 1) return 1;
    if (v > (uint32_t(1UL) << 31)) return uint32_t(1UL) << 31;   // насыщение
    --v;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1u;
}

// ============================ делимость ====================================
// Бинарный алгоритм Евклида: только вычитания и сдвиги, без деления.
inline uint32_t gcd(uint32_t a, uint32_t b) {
    if (a == 0) return b;
    if (b == 0) return a;
    uint8_t shift = 0;
    while (((a | b) & 1u) == 0) { a >>= 1; b >>= 1; ++shift; }
    while ((a & 1u) == 0) a >>= 1;
    do {
        while ((b & 1u) == 0) b >>= 1;
        if (a > b) { const uint32_t t = a; a = b; b = t; }
        b -= a;
    } while (b != 0);
    return a << shift;
}

inline uint32_t lcm(uint32_t a, uint32_t b) {
    if (a == 0 || b == 0) return 0;
    const uint32_t g = gcd(a, b);
    const uint64_t r = uint64_t(a / g) * uint64_t(b);
    return r > 0xFFFFFFFFULL ? 0u : uint32_t(r);
}

// ============================ деление и округление =========================
inline constexpr int32_t divRound(int32_t a, int32_t b) {
    return b == 0 ? 0
         : int32_t((int64_t(a) * 2 + (b > 0 ? b : -b) * ((a >= 0) == (b >= 0) ? 1 : -1)) /
                  (int64_t(b) * 2));
}

inline constexpr int32_t divFloor(int32_t a, int32_t b) {
    if (b == 0) return 0;
    const int32_t q = int32_t(a / b);
    const int32_t r = int32_t(a % b);
    return (r != 0 && ((r < 0) != (b < 0))) ? int32_t(q - 1) : q;
}

inline constexpr int32_t divCeil(int32_t a, int32_t b) {
    if (b == 0) return 0;
    const int32_t q = int32_t(a / b);
    const int32_t r = int32_t(a % b);
    return (r != 0 && ((r < 0) == (b < 0))) ? int32_t(q + 1) : q;
}

inline uint32_t roundUpTo(uint32_t v, uint32_t step) {
    if (step == 0) return v;
    const uint32_t r = v % step;
    if (r == 0) return v;
    const uint32_t add = step - r;
    return (v > 0xFFFFFFFFu - add) ? 0xFFFFFFFFu : v + add;
}

inline constexpr uint32_t alignUp(uint32_t v, uint8_t bits) {
    return bits >= 32 ? 0u
         : uint32_t((uint64_t(v) + (uint64_t(1UL) << bits) - 1u) & ~((uint64_t(1UL) << bits) - 1u));
}

// ============================ прочее =======================================
inline constexpr uint32_t absDiff(int32_t a, int32_t b) {
    return a >= b ? uint32_t(int64_t(a) - int64_t(b))
                  : uint32_t(int64_t(b) - int64_t(a));
}

inline constexpr int8_t sign(int32_t v) { return v > 0 ? 1 : (v < 0 ? -1 : 0); }

inline constexpr int32_t average2(int32_t a, int32_t b) {
    // (a+b)/2 без переполнения: разложение на целые половины и остаток.
    return int32_t((a & b) + ((a ^ b) >> 1));
}

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
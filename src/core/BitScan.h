#pragma once
// ============================================================================
//  BitScan.h — сканирование битов: ctz / ffs / разрядность
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Позиции младшего и старшего значащего бита. Основа битовых полей
//    (BitOps.h) и поиска установленных бит в массивах (BitPack.h).
//
//  API
//    ctz8(m) / ctz16(m) / ctz32(m)
//        число младших нулей (0..7/15/31); m == 0 -> 8/16/32 (НЕ UB)
//    ffs8(m) / ffs16(m) / ffs32(m)
//        позиция младшей единицы, 1-based; m == 0 -> 0 (как POSIX ffs)
//    bitWidth8(v) / bitWidth16(v) / bitWidth32(v)
//        число значащих бит (позиция старшей единицы + 1); v == 0 -> 0
//    ctzOf(m), bitWidthOf(v)
//        перегрузки для uint8_t/uint16_t/uint32_t — нужны шаблонному коду
//
//  ОГРАНИЧЕНИЯ / ПОЧЕМУ НЕ __builtin_ctz
//    Реализация — цепочка тернарных тестов. Причины:
//      1) на AVR __builtin_ctz* при переменном аргументе превращается в вызов
//         libgcc (__ctzsi2 и подобных) — лишний flash и такты;
//      2) поведение встроенных функций в constexpr зависит от версии GCC,
//         а слой обязан собираться под gnu++11 / avr-gcc 7.3.0.
//    Цепочка гарантированно constexpr и не тянет libgcc.
//
//  СТОИМОСТЬ (AVR)
//    Константный аргумент -> полная свёртка, 0 тактов. Это типичный случай:
//    маски полей почти всегда константы. Переменный аргумент -> до 7/15/31
//    теста; в горячем пути результат кэшировать.
//
//  ПРИМЕР
//    constexpr uint8_t M = 0x38;
//    static_assert(fn::ctz8(M) == 3, "");
//    static_assert(fn::bitWidth8(M) == 6, "");
//    uint8_t shift = fn::ctz8(M);        // свёрнуто в константу
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>

namespace fn {

inline constexpr uint8_t ctz8(uint8_t m) {
    return m ? ((m & 0x01) ? 0
              : (m & 0x02) ? 1
              : (m & 0x04) ? 2
              : (m & 0x08) ? 3
              : (m & 0x10) ? 4
              : (m & 0x20) ? 5
              : (m & 0x40) ? 6 : 7)
             : 8;
}

inline constexpr uint8_t ctz16(uint16_t m) {
    return m ? ((m & 0x00FFu) ? ctz8(uint8_t(m))
                              : uint8_t(8 + ctz8(uint8_t(m >> 8))))
             : 16;
}

inline constexpr uint8_t ctz32(uint32_t m) {
    return m ? ((m & 0x0000FFFFUL) ? ctz16(uint16_t(m))
                                   : uint8_t(16 + ctz16(uint16_t(m >> 16))))
             : 32;
}

inline constexpr uint8_t ffs8(uint8_t m)   { return m ? uint8_t(ctz8(m) + 1)  : 0; }
inline constexpr uint8_t ffs16(uint16_t m) { return m ? uint8_t(ctz16(m) + 1) : 0; }
inline constexpr uint8_t ffs32(uint32_t m) { return m ? uint8_t(ctz32(m) + 1) : 0; }

inline constexpr uint8_t bitWidth8(uint8_t v) {
    return v ? ((v & 0xF0) ? ((v & 0xC0) ? ((v & 0x80) ? 8 : 7)
                                         : ((v & 0x20) ? 6 : 5))
                           : ((v & 0x0C) ? ((v & 0x08) ? 4 : 3)
                                         : ((v & 0x02) ? 2 : 1)))
             : 0;
}

inline constexpr uint8_t bitWidth16(uint16_t v) {
    return (v & 0xFF00u) ? uint8_t(8 + bitWidth8(uint8_t(v >> 8)))
                         : bitWidth8(uint8_t(v));
}

inline constexpr uint8_t bitWidth32(uint32_t v) {
    return (v & 0xFFFF0000UL) ? uint8_t(16 + bitWidth16(uint16_t(v >> 16)))
                              : bitWidth16(uint16_t(v));
}

// Перегрузки для шаблонного кода: точное совпадение типа, без продвижений.
inline constexpr uint8_t ctzOf(uint8_t m)  { return ctz8(m); }
inline constexpr uint8_t ctzOf(uint16_t m) { return ctz16(m); }
inline constexpr uint8_t ctzOf(uint32_t m) { return ctz32(m); }

inline constexpr uint8_t bitWidthOf(uint8_t v)  { return bitWidth8(v); }
inline constexpr uint8_t bitWidthOf(uint16_t v) { return bitWidth16(v); }
inline constexpr uint8_t bitWidthOf(uint32_t v) { return bitWidth32(v); }

}  // namespace fn

#pragma once
// ============================================================================
//  BitMask.h — конфигурация битового слоя, генераторы масок, одиночные биты
//  Цель: ATmega328P/328, avr-gcc 7.3.0, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Базовый заголовок битового слоя: несёт его конфигурацию и элементарные
//    операции над масками. Заменяет «магические» hex-константы вычисляемыми.
//    Всё constexpr: при константных аргументах в код не попадает ничего.
//
//  КОНФИГУРАЦИЯ (менять здесь или через -D для ВСЕХ TU)
//    FN_BITS_CHECKS         1 (по умолчанию) — проверки границ и nullptr в
//                           битовых массивах; 0 — без проверок (быстрее,
//                           выход за границы = UB, ответственность вызывающего).
//    FN_BITS_BITREV_MODE    0 — таблица 256 байт PROGMEM (быстро, по умолчанию);
//                           1 — таблица 16 байт PROGMEM (−240 байт flash);
//                           2 — SWAR, без таблиц (медленнее всего).
//    FN_BITS_POPCOUNT_TABLE 0 — SWAR popcount, таблиц нет (по умолчанию);
//                           1 — таблица полубайтов, 16 байт PROGMEM, быстрее.
//    FN_BITS_SELFTEST       1 — static_assert-самотест в Bits.h (0 байт кода).
//
//  API
//    bitMask8(n) / bitMask16(n) / bitMask32(n)
//        маска одного бита; n берётся по модулю 8/16/32 (UB исключён)
//    maskLo8(n) / maskLo16(n) / maskLo32(n)
//        n младших единиц; n >= разрядности -> все единицы
//    maskRange8(hi, lo) / maskRange16 / maskRange32
//        единицы в битах [lo..hi] включительно; hi < lo -> 0; hi обрезается
//    bytesNeeded(bits)
//        сколько байт нужно под bits бит, без переполнения при bits = 65535
//    setBit(v, n) / clrBit(v, n) / tglBit(v, n) / putBit(v, n, b)
//        значение с изменённым битом
//    getBit(v, n) -> bool
//    hasSingleBit(v) -> bool      ровно один бит установлен
//
//  ОГРАНИЧЕНИЯ
//    - setBit/clrBit/tglBit/getBit/putBit: n ДОЛЖЕН быть меньше разрядности T.
//      Проверки нет: это скалярные примитивы, проверка убила бы смысл.
//    - Имена намеренно не совпадают с макросами Arduino (bit, bitRead, bitSet,
//      bitClear, bitWrite): препроцессор не знает про namespace.
//    - T должен быть целым. Знаковые допустимы, но сдвиги знаковых лучше
//      не использовать — приводите к fn::UBits<T>.
//
//  СТОИМОСТЬ (AVR)
//    Константные аргументы -> 0 тактов (свёртка). Переменный n -> переменный
//    сдвиг циклом (до ~14 тактов на 8 бит). В горячем цикле выгоднее держать
//    маску в регистровой переменной и сдвигать её.
//
//  ПРИМЕР
//    constexpr uint8_t MODE = fn::maskRange8(5, 3);   // 0b00111000
//    DDRB = fn::bitMask8(5);
//    uint8_t cfg = fn::setBit<uint8_t>(0, 2);         // 0x04
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>

#ifndef FN_BITS_CHECKS
#define FN_BITS_CHECKS 1
#endif

#ifndef FN_BITS_BITREV_MODE
#define FN_BITS_BITREV_MODE 0
#endif

#ifndef FN_BITS_POPCOUNT_TABLE
#define FN_BITS_POPCOUNT_TABLE 0
#endif

#ifndef FN_BITS_SELFTEST
#define FN_BITS_SELFTEST 1
#endif

namespace fn {

// ---------------------------- маска одного бита ----------------------------
inline constexpr uint8_t bitMask8(uint8_t n) { return uint8_t(1u << (n & 7u)); }
inline constexpr uint16_t bitMask16(uint8_t n) { return uint16_t(1u << (n & 15u)); }
inline constexpr uint32_t bitMask32(uint8_t n) { return uint32_t(1UL << (n & 31u)); }

// ---------------------------- n младших единиц -----------------------------
inline constexpr uint8_t maskLo8(uint8_t n) {
    return (n >= 8) ? uint8_t(0xFF) : uint8_t((1u << n) - 1u);
}
inline constexpr uint16_t maskLo16(uint8_t n) {
    return (n >= 16) ? uint16_t(0xFFFF) : uint16_t((1u << n) - 1u);
}
inline constexpr uint32_t maskLo32(uint8_t n) {
    return (n >= 32) ? uint32_t(0xFFFFFFFFUL) : uint32_t((1UL << n) - 1UL);
}

// ---------------------------- диапазон [lo..hi] ----------------------------
inline constexpr uint8_t maskRange8(uint8_t hi, uint8_t lo) {
    return (lo > 7 || hi < lo)
               ? uint8_t(0)
               : uint8_t(maskLo8(uint8_t((hi > 7 ? 7 : hi) + 1)) & uint8_t(~maskLo8(lo)));
}
inline constexpr uint16_t maskRange16(uint8_t hi, uint8_t lo) {
    return (lo > 15 || hi < lo)
               ? uint16_t(0)
               : uint16_t(maskLo16(uint8_t((hi > 15 ? 15 : hi) + 1)) & uint16_t(~maskLo16(lo)));
}
inline constexpr uint32_t maskRange32(uint8_t hi, uint8_t lo) {
    return (lo > 31 || hi < lo)
               ? uint32_t(0)
               : uint32_t(maskLo32(uint8_t((hi > 31 ? 31 : hi) + 1)) & ~maskLo32(lo));
}

// ---------------------------- размер хранилища -----------------------------
// (bits>>3) + (bits&7 ? 1 : 0), а не (bits+7)/8: при bits = 65535 второй
// вариант переполнил бы uint16_t.
inline constexpr uint16_t bytesNeeded(uint16_t bits) {
    return uint16_t((bits >> 3) + ((bits & 7) ? 1 : 0));
}

// ---------------------------- одиночные биты -------------------------------
template <typename T> inline constexpr T setBit(T v, uint8_t n) { return T(v | (T(1) << n)); }
template <typename T> inline constexpr T clrBit(T v, uint8_t n) { return T(v & T(~(T(1) << n))); }
template <typename T> inline constexpr T tglBit(T v, uint8_t n) { return T(v ^ (T(1) << n)); }
template <typename T> inline constexpr bool getBit(T v, uint8_t n) { return (v & (T(1) << n)) != 0; }
template <typename T> inline constexpr T putBit(T v, uint8_t n, bool b) {
    return b ? setBit(v, n) : clrBit(v, n);
}
template <typename T> inline constexpr bool hasSingleBit(T v) {
    return v != 0 && (v & T(v - 1)) == 0;
}

}  // namespace fn

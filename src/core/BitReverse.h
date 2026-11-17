#pragma once
// ============================================================================
//  BitReverse.h — реверс порядка бит
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Зеркалирование бит в байте, слове и буферах. Типовые задачи: разворот
//    знакогенератора и фреймбуфера (ST7567 / SSD1306 и подобные), LSB/MSB-first
//    протоколы, бит-реверсные перестановки.
//
//  API
//    bitrev8(b)                  байт с зеркальным порядком бит
//    bitrev16(w) / bitrev32(dw)  то же для 16 / 32 бит
//    bitrevN(b, n)               реверс младших n бит (n = 0..8), результат
//                                в младших n битах; n == 0 -> 0; n > 8 -> как 8
//    bitrevN32(v, n)             реверс младших n бит (n = 0..32); n == 0 -> 0
//    bitrev8c(b)                 constexpr-версия (static_assert, таблицы)
//    bitrevBuf(p, n)             каждый байт буфера зеркалится на месте
//    mirrorBuf(p, n)             полный реверс битового потока: порядок байт
//                                плюс биты внутри каждого байта
//
//  КОНФИГУРАЦИЯ (FN_BITS_BITREV_MODE в BitMask.h)
//    0 — таблица 256 байт PROGMEM, 1 LPM на байт: быстрее всего (по умолчанию);
//    1 — таблица 16 байт PROGMEM, 2 LPM на байт: экономия 240 байт flash;
//    2 — SWAR без таблиц: экономия 256 байт flash, ~20 тактов на байт.
//    Таблицы определены в Tables.cpp и в SRAM не копируются.
//
//  ОГРАНИЧЕНИЯ
//    - p == nullptr: буферные функции безопасно ничего не делают.
//    - bitrevN берёт именно младшие n бит исходного значения, старшие
//      отбрасываются; проверять их нулевость не нужно.
//    - mirrorBuf работает с целыми байтами. Для набора из N бит, где
//      N % 8 != 0, используйте BitSet::reverseBitOrder().
//    - Порядок байт меняет fn::reverseByteOrder из BitShift.h — отдельной
//      реализации здесь нет.
//
//  СТОИМОСТЬ (AVR)
//    Если ни одна функция реверса не вызывается, таблица вырезается линкером
//    (--gc-sections). bitrev16/32 собираются из bitrev8.
//
//  ПРИМЕР
//    uint8_t col = fn::bitrev8(fontByte);       // столбец символа наоборот
//    fn::mirrorBuf(fb, sizeof(fb));
//    static_assert(fn::bitrev8c(0x01) == 0x80, "");
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, BitMask.h, BitShift.h;
//               таблицы — Tables.cpp
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "BitMask.h"
#include "BitShift.h"

#include <stdint.h>
#include <avr/pgmspace.h>

#if F_USE_BIT_REVERSE

namespace fn {

#if FN_BITS_BITREV_MODE == 0
namespace detail { extern const uint8_t bitrevTable[256] FN_PROGMEM; }
#elif FN_BITS_BITREV_MODE == 1
namespace detail { extern const uint8_t bitrevNibble[16] FN_PROGMEM; }
#endif

// constexpr-версия: рекурсия глубины 8, для compile-time.
inline constexpr uint8_t bitrev8c(uint8_t v, uint8_t acc = 0, uint8_t n = 8) {
    return n ? bitrev8c(uint8_t(v >> 1), uint8_t((acc << 1) | (v & 1u)), uint8_t(n - 1))
             : acc;
}

inline uint8_t bitrev8(uint8_t b) {
#if FN_BITS_BITREV_MODE == 0
    return pgm_read_byte(&detail::bitrevTable[b]);
#elif FN_BITS_BITREV_MODE == 1
    return uint8_t((pgm_read_byte(&detail::bitrevNibble[b & 0x0F]) << 4) |
                    pgm_read_byte(&detail::bitrevNibble[b >> 4]));
#else
    uint8_t v = b;
    v = uint8_t(((v >> 1) & 0x55) | ((v & 0x55) << 1));
    v = uint8_t(((v >> 2) & 0x33) | ((v & 0x33) << 2));
    return uint8_t((v >> 4) | (v << 4));
#endif
}

inline uint16_t bitrev16(uint16_t w) {
    return uint16_t((uint16_t(bitrev8(uint8_t(w))) << 8) | uint16_t(bitrev8(uint8_t(w >> 8))));
}

inline uint32_t bitrev32(uint32_t dw) {
    return (uint32_t(bitrev16(uint16_t(dw))) << 16) | uint32_t(bitrev16(uint16_t(dw >> 16)));
}

inline uint8_t bitrevN(uint8_t b, uint8_t n) {
    if (n == 0) return 0;
    if (n >= 8) return bitrev8(b);
    return uint8_t(bitrev8(b) >> (8u - n));
}

inline uint32_t bitrevN32(uint32_t v, uint8_t n) {
    if (n == 0) return 0;
    if (n >= 32) return bitrev32(v);
    return uint32_t(bitrev32(v) >> (32u - n));
}

inline void bitrevBuf(uint8_t* p, uint16_t n) {
    if (p == nullptr) return;
    uint8_t* end = p + n;
    for (; p != end; ++p) *p = bitrev8(*p);
}

inline void mirrorBuf(uint8_t* p, uint16_t n) {
    if (p == nullptr || n == 0) return;
    reverseByteOrder(p, n);
    bitrevBuf(p, n);
}

}  // namespace fn

#endif  // F_USE_BIT_REVERSE

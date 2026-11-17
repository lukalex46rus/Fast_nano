#pragma once
// ============================================================================
//  BitCount.h — подсчёт единиц (popcount) и чётность (parity)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Подсчёт установленных бит для скаляров и буферов. Используется битовыми
//    массивами (count) и прикладными задачами (чётность в протоколах).
//
//  API
//    popcount8c(v) -> uint8_t
//        constexpr-версия (рекурсия, глубина 8): для static_assert и таблиц
//    popcount8(v) / popcount16(v) / popcount32(v) -> uint8_t
//        число установленных бит (0..8 / 0..16 / 0..32)
//    popcountBuf(p, n) -> uint16_t
//        число единиц в n байтах; p == nullptr -> 0
//    parity8(v) / parity16(v) / parity32(v) -> uint8_t
//        1 при нечётном числе единиц, иначе 0
//
//  КОНФИГУРАЦИЯ
//    FN_BITS_POPCOUNT_TABLE (задаётся в BitMask.h):
//      0 — SWAR, таблиц нет, ~20 тактов на байт (по умолчанию);
//      1 — таблица полубайтов, 16 байт PROGMEM, ~12 тактов на байт.
//    Таблица определена в Tables.cpp, в SRAM не копируется.
//
//  ОГРАНИЧЕНИЯ
//    - popcountBuf возвращает uint16_t: при n > 8191 теоретически возможно
//      переполнение. На 328p такой буфер в SRAM не помещается, но может
//      лежать во внешнем хранилище — учитывайте.
//    - За пределы указанного буфера чтение не выходит.
//    - popcount8c не предназначена для горячего кода: рекурсия разворачивается
//      только при константном аргументе.
//
//  СТОИМОСТЬ (AVR)
//    popcount16/32 собираются из popcount8 — отдельного кода не плодят.
//    popcountBuf — цикл по указателю, без 16-битной индексной арифметики.
//
//  ПРИМЕР
//    static_assert(fn::popcount8c(0xA5) == 4, "");
//    uint8_t ones = fn::popcount8(PORTB);
//    bool odd = fn::parity8(byteFromUart);
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, BitMask.h; таблица — Tables.cpp
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "BitMask.h"

#include <stdint.h>
#include <avr/pgmspace.h>

namespace fn {

#if FN_BITS_POPCOUNT_TABLE
namespace detail {
extern const uint8_t popcountNibble[16] FN_PROGMEM;
}  // namespace detail
#endif

// constexpr-версия: только для compile-time.
inline constexpr uint8_t popcount8c(uint8_t v) {
    return v ? uint8_t((v & 1u) + popcount8c(uint8_t(v >> 1))) : 0;
}

inline uint8_t popcount8(uint8_t v) {
#if FN_BITS_POPCOUNT_TABLE
    return uint8_t(pgm_read_byte(&detail::popcountNibble[v & 0x0F]) +
                   pgm_read_byte(&detail::popcountNibble[v >> 4]));
#else
    v = uint8_t(v - ((v >> 1) & 0x55));
    v = uint8_t((v & 0x33) + ((v >> 2) & 0x33));
    return uint8_t((v + (v >> 4)) & 0x0F);
#endif
}

inline uint8_t popcount16(uint16_t v) {
    return uint8_t(popcount8(uint8_t(v)) + popcount8(uint8_t(v >> 8)));
}

inline uint8_t popcount32(uint32_t v) {
    return uint8_t(popcount16(uint16_t(v)) + popcount16(uint16_t(v >> 16)));
}

inline uint16_t popcountBuf(const uint8_t* p, uint16_t n) {
    if (p == nullptr) return 0;
    const uint8_t* end = p + n;
    uint16_t c = 0;
    for (; p != end; ++p) c += popcount8(*p);
    return c;
}

inline uint8_t parity8(uint8_t v) {
    v = uint8_t(v ^ (v >> 4));
    v = uint8_t(v ^ (v >> 2));
    v = uint8_t(v ^ (v >> 1));
    return uint8_t(v & 1u);
}

inline uint8_t parity16(uint16_t v) { return parity8(uint8_t(v ^ (v >> 8))); }

inline uint8_t parity32(uint32_t v) { return parity16(uint16_t(v ^ (v >> 16))); }

}  // namespace fn

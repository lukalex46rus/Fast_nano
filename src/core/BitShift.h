#pragma once
// ============================================================================
//  BitShift.h — циклические повороты и перестановки байт / полубайтов
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Переупорядочивание бит внутри скаляра и порядка байт в буфере: повороты
//    (CRC, скремблеры, генераторы), обмен полубайтов, смена endianness при
//    обмене с внешней периферией, разворот порядка байт фреймбуфера.
//
//  API
//    rotl8(v, n)  / rotr8(v, n)      циклический поворот, n по модулю 8
//    rotl16(v, n) / rotr16(v, n)     n по модулю 16
//    rotl32(v, n) / rotr32(v, n)     n по модулю 32
//    swapNibbles8(v)                 обмен младшего и старшего полубайта
//    swapBytes16(v) / swapBytes32(v) порядок байт слова
//    reverseByteOrder(p, n)          порядок байт буфера на обратный;
//                                    биты внутри байта не трогаются
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - n == 0 возвращает значение без изменений: сдвига на ширину типа нет,
//      UB исключён.
//    - reverseByteOrder: p == nullptr или n < 2 -> ничего не делает.
//    - Скалярные функции constexpr, побочных эффектов нет.
//    - Логические и арифметические сдвиги здесь не оборачиваются: используйте
//      встроенные << и >> (для знаковых приводите к fn::UBits<T>).
//
//  СТОИМОСТЬ (AVR)
//    Barrel shifter отсутствует: поворот на константу сворачивается или даёт
//    N команд сдвига; на переменную — цикл. Поворот 8-битного на 4 GCC
//    превращает в swap (1 команда). swapBytes16/32 распознаются компилятором
//    и собираются перестановкой регистров, без сдвигов.
//
//  ПРИМЕР
//    uint8_t crc = fn::rotl8(0x81, 1);          // 0x03
//    uint16_t be = fn::swapBytes16(0x1234);     // 0x3412
//    fn::reverseByteOrder(fb, sizeof(fb));      // страницы дисплея наоборот
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>

namespace fn {

inline constexpr uint8_t rotl8(uint8_t v, uint8_t n) {
    return (n & 7u) ? uint8_t((v << (n & 7u)) | (v >> (8u - (n & 7u)))) : v;
}
inline constexpr uint8_t rotr8(uint8_t v, uint8_t n) {
    return (n & 7u) ? uint8_t((v >> (n & 7u)) | (v << (8u - (n & 7u)))) : v;
}

inline constexpr uint16_t rotl16(uint16_t v, uint8_t n) {
    return (n & 15u) ? uint16_t((v << (n & 15u)) | (v >> (16u - (n & 15u)))) : v;
}
inline constexpr uint16_t rotr16(uint16_t v, uint8_t n) {
    return (n & 15u) ? uint16_t((v >> (n & 15u)) | (v << (16u - (n & 15u)))) : v;
}

inline constexpr uint32_t rotl32(uint32_t v, uint8_t n) {
    return (n & 31u) ? uint32_t((v << (n & 31u)) | (v >> (32u - (n & 31u)))) : v;
}
inline constexpr uint32_t rotr32(uint32_t v, uint8_t n) {
    return (n & 31u) ? uint32_t((v >> (n & 31u)) | (v << (32u - (n & 31u)))) : v;
}

inline constexpr uint8_t swapNibbles8(uint8_t v) { return uint8_t((v << 4) | (v >> 4)); }

inline constexpr uint16_t swapBytes16(uint16_t v) { return uint16_t((v << 8) | (v >> 8)); }

inline constexpr uint32_t swapBytes32(uint32_t v) {
    return ((v & 0x000000FFUL) << 24) | ((v & 0x0000FF00UL) << 8) |
           ((v & 0x00FF0000UL) >> 8) | ((v & 0xFF000000UL) >> 24);
}

inline void reverseByteOrder(uint8_t* p, uint16_t n) {
    if (p == nullptr || n < 2) return;
    uint8_t* a = p;
    uint8_t* b = p + (n - 1);
    while (a < b) {
        const uint8_t t = *a;
        *a = *b;
        *b = t;
        ++a;
        --b;
    }
}

}  // namespace fn

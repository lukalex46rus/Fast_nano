#pragma once
// ============================================================================
//  ShiftRegister.h — вывод/ввод байтов через сдвиговый регистр (74HC595/165)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Программный побитовый обмен с внешним сдвиговым регистром. Тактирование
//    вручную, задержка между фронтами задаётся вызывающим (0 = без задержки).
//
//  API (namespace fn::shift)
//    enum Order : uint8_t { MSB_FIRST = 1, LSB_FIRST = 0 }
//    sendByte<DAT, CLK, ORDER>(data, dlyUs = 0)
//    send<DAT, CLK, ORDER>(const uint8_t* data, len, dlyUs = 0)
//    readByte<DAT, CLK, ORDER>(dlyUs = 0) -> uint8_t
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - DAT/CLK — номера выводов платы (BoardPin<N>). Направление выводов
//      настраивает вызывающий: BoardPin<DAT>::output(), BoardPin<CLK>::output().
//    - Параметр задержки назван dlyUs, а НЕ delayUs: имя delayUs затеняло бы
//      функцию fn::delayUs, и вызов разрешался бы как fn::fn::delayUs (INV-24).
//    - Защелка (LATCH/RCLK) — ответственность вызывающего: дёрните её после send.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Шаблоны инстанцируются только при использовании.
//
//  ПРИМЕР
//    fn::BoardPin<8>::output();   // DATA
//    fn::BoardPin<9>::output();   // CLOCK
//    fn::BoardPin<10>::output();  // LATCH
//    fn::BoardPin<10>::low();
//    fn::shift::sendByte<8, 9>(0xA5);
//    fn::BoardPin<10>::high();
//
//  ЗАВИСИМОСТИ: core/Compiler.h, pin/Pin.h, timer/Tick.h
// ============================================================================

#include <stdint.h>

#include "../core/Compiler.h"
#include "../pin/Pin.h"
#include "../timer/Tick.h"

namespace fn {
namespace shift {

enum Order : uint8_t { MSB_FIRST = 1, LSB_FIRST = 0 };

// Отправить байт MSB-first (или LSB-first) с задержкой dlyUs (0 = без задержки).
template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline void sendByte(uint8_t data, uint8_t dlyUs = 0) {
    for (uint8_t i = 0; i < 8; i++) {
        if (ORDER == MSB_FIRST) { BoardPin<DAT>::write(data & 0x80); data <<= 1; }
        else                    { BoardPin<DAT>::write(data & 0x01); data >>= 1; }
        if (dlyUs) ::fn::delayUs(dlyUs);
        BoardPin<CLK>::high();
        if (dlyUs) ::fn::delayUs(dlyUs);
        BoardPin<CLK>::low();
    }
}

template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline void send(const uint8_t* data, uint16_t len, uint8_t dlyUs = 0) {
    while (len--) sendByte<DAT, CLK, ORDER>(*data++, dlyUs);
}

// Прочитать байт MSB-first (или LSB-first).
template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline uint8_t readByte(uint8_t dlyUs = 0) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (ORDER == MSB_FIRST) v <<= 1;
        if (BoardPin<DAT>::read()) {
            if (ORDER == MSB_FIRST) v |= 1;
            else                    v |= uint8_t(0x80 >> i);
        }
        if (dlyUs) ::fn::delayUs(dlyUs);
        BoardPin<CLK>::high();
        if (dlyUs) ::fn::delayUs(dlyUs);
        BoardPin<CLK>::low();
    }
    return v;
}

}  // namespace shift
}  // namespace fn
#pragma once
// ============================================================================
//  ShiftRegister.h — сдвиговые регистры (74HC595 / 74HC165) на BoardPin
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Программная передача/приём байта через два вывода: данные и тактовый.
//    Порядок бит выбирается шаблоном (MSB_FIRST / LSB_FIRST). Задержка между
//    фронтами задаётся параметром dlyUs (0 = без задержки, максимальная скорость).
//
//  API (namespace fn::shift)
//    enum Order : uint8_t { MSB_FIRST = 1, LSB_FIRST = 0 }
//    sendByte<DAT, CLK, ORDER>(data, dlyUs = 0)
//    send<DAT, CLK, ORDER>(const uint8_t* data, len, dlyUs = 0)
//    readByte<DAT, CLK, ORDER>(dlyUs = 0) -> uint8_t
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - DAT/CLK — номера выводов платы (0..19), тип BoardPin<N>, не fn::Pin:
//      fn::Pin — дескриптор-значение, шаблонного API у него нет.
//    - Направление выводов задаёт вызывающий: BoardPin<DAT>::output(),
//      BoardPin<CLK>::output(). Модуль DDR не трогает.
//    - Параметр назван dlyUs, а НЕ delayUs: одноимённый параметр затеняет
//      fn::delayUs, и qualified-id разрешается относительно параметра
//      (ошибка 'delayUs is not a member of fn::fn'). Вызов идёт через ::fn::.
//    - Защелка (RCLK/STCP) — ответственность вызывающего: после send()
//      дёрните её отдельно.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Flash — по инстанцированию шаблона, ~60 байт на функцию.
//
//  ПРИМЕР
//    fn::BoardPin<8>::output();   // DATA
//    fn::BoardPin<9>::output();   // CLK
//    fn::BoardPin<10>::output();  // LATCH
//    fn::shift::sendByte<8, 9>(0xA5);
//    fn::BoardPin<10>::high(); fn::BoardPin<10>::low();
//
//  ЗАВИСИМОСТИ: core/Compiler.h, pin/Pin.h (BoardPin), timer/Tick.h (delayUs)
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

// Отправить буфер подряд, без защёлки между байтами.
template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline void send(const uint8_t* data, uint16_t len, uint8_t dlyUs = 0) {
    while (len--) sendByte<DAT, CLK, ORDER>(*data++, dlyUs);
}

// Прочитать байт: выборка до фронта CLK (данные уже выставлены ведомым).
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
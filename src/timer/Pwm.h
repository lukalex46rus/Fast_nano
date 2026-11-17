#pragma once
// ============================================================================
//  Pwm.h — словарь ШИМ-слоя и карта каналов
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единые типы для трёх таймеров и карта «канал -> вывод платы». Слой
//    ЗАМЕЩАЕТ analogWrite(): после attach() канал принадлежит нам. Вызов
//    analogWrite() на том же выводе перезапишет COM/OCR и испортит скважность
//    молча — так делать нельзя.
//
//  API (namespace fn)
//    enum class PwmMode : uint8_t
//      Fast              TOP = MAX, одинарный наклон
//      PhaseCorrect      TOP = MAX, двойной наклон (частота вдвое ниже)
//      FastTop           TOP = OCRnA (T0/T2) или ICR1 (T1)
//      PhaseCorrectTop   то же с двойным наклоном
//
//    enum class PwmOut : uint8_t
//      Disabled = 0      вывод отключён от таймера (обычный GPIO)
//      Toggle   = 1      только non-PWM; в PWM — служебный режим WGM
//      Normal   = 2      non-inverting: HIGH до compare
//      Inverted = 3      inverting
//
//    enum class PwmChannel : uint8_t { D3, D5, D6, D9, D10, D11 }
//    channelPin(c)     -> uint8_t   номер вывода платы
//    channelTimer(c)   -> uint8_t   0 / 1 / 2
//    channelIsA(c)     -> bool      канал A или B
//    modeUsesOcrATop(m)-> bool      TOP задаётся OCRnA (канал A недоступен)
//    constexpr uint8_t PwmChannelCount = 6
//
//  КАРТА КАНАЛОВ
//    D6  = OC0A (PD6)  Timer0 A   недоступен в *Top (TOP = OCR0A)
//    D5  = OC0B (PD5)  Timer0 B   совпадает с T1 (внешнее тактирование Timer1)
//    D9  = OC1A (PB1)  Timer1 A   доступен всегда: TOP = ICR1
//    D10 = OC1B (PB2)  Timer1 B
//    D11 = OC2A (PB3)  Timer2 A   недоступен в *Top; совпадает с MOSI
//    D3  = OC2B (PD3)  Timer2 B   совпадает с INT1
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Timer0 принадлежит ядру Arduino. ШИМ на D5/D6 меняет только COM и
//      OCR0B, не трогая WGM/CS/TOP: millis() продолжает работать. Режимы
//      *Top на Timer0 запрещены — setFrequency вернёт 0.
//    - Частота Timer0 фиксирована ядром: ~976 Гц (Fast) или ~490 Гц
//      (PhaseCorrect). Изменить нельзя без потери millis().
//    - Один таймер = одна частота на оба канала. Разная скважность — можно.
//    - Направление вывода (DDR) слой НЕ трогает: это делает вызывающий
//      через regs::modeOutput. Иначе timer тянул бы pin-слой.
//    - Конфликты выводов (D3/INT1, D11/MOSI, D5/T1) — ответственность
//      прикладного кода, слой их не проверяет.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт flash и RAM: только enum и constexpr-функции.
//
//  ПРИМЕР
//    fn::Pwm1::begin(fn::PwmMode::FastTop);
//    fn::Pwm1::setFrequency(1000);
//    fn::regs::modeOutput(fn::boardPin(9));
//    fn::Pwm1::attachA(fn::PwmOut::Normal);
//    fn::Pwm1::setDutyPercentA(25);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, pin/Board.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>

#if F_USE_PWM

namespace fn {

enum class PwmMode : uint8_t {
    Fast,
    PhaseCorrect,
    FastTop,
    PhaseCorrectTop
};

enum class PwmOut : uint8_t {
    Disabled = 0,
    Toggle   = 1,
    Normal   = 2,
    Inverted = 3
};

enum class PwmChannel : uint8_t { D3 = 0, D5, D6, D9, D10, D11 };

constexpr uint8_t PwmChannelCount = 6;

constexpr uint8_t channelPin(PwmChannel c) {
    return (c == PwmChannel::D3)  ? 3u
         : (c == PwmChannel::D5)  ? 5u
         : (c == PwmChannel::D6)  ? 6u
         : (c == PwmChannel::D9)  ? 9u
         : (c == PwmChannel::D10) ? 10u
                                  : 11u;
}

constexpr uint8_t channelTimer(PwmChannel c) {
    return (c == PwmChannel::D5 || c == PwmChannel::D6) ? 0u
         : (c == PwmChannel::D9 || c == PwmChannel::D10) ? 1u
                                                         : 2u;
}

constexpr bool channelIsA(PwmChannel c) {
    return c == PwmChannel::D6 || c == PwmChannel::D9 || c == PwmChannel::D11;
}

constexpr bool modeUsesOcrATop(PwmMode m) {
    return m == PwmMode::FastTop || m == PwmMode::PhaseCorrectTop;
}

constexpr bool modeIsPhase(PwmMode m) {
    return m == PwmMode::PhaseCorrect || m == PwmMode::PhaseCorrectTop;
}

}  // namespace fn

#endif  // F_USE_PWM

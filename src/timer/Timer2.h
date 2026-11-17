#pragma once
// ============================================================================
//  Timer2.h — 8-битный таймер с отдельным набором делителей
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Второй 8-битный таймер. Не связан с millis()/micros(), поэтому его можно
//    занимать целиком без потери базы времени. Выходы: OC2A = D11 (PB3),
//    OC2B = D3 (PD3). Внешнего тактового входа нет.
//
//  ОТЛИЧИЕ ДЕЛИТЕЛЕЙ ОТ TIMER0/TIMER1
//    У Timer2 набор CS2x означает: 1, 8, 32, 64, 128, 256, 1024.
//    У Timer0/1: 1, 8, 64, 256, 1024, ExtFall, ExtRise.
//    Коды 3..7 трактуются ПО-РАЗНОМУ, поэтому общий tmr::Clock применять
//    нельзя — здесь свой enum Clock2. Прежняя версия использовала
//    Timer0::Clock и получала неверные периоды (код 3 = Div64 вместо 32).
//
//  API (namespace fn, класс Timer2 — только статические методы)
//    enum class Clock2 : uint8_t
//      Stopped, Div1, Div8, Div32, Div64, Div128, Div256, Div1024
//    using Out = tmr::Out;
//    enum class Mode : uint8_t
//      Normal, Ctc, FastPwm, PhaseCorrect, FastPwmTop, PhaseCorrectTop
//
//    begin(Mode, Clock2) -> Result / takeover(...) / end() / ownsTimer()
//    setMode / setClock / clock()
//    pause() / resume()
//    setTop(uint8_t) / top()            TOP = OCR2A
//    setPeriodUs(us) -> uint32_t        фактический период (CTC)
//    setFrequency(hz) -> uint32_t
//    periodTicks() -> uint16_t
//    outputA(Out) / outputB(Out)
//    setOcrA/B(uint8_t) / ocrA/B() / counter() / setCounter()
//    enableOvf/CompA/CompB() и disable*
//    struct State / save() / restore()
//
//    Асинхронный режим: НЕ поддерживается (см. ограничения).
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Асинхронный режим от 32.768 кГц на ATmega328P невозможен на Nano/Uno:
//      TOSC1/TOSC2 мультиплексированы с PB6/PB7, куда подключён основной
//      кварц 16 МГц. Прежняя версия включала AS2 и уходила в бесконечный
//      цикл ожидания ASSR — МК зависал. Функции удалены намеренно.
//      Для RTC используйте внешний DS3231/PCF8563 по I2C.
//    - analogWrite() на D3/D11 настраивает Timer2 в FastPwm с Div64. После
//      её вызова begin() вернёт Busy — нужен takeover(). Смешивать наш PWM
//      и analogWrite на этих выводах нельзя.
//    - OC2B = D3 совпадает с INT1. При использовании PWM на D3 внешнее
//      прерывание INT1 недоступно.
//    - Максимум периода: 256 * 1024 / F_CPU = 16.4 мс при 16 МГц. Для длинных
//      интервалов используйте Timer1 или программный делитель в ISR.
//    - Делители 32 и 128 дают неточные «круглые» частоты: учитывайте при
//      расчёте UART-подобных таймингов.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 1 байт (сохранённый CS). Flash: ~280 байт при полном использовании.
//
//  ПРИМЕР
//    // Прерывание 1 кГц (период 1000 мкс)
//    fn::Timer2::takeover(fn::Timer2::Mode::Ctc, fn::Timer2::Clock2::Div64);
//    fn::Timer2::setPeriodUs(1000);
//    fn::Timer2::enableCompA();
//    // ISR(TIMER2_COMPA_vect) — в своём модуле, с FN_CLAIM_VECTOR(Timer2Compa,...)
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               timer/TimerCommon.h; реализация — Timer2.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "TimerCommon.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_TIMER2

namespace fn {

class Timer2 {
public:
    // Свой набор делителей: коды CS2x трактуются иначе, чем у Timer0/1.
    enum class Clock2 : uint8_t {
        Stopped = 0,
        Div1    = 1,
        Div8    = 2,
        Div32   = 3,
        Div64   = 4,
        Div128  = 5,
        Div256  = 6,
        Div1024 = 7
    };

    using Out = tmr::Out;

    enum class Mode : uint8_t {
        Normal,
        Ctc,                ///< TOP = OCR2A
        FastPwm,            ///< TOP = 0xFF
        PhaseCorrect,       ///< TOP = 0xFF
        FastPwmTop,         ///< TOP = OCR2A
        PhaseCorrectTop     ///< TOP = OCR2A
    };

    static Result begin(Mode m, Clock2 c);
    static Result takeover(Mode m, Clock2 c);
    static void end();
    static bool ownsTimer();

    static void setMode(Mode m);
    static void setClock(Clock2 c);
    static Clock2 clock();

    static void pause();
    static void resume();

    static void setTop(uint8_t v) { OCR2A = v; }
    static uint8_t top() { return OCR2A; }

    static uint32_t setPeriodUs(uint32_t us);
    static uint32_t setFrequency(uint32_t hz);
    static uint16_t periodTicks();

    static void outputA(Out m);
    static void outputB(Out m);

    static void setOcrA(uint8_t v) { OCR2A = v; }
    static void setOcrB(uint8_t v) { OCR2B = v; }
    static uint8_t ocrA() { return OCR2A; }
    static uint8_t ocrB() { return OCR2B; }

    static uint8_t counter() { return TCNT2; }
    static void setCounter(uint8_t v) { TCNT2 = v; }

    static void enableOvf()   { AtomicBlock l; TIMSK2 |= uint8_t(1u << TOIE2); }
    static void enableCompA() { AtomicBlock l; TIMSK2 |= uint8_t(1u << OCIE2A); }
    static void enableCompB() { AtomicBlock l; TIMSK2 |= uint8_t(1u << OCIE2B); }
    static void disableOvf()   { AtomicBlock l; TIMSK2 &= uint8_t(~(1u << TOIE2)); }
    static void disableCompA() { AtomicBlock l; TIMSK2 &= uint8_t(~(1u << OCIE2A)); }
    static void disableCompB() { AtomicBlock l; TIMSK2 &= uint8_t(~(1u << OCIE2B)); }

    struct State {
        uint8_t tccrA, tccrB, timsk, ocrA, ocrB;
    };
    static State save();
    static void restore(const State& s);

    // Делитель числом — для расчётов вне класса.
    static constexpr uint16_t prescaleOf(Clock2 c) {
        return (c == Clock2::Div1)    ? 1u
             : (c == Clock2::Div8)    ? 8u
             : (c == Clock2::Div32)   ? 32u
             : (c == Clock2::Div64)   ? 64u
             : (c == Clock2::Div128)  ? 128u
             : (c == Clock2::Div256)  ? 256u
             : (c == Clock2::Div1024) ? 1024u
                                      : 0u;
    }

private:
    Timer2() = delete;
};

}  // namespace fn

#endif  // F_USE_TIMER2

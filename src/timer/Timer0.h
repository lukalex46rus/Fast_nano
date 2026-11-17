#pragma once
// ============================================================================
//  Timer0.h — 8-битный таймер ядра Arduino: доступ с явной ответственностью
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Регистровый драйвер Timer0. Особенность одна и главная: таймер принадлежит
//    ядру Arduino (millis/micros/delay/analogWrite на D5/D6). Драйвер не
//    запрещает доступ, но требует явного захвата: begin() при владении ядра
//    вернёт Busy, а takeover() меняет железо и ломает базу времени осознанно.
//    restoreArduino() возвращает конфигурацию ядра, чтобы вернуть millis().
//
//  ВЛАДЕНИЕ
//    Драйвер владение НЕ заявляет (правило R19): заявляет потребитель.
//    FN_TIMER0_DEVICE по умолчанию FN_OWNER_ARDUINO. Вектор TIMER0_OVF
//    принадлежит ядру безусловно — свой ISR на него определить нельзя.
//    Доступны TIMER0_COMPA и TIMER0_COMPB: их можно занимать без потери
//    millis(), если не менять WGM/CS/TOP.
//
//  API (namespace fn, класс Timer0 — только статические методы)
//    using Clock = tmr::Clock;   Stopped/Div1/Div8/Div64/Div256/Div1024/ExtFall/ExtRise
//    using Out   = tmr::Out;     Disable/Toggle/Normal/Inverted
//    enum class Mode : uint8_t
//      Normal, Ctc, FastPwm, PhaseCorrect, FastPwmTop, PhaseCorrectTop
//
//    millisSafe() -> constexpr bool   таймер закреплён за ядром
//    taken() -> bool                  захвачен через takeover()
//    begin(Mode, Clock) -> Result     Busy, если за ядром или уже тактируется
//    takeover(Mode, Clock) -> Result  явный захват; millis() перестаёт идти
//    end()                            остановить, снять прерывания
//    restoreArduino()                 вернуть конфигурацию ядра (FastPWM/64)
//
//    setMode(Mode) / setClock(Clock) / clock()
//    pause() / resume()               resume восстанавливает сохранённый CS
//    setTop(uint8_t) / top()          TOP = OCR0A
//    setPeriodUs(us) -> uint32_t      фактический период; 0 если запрещено
//    setFrequency(hz) -> uint32_t     фактическая частота; 0 если запрещено
//    periodTicks() -> uint16_t        период в тактах таймера
//
//    outputA(Out) / outputB(Out)      COM0A (D6) / COM0B (D5)
//    setOcrA/B(uint8_t) / ocrA/B()
//    counter() / setCounter(uint8_t)
//    enableOvf/CompA/CompB() и disable*
//    struct State / save() / restore(State)
//    lastError() -> Result
//
//  КОДЫ ВОЗВРАТА
//    Ok           выполнено
//    Busy         таймер за ядром (begin/setPeriodUs/setFrequency) либо уже
//                 тактируется (begin)
//    OutOfRange   период недостижим ни при одном делителе
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - setPeriodUs/setFrequency меняют WGM на CTC и TOP на OCR0A. При владении
//      ядра это запрещено: millis() начнёт врать в разы. Функции вернут 0 и
//      выставят lastError() = Busy, пока не вызван takeover().
//    - takeover() не заявляет владение через FN_CLAIM_*: это рантаймовое
//      решение, а compile-time арбитраж остаётся за потребителем. Если вы
//      отдали Timer0 своему модулю, поменяйте FN_TIMER0_DEVICE в Resources.h.
//    - restoreArduino() восстанавливает ровно то, что ставит wiring.c:
//      FastPWM (WGM01|WGM00), делитель 64, OCR0A/B = 0, TIMSK0 = TOIE0,
//      TCNT0 = 0. Счётчик миллисекунд ядра не трогается, но обнуление TCNT0
//      теряет до 1 мс накопленного времени.
//    - pause() сохраняет CS, resume() восстанавливает. Прежняя resume() была
//      пустой с комментарием «CS уже установлен» — после pause() таймер не
//      запускался вовсе.
//    - periodTicks() для PhaseCorrect считает 2*TOP (двойной наклон), а не
//      TOP+1: 510 при TOP=255. Прежние версии таймерного слоя давали 512.
//    - Подбор делителя — tmr::bestClock, без 64-битной арифметики и без
//      таблиц в RAM. Прежняя setPeriodUs тянула __muldi3 + __udivdi3 (~500 Б).
//    - Максимум периода: 256 * 1024 / F_CPU = 16.4 мс при 16 МГц.
//    - ExtFall/ExtRise тактируют от входа T0 = PD4 (D4). При этом D4 нельзя
//      использовать как GPIO.
//    - analogWrite(5,x)/analogWrite(6,x) пишут в те же OCR/COM. После takeover
//      смешивать нельзя.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта (savedCs, taken, lastError). Flash: ~330 байт при полном
//    использовании; неиспользуемые методы вырезаются линкером.
//
//  ПРИМЕР
//    // Безопасно: прерывание 1 кГц на COMPA, millis() продолжает идти
//    fn::Timer0::enableCompA();       // OCR0A ставит свой модуль
//
//    // Осознанный захват: millis() больше не работает
//    fn::Timer0::takeover(fn::Timer0::Mode::Ctc, fn::tmr::Clock::Div64);
//    fn::Timer0::setPeriodUs(1000);
//    ...
//    fn::Timer0::restoreArduino();    // millis() снова идёт
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               timer/TimerCommon.h; реализация — Timer0.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "TimerCommon.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_TIMER0

namespace fn {

class Timer0 {
public:
    using Clock = tmr::Clock;
    using Out   = tmr::Out;

    enum class Mode : uint8_t {
        Normal,
        Ctc,                ///< TOP = OCR0A
        FastPwm,            ///< TOP = 0xFF
        PhaseCorrect,       ///< TOP = 0xFF, двойной наклон
        FastPwmTop,         ///< TOP = OCR0A
        PhaseCorrectTop     ///< TOP = OCR0A, двойной наклон
    };

    static constexpr bool millisSafe() { return FN_TIMER0_DEVICE == FN_OWNER_ARDUINO; }
    static bool taken();

    static Result begin(Mode m, Clock c);
    static Result takeover(Mode m, Clock c);
    static void end();
    static void restoreArduino();

    static void setMode(Mode m);
    static void setClock(Clock c);
    static Clock clock();

    static void pause();
    static void resume();

    static void setTop(uint8_t v) { OCR0A = v; }
    static uint8_t top() { return OCR0A; }

    static uint32_t setPeriodUs(uint32_t us);
    static uint32_t setFrequency(uint32_t hz);
    static uint16_t periodTicks();

    static void outputA(Out m);
    static void outputB(Out m);

    static void setOcrA(uint8_t v) { OCR0A = v; }
    static void setOcrB(uint8_t v) { OCR0B = v; }
    static uint8_t ocrA() { return OCR0A; }
    static uint8_t ocrB() { return OCR0B; }

    static uint8_t counter() { return TCNT0; }
    static void setCounter(uint8_t v) { TCNT0 = v; }

    static void enableOvf()    { AtomicBlock l; TIMSK0 |= uint8_t(1u << TOIE0); }
    static void enableCompA()  { AtomicBlock l; TIMSK0 |= uint8_t(1u << OCIE0A); }
    static void enableCompB()  { AtomicBlock l; TIMSK0 |= uint8_t(1u << OCIE0B); }
    static void disableOvf()   { AtomicBlock l; TIMSK0 &= uint8_t(~(1u << TOIE0)); }
    static void disableCompA() { AtomicBlock l; TIMSK0 &= uint8_t(~(1u << OCIE0A)); }
    static void disableCompB() { AtomicBlock l; TIMSK0 &= uint8_t(~(1u << OCIE0B)); }

    struct State {
        uint8_t tccrA, tccrB, ocrA, ocrB, timsk;
    };
    static State save();
    static void restore(const State& s);

    static Result lastError();

private:
    Timer0() = delete;
    static bool canReconfigure();
};

}  // namespace fn

#endif  // F_USE_TIMER0
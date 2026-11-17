#pragma once
// ============================================================================
//  Timer1.h — 16-битный таймер: период, PWM, захват, арбитраж потребителей
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Регистровый драйвер единственного 16-битного таймера. Разрешение 62.5 нс
//    при 16 МГц и Div1, периоды до 4.19 с без переполнений (Div1024).
//    Выходы OC1A = D9 (PB1), OC1B = D10 (PB2). Вход захвата ICP1 = D8 (PB0).
//    Внешнее тактирование T1 = PD5 (совпадает с OC0B).
//
//  ВЛАДЕНИЕ
//    Драйвер владение НЕ заявляет (R19): FN_CLAIM_TIMER ставит потребитель
//    (Pulse / Pwm1 / Servo / Stepper) у себя. Иначе драйвер запретил бы всех
//    потребителей сразу — именно так ломалась прежняя версия.
//    Рантаймовый арбитраж: acquire(Client) вернёт Busy, если таймер держит
//    другой потребитель. Это advisory-механизм, 1 байт SRAM.
//
//  API (namespace fn, класс Timer1 — только статические методы)
//    using Clock = tmr::Clock;  using Out = tmr::Out;
//    enum class Mode : uint8_t
//      Normal, Ctc, CtcIcr, FastPwm8, FastPwm9, FastPwm10,
//      PhaseCorrect8, PhaseCorrect9, PhaseCorrect10, FastPwmTop, PhaseCorrectTop
//    enum class Client : uint8_t { None, Generic, Pulse, Pwm, Servo, Stepper, User }
//
//    acquire(Client) -> Result / release(Client) / client() -> Client
//    begin(Mode, Clock) -> Result      Busy, если тактируется или занят
//    takeover(Mode, Clock) -> Result   без проверки; для того, кто уже acquire
//    end() / ownsTimer() -> bool
//
//    setMode / setClock / clock()
//    pause() / resume()
//    setTop(uint16_t) / top()          TOP = ICR1
//    setPeriodUs(us) -> uint32_t       фактический период, FastPwmTop
//    setFrequency(hz) -> uint32_t      фактическая частота
//    periodTicks() -> uint32_t
//
//    outputA(Out) / outputB(Out)
//    setOcrA/B(uint16_t) / ocrA/B()    атомарно
//    counter() / setCounter(uint16_t)  атомарно
//    enableOvf/CompA/CompB() и disable*
//
//    captureEdge(bool rising) / noiseCancel(bool)
//    enableCapture() / disableCapture()
//    capture() -> uint16_t             ICR1, атомарно
//    capturePending() -> bool          ICF1
//    overflowPending() -> bool         TOV1
//    clearCaptureFlag() / clearOvfFlag()
//
//    struct State / save() / restore(State)
//    lastError() -> Result
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - setMode сохраняет COM-биты маской 0xF0 в TCCR1A и ICNC1/ICES1 маской
//      0xE0 в TCCR1B. Прежняя версия использовала 0x0C в A и глушила оба
//      PWM-выхода при каждой смене режима.
//    - Все 16-битные обращения (TCNT1/OCR1x/ICR1) атомарны: старший байт
//      идёт через временный регистр TEMP, ISR между байтами портит значение.
//    - setPeriodUs/setFrequency переводят в FastPwmTop (TOP = ICR1). Нужен
//      CTC — задайте setMode(Ctc) после. ICR1 буферизуется: смена частоты на
//      работающем PWM даёт один искажённый период.
//    - periodTicks() для PhaseCorrect считает 2*TOP (510/1022/2046 при
//      8/9/10 бит), а не TOP+1. Прежние версии давали 512/1024/2048.
//    - Mode::FastPwm8/9/10 — TOP 255/511/1023. Нужен TOP=1023 ровно — это
//      FastPwm10; произвольный — FastPwmTop + setTop.
//    - acquire/release — advisory: драйвер не запрещает работу без захвата.
//      Потребитель обязан захватывать, иначе конфликт обнаружится только
//      по искажённому сигналу.
//    - end() гасит таймер и сбрасывает client. Освобождение явное: если
//      клиент хочет сохранить захват, он вызывает release сам.
//    - analogWrite(9,x)/analogWrite(10,x) и Servo.h ядра пишут в те же
//      регистры. Смешивать нельзя.
//    - Подбор делителя — tmr::bestClock, без 64-битной арифметики и таблиц
//      в RAM.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта (savedCs, client, lastError). Flash: ~420 байт при полном
//    использовании; неиспользуемые методы вырезаются.
//
//  ПРИМЕР
//    // Потребитель заявляет владение у себя в .cpp:
//    FN_CLAIM_TIMER(1, FN_OWNER_PULSE);
//    fn::Timer1::acquire(fn::Timer1::Client::Pulse);
//    fn::Timer1::takeover(fn::Timer1::Mode::Normal, fn::tmr::Clock::Div1);
//    fn::Timer1::captureEdge(true);
//    fn::Timer1::enableCapture();
//
//    // Меандр 1 кГц на D9
//    fn::Timer1::begin(fn::Timer1::Mode::FastPwmTop, fn::tmr::Clock::Div8);
//    fn::Timer1::setFrequency(1000);
//    fn::Timer1::outputA(fn::tmr::Out::Normal);
//    fn::Timer1::setOcrA(fn::Timer1::top() / 2);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               timer/TimerCommon.h; реализация — Timer1.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "TimerCommon.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_TIMER1

namespace fn {

class Timer1 {
public:
    using Clock = tmr::Clock;
    using Out   = tmr::Out;

    enum class Mode : uint8_t {
        Normal,             ///< TOP = 0xFFFF
        Ctc,                ///< TOP = OCR1A
        CtcIcr,             ///< TOP = ICR1 (OCR1A свободен под PWM)
        FastPwm8,           ///< TOP = 0x00FF
        FastPwm9,           ///< TOP = 0x01FF
        FastPwm10,          ///< TOP = 0x03FF
        PhaseCorrect8,
        PhaseCorrect9,
        PhaseCorrect10,
        FastPwmTop,         ///< TOP = ICR1
        PhaseCorrectTop     ///< TOP = ICR1
    };

    enum class Client : uint8_t { None = 0, Generic, Pulse, Pwm, Servo, Stepper, User };

    // --- арбитраж между потребителями (advisory) ---
    static Result acquire(Client c);
    static void release(Client c);
    static Client client();

    static Result begin(Mode m, Clock c);
    static Result takeover(Mode m, Clock c);
    static void end();
    static bool ownsTimer();

    static void setMode(Mode m);
    static void setClock(Clock c);
    static Clock clock();

    static void pause();
    static void resume();

    static void setTop(uint16_t v);
    static uint16_t top();

    static uint32_t setPeriodUs(uint32_t us);
    static uint32_t setFrequency(uint32_t hz);
    static uint32_t periodTicks();

    static void outputA(Out m);
    static void outputB(Out m);

    static void setOcrA(uint16_t v);
    static void setOcrB(uint16_t v);
    static uint16_t ocrA();
    static uint16_t ocrB();

    static uint16_t counter();
    static void setCounter(uint16_t v);

    static void enableOvf()    { AtomicBlock l; TIMSK1 |= uint8_t(1u << TOIE1); }
    static void enableCompA()  { AtomicBlock l; TIMSK1 |= uint8_t(1u << OCIE1A); }
    static void enableCompB()  { AtomicBlock l; TIMSK1 |= uint8_t(1u << OCIE1B); }
    static void disableOvf()   { AtomicBlock l; TIMSK1 &= uint8_t(~(1u << TOIE1)); }
    static void disableCompA() { AtomicBlock l; TIMSK1 &= uint8_t(~(1u << OCIE1A)); }
    static void disableCompB() { AtomicBlock l; TIMSK1 &= uint8_t(~(1u << OCIE1B)); }

    // --- захват: нужно InputCapture, чтобы не лезть в регистры ---
    static void captureEdge(bool rising);
    static void noiseCancel(bool on);
    static void enableCapture()  { AtomicBlock l; TIMSK1 |= uint8_t(1u << ICIE1); }
    static void disableCapture() { AtomicBlock l; TIMSK1 &= uint8_t(~(1u << ICIE1)); }
    static uint16_t capture();
    static bool capturePending();
    static bool overflowPending();
    static void clearCaptureFlag();
    static void clearOvfFlag();

    struct State {
        uint8_t tccrA, tccrB, timsk;
        uint16_t ocrA, ocrB, icr;
    };
    static State save();
    static void restore(const State& s);

    static Result lastError();

private:
    Timer1() = delete;
};

}  // namespace fn

#endif  // F_USE_TIMER1
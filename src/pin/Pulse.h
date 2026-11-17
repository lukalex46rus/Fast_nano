#pragma once
// ============================================================================
//  Pulse.h — Input Capture на ICP1 (D8): период и частота с точностью до такта
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Аппаратный захват момента фронта: Timer1 считает свободно, фронт
//    защёлкивается в ICR1 без участия CPU. Разрешение — 1 такт (62.5 нс при
//    16 МГц и Div1), в отличие от pulseIn с его разрешением micros() (4 мкс).
//    Счётчик переполнений достраивает момент захвата до 32 бит.
//
//  ОТНОШЕНИЕ К timer/Timer1
//    Модуль — ПОТРЕБИТЕЛЬ Timer1, а не второй регистровый драйвер. Вся
//    настройка идёт через Timer1::takeover / setClock / captureEdge /
//    noiseCancel / enableCapture / enableOvf, чтение — через Timer1::capture /
//    counter / overflowPending. Прямых записей в TCCR1x/TIMSK1/TIFR1 здесь нет.
//    Владение Timer1 заявляет Pulse (FN_CLAIM_TIMER(1, FN_OWNER_PULSE)),
//    сам драйвер Timer1 владение не заявляет — иначе он запретил бы всех
//    потребителей сразу.
//
//  ВЛАДЕНИЕ
//    Векторы TIMER1_CAPT и TIMER1_OVF, таймер Timer1 целиком.
//    Несовместим с PwmTimer1, Servo на Timer1, StepperDriver на Timer1,
//    timer/Timer1.h как отдельным потребителем. Конфликт ловится на компиляции
//    (FN_CLAIM_*) и в рантайме (Timer1::acquire вернёт Busy).
//    Вывод D8 (PB0) — вход ICP1. Компаратор с ACIC=1 тоже подаёт сигнал на
//    ICP1: одновременно внешний захват и компараторный невозможны.
//
//  API (namespace fn, класс InputCapture — только статические методы)
//    enum class Edge  : uint8_t  { Falling, Rising }
//    enum class Presc : uint16_t { Div1=1, Div8=8, Div64=64, Div256=256, Div1024=1024 }
//    using Callback = void (*)(uint32_t periodTicks)
//
//    begin(Edge = Rising, Presc = Div1, bool noiseFilter = false) -> Result
//    end()
//    setEdge(Edge)
//    active() -> bool
//
//    lastTicks()    -> uint32_t   момент последнего захвата (32 бита)
//    periodTicks()  -> uint32_t   период между двумя последними фронтами
//    periodMicros() -> uint32_t   период в мкс, без переполнения
//    frequencyMhz() -> uint32_t   частота в миллигерцах (Гц * 1000)
//    captures()     -> uint32_t   число захватов с begin()
//    missed()       -> uint16_t   захватов потеряно (ISR не успел прочитать)
//    prescaler()    -> uint16_t   текущий делитель
//
//    attach(Callback) -> Result / detach() / isAttached()
//    lastError() -> Result
//
//  КОДЫ ВОЗВРАТА begin
//    Ok        запущен
//    Busy      Timer1 занят другим потребителем (Pwm/Servo/Stepper)
//    BadArg    недопустимый делитель
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - periodTicks() до второго фронта равен 0. Проверяйте captures() >= 2.
//    - Пространство тиков 32-битное и заворачивается: 268 с при Div1,
//      4.19 с * 1024 = 76 ч при Div1024. Период измеряется корректно, пока он
//      меньше 2^32 тиков; беззнаковое вычитание обрабатывает заворот само.
//    - periodMicros() считается как ticks * presc / (F_CPU/1e6) с делением
//      ДО умножения: прежняя версия делала t << sh и переполняла uint32 при
//      длинных периодах и prescaler > 1. При выходе за uint32 возвращается
//      0xFFFFFFFF (насыщение), а не завёрнутое значение.
//    - frequencyMhz() = 1e9 / periodMicros(). Погрешность определяется
//      квантованием периода до 1 мкс: выше ~10 кГц она превышает 1%.
//      Для высоких частот считайте из periodTicks() самостоятельно:
//      f_Гц = (F_CPU / prescaler) / periodTicks().
//    - Коррекция переполнения детерминированная, без магических порогов:
//      в ISR читаются ICR1, затем TOV1, затем TCNT1; если переполнение не
//      обработано И счётчик не заворачивался с момента захвата (cnt >= icr),
//      значит переполнение случилось ДО захвата и локально добавляется +1.
//      Глобальный счётчик overflows при этом не меняется.
//      Порядок чтений критичен: TOV1 после TCNT1 дал бы ложное +1.
//    - missed() растёт, если на входе в ISR флаг ICF1 уже выставлен снова:
//      предыдущий захват не был прочитан. Значение периода в этом случае
//      недостоверно. Причина — слишком высокая частота или тяжёлый коллбэк.
//    - noiseFilter=true включает ICNC1: аппаратный фильтр подавления помех,
//      требует 4 одинаковых выборки на входе. Полезно для механических
//      датчиков и длинных линий; добавляет задержку 4 такта таймера.
//    - Коллбэк вызывается ИЗ ISR захвата: короткий, без delay/Serial/malloc.
//      Разделяемые данные — volatile, многобайтовые читать atomicRead32.
//    - D8 настраивается как вход без подтяжки; подтяжку задаёт вызывающий.
//    - Прерывания захвата и переполнения обязательны оба: без OVF счётчик
//      эпох не ведётся, без CAPT нет измерения.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 22 байта (overflows/last/prev/per/count по 4, lost 2, presc 2,
//          sh 1, active 1) + 2 байта коллбэка + 1 байт lastError.
//    Flash: ~420 байт вместе с двумя ISR, без soft-float и __udivdi3.
//    ISR захвата: ~45 тактов без коллбэка.
//
//  ПРИМЕР
//    // Расходомер с датчиком Холла на D8
//    void setup() {
//        fn::regs::modeInputPullup(fn::boardPin(8));
//        fn::InputCapture::begin(fn::InputCapture::Edge::Falling,
//                                fn::InputCapture::Presc::Div8, true);
//    }
//    void loop() {
//        if (fn::InputCapture::captures() >= 2) {
//            uint32_t us = fn::InputCapture::periodMicros();
//            uint32_t t  = fn::InputCapture::periodTicks();   // точное значение
//        }
//        if (fn::InputCapture::missed()) { /* пропуск фронтов */ }
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               core/Registers.h, timer/Timer1.h, timer/TimerCommon.h,
//               pin/Board.h; реализация — Pulse.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "../core/Registers.h"
#include "../timer/Timer1.h"
#include "../timer/TimerCommon.h"
#include "Board.h"

#include <stdint.h>

#if F_USE_PULSE

namespace fn {

class InputCapture {
public:
    enum class Edge : uint8_t { Falling = 0, Rising = 1 };

    enum class Presc : uint16_t {
        Div1 = 1, Div8 = 8, Div64 = 64, Div256 = 256, Div1024 = 1024
    };

    using Callback = void (*)(uint32_t periodTicks);

    static Result begin(Edge e = Edge::Rising,
                        Presc p = Presc::Div1,
                        bool noiseFilter = false);
    static void end();
    static void setEdge(Edge e);
    static bool active();

    static uint32_t lastTicks();
    static uint32_t periodTicks();
    static uint32_t periodMicros();
    static uint32_t frequencyMhz();
    static uint32_t captures();
    static uint16_t missed();
    static uint16_t prescaler();

    static Result attach(Callback cb);
    static void detach();
    static bool isAttached();

    static Result lastError();

private:
    InputCapture() = delete;
};

}  // namespace fn

#endif  // F_USE_PULSE
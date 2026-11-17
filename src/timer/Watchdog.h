#pragma once
// ============================================================================
//  Watchdog.h — сторожевой таймер: сброс по зависанию, периодические прерывания
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Два слоя. Watchdog — регистровый: включить/выключить/покормить, без ISR
//    и без состояния. fn::wdt — прикладной: единственный ISR(WDT_vect) с
//    коллбэком и режимом «сначала прерывание, потом сброс».
//    Crash (core) работает с WDT напрямую через avr-libc и от этого модуля
//    не зависит: ядро не тянет прикладной слой.
//
//  API — регистровый слой (класс Watchdog, только статические методы)
//    enum class Mode : uint8_t { Reset, Interrupt, InterruptReset }
//    enum class Timeout : uint8_t
//      T16ms, T32ms, T64ms, T125ms, T250ms, T500ms, T1s, T2s, T4s, T8s
//    disable()
//    enable(Mode, Timeout) -> Result
//    feed()                              сброс счётчика (wdr)
//    isEnabled() -> bool
//    mode() -> Mode                      текущий режим по WDTCSR
//    timeout() -> Timeout                текущий период
//    timeoutMs() -> uint16_t
//    transitionToReset()                 из ISR: следующий тайм-аут -> сброс
//    clearResetFlag()                    сбросить MCUSR.WDRF
//    timeoutBits(Timeout) -> uint8_t     constexpr, кодирование WDP3..0
//    msOf(Timeout) -> uint16_t           constexpr
//
//  API — прикладной слой (namespace fn::wdt)
//    using Callback = void (*)(void* ctx)
//    enum class Action : uint8_t { Repeat, ResetAfter }
//    begin(Action, Watchdog::Timeout, Callback cb = nullptr, void* ctx = nullptr)
//      -> Result
//    end() / feed() / active() -> bool
//    fired() -> uint32_t                 число срабатываний ISR
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - MCUSR.WDRF форсирует WDE=1 аппаратно. Если флаг остался от прошлого
//      сброса, enable(Interrupt) даст Interrupt+Reset. Модуль сбрасывает
//      WDRF перед настройкой — прежняя версия этого не делала.
//    - Изменение WDE требует последовательности: WDCE|WDE, затем новый байт
//      в течение 4 тактов. Обе записи под AtomicBlock, между ними ничего.
//    - После сброса по WDT таймер остаётся включён с минимальным периодом
//      (15 мс). Crash выключает его в .init3 до main(); если Crash не
//      используется, вызывайте disable() первым делом в setup().
//    - Периоды 4 с и 8 с используют бит WDP3 (позиция 5), отделённый от
//      WDP2..0 (позиции 2..0). Прямой сдвиг номера Timeout в регистр давать
//      нельзя — только timeoutBits().
//    - ISR НЕ вызывает wdr: счётчик сброшен аппаратно при входе в
//      прерывание, а лишний wdr отодвигал сброс на полный период.
//    - ISR переустанавливает WDIE только если модуль ещё активен: коллбэк
//      мог вызвать wdt::end(), и безусловная запись воскрешала WDT.
//    - Коллбэк вызывается ИЗ ISR: короткий, без delay/Serial/malloc.
//      Разделяемые данные — volatile, многобайтовые через atomicRead*.
//    - Action::ResetAfter: первый тайм-аут вызывает коллбэк, затем модуль
//      включает WDE и сбрасывает WDIE. Следующий тайм-аут — аппаратный
//      сброс. feed() внутри коллбэка отменяет сброс.
//    - Action::Repeat: коллбэк на каждом тайм-ауте, сброса нет. Пригоден
//      для пробуждения из sleep: WDT — единственный источник, работающий
//      в power-down.
//    - Точность: WDT тактируется отдельным RC 128 кГц, разброс до ±10% по
//      температуре и напряжению. Для точных интервалов не годится.
//    - При прошитом фьюзе WDTON выключить WDT невозможно: disable()
//      вернётся, но железо продолжит работать.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 11 байт при использовании fn::wdt (cb 2, ctx 2, action 1,
//    count 4, isActive 1) + 1 байт политики нет. Регистровый слой — 0.
//    Flash: ~200 байт вместе с ISR.
//
//  ПРИМЕР
//    // Защита от зависания
//    fn::Watchdog::enable(fn::Watchdog::Mode::Reset, fn::Watchdog::Timeout::T2s);
//    void loop() { doWork(); fn::Watchdog::feed(); }
//
//    // Диагностика перед сбросом
//    fn::wdt::begin(fn::wdt::Action::ResetAfter, fn::Watchdog::Timeout::T1s,
//                   [](void*) { saveCrashReason(); });
//
//    // Пробуждение каждые 250 мс
//    fn::wdt::begin(fn::wdt::Action::Repeat, fn::Watchdog::Timeout::T250ms,
//                   [](void*) { ++wakeCount; });
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h;
//               реализация — Watchdog.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_WDT

namespace fn {

class Watchdog {
public:
    enum class Mode : uint8_t {
        Reset,           ///< WDE: сброс по тайм-ауту
        Interrupt,       ///< WDIE: только прерывание, повторяется
        InterruptReset   ///< WDIE|WDE: первое прерывание, затем сброс
    };

    enum class Timeout : uint8_t {
        T16ms = 0, T32ms, T64ms, T125ms, T250ms, T500ms, T1s, T2s, T4s, T8s
    };

    static void disable();
    static Result enable(Mode m, Timeout t);
    static void feed();
    static bool isEnabled();
    static Mode mode();
    static Timeout timeout();
    static uint16_t timeoutMs();
    static void transitionToReset();
    static void clearResetFlag();

    // Кодирование периода: WDP3 отделён (бит 5) от WDP2..0 (биты 2..0).
    static constexpr uint8_t timeoutBits(Timeout t) {
        return (uint8_t(t) >= 8)
                   ? uint8_t((1u << WDP3) | (uint8_t(t) - 8u))
                   : uint8_t(t);
    }

    static constexpr uint16_t msOf(Timeout t) {
        return (t == Timeout::T16ms)  ? 16u
             : (t == Timeout::T32ms)  ? 32u
             : (t == Timeout::T64ms)  ? 64u
             : (t == Timeout::T125ms) ? 125u
             : (t == Timeout::T250ms) ? 250u
             : (t == Timeout::T500ms) ? 500u
             : (t == Timeout::T1s)    ? 1000u
             : (t == Timeout::T2s)    ? 2000u
             : (t == Timeout::T4s)    ? 4000u
                                      : 8000u;
    }

private:
    Watchdog() = delete;
    static void write(uint8_t wd);
};

namespace wdt {

using Callback = void (*)(void* ctx);

enum class Action : uint8_t {
    Repeat,      ///< коллбэк на каждом тайм-ауте, сброса нет
    ResetAfter   ///< коллбэк один раз, следующий тайм-аут -> сброс
};

Result begin(Action a, Watchdog::Timeout t, Callback cb = nullptr, void* ctx = nullptr);
void end();
void feed();
bool active();
uint32_t fired();

}  // namespace wdt
}  // namespace fn

#endif  // F_USE_WDT
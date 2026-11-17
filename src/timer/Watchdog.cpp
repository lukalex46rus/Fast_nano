// ============================================================================
//  Watchdog.cpp — регистровый слой WDT и единственный ISR(WDT_vect)
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Настройка WDT с корректной timed-последовательностью и прикладная
//    обёртка с коллбэком. Владеет вектором WDT единолично.
//
//  TIMED-ПОСЛЕДОВАТЕЛЬНОСТЬ
//    Изменение WDE и делителя разрешено только так: записать WDCE|WDE,
//    затем в течение 4 тактов записать новый байт. Обе записи под
//    AtomicBlock, между ними нет ничего — иначе последовательность рвётся
//    и запись игнорируется молча.
//
//  ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ПРЕЖНЕЙ ВЕРСИИ
//    - enable() не сбрасывал MCUSR.WDRF. Флаг аппаратно форсирует WDE=1,
//      поэтому режим Interrupt фактически давал Interrupt+Reset.
//    - ISR вызывал wdr в начале: при InterruptReset это отодвигало сброс
//      на полный период. Теперь wdr не вызывается вовсе.
//    - ISR безусловно переустанавливал WDIE / звал transitionToReset даже
//      после wdt::end() из коллбэка — WDT оставался включённым.
//    - cb/mode не были volatile: компилятор вправе кэшировать их в регистре.
//    - transitionToReset оставлял WDIE: теперь сбрасывает его явно.
//    - Ручной SREG/cli заменён на AtomicBlock (барьеры памяти).
//
//  ОГРАНИЧЕНИЯ
//    - При фьюзе WDTON disable() не действует.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Watchdog.h, core/Atomic.h, core/Resources.h, avr/wdt.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_WDT

#include "Watchdog.h"

#include <avr/interrupt.h>
#include <avr/wdt.h>

FN_CLAIM_VECTOR(Wdt, FN_OWNER_USER);

namespace fn {
namespace wdt_detail {

wdt::Callback volatile cb = nullptr;
void* volatile ctx = nullptr;
volatile wdt::Action action = wdt::Action::Repeat;
volatile uint32_t count = 0;
volatile bool isActive = false;

}  // namespace wdt_detail

// ============================ регистровый слой =============================
void Watchdog::write(uint8_t wd) {
    AtomicBlock lock;
    WDTCSR = uint8_t((1u << WDCE) | (1u << WDE));   // разрешить изменение
    WDTCSR = wd;                                    // в пределах 4 тактов
}

void Watchdog::clearResetFlag() {
    AtomicBlock lock;
    MCUSR = uint8_t(MCUSR & uint8_t(~(1u << WDRF)));
}

void Watchdog::disable() {
    clearResetFlag();
    write(0);
}

Result Watchdog::enable(Mode m, Timeout t) {
    if (uint8_t(t) > uint8_t(Timeout::T8s)) return Result::BadArg;

    // WDRF форсирует WDE — без сброса флага режим Interrupt не получится.
    clearResetFlag();

    uint8_t wd = timeoutBits(t);
    switch (m) {
        case Mode::Reset:          wd = uint8_t(wd | (1u << WDE)); break;
        case Mode::Interrupt:      wd = uint8_t(wd | (1u << WDIE)); break;
        case Mode::InterruptReset: wd = uint8_t(wd | (1u << WDE) | (1u << WDIE)); break;
    }
    write(wd);
    feed();
    return Result::Ok;
}

void Watchdog::feed() {
    __asm__ __volatile__("wdr" ::: "memory");
}

bool Watchdog::isEnabled() {
    return (WDTCSR & uint8_t((1u << WDE) | (1u << WDIE))) != 0;
}

Watchdog::Mode Watchdog::mode() {
    const uint8_t r = WDTCSR;
    const bool wde = (r & (1u << WDE)) != 0;
    const bool wdie = (r & (1u << WDIE)) != 0;
    if (wde && wdie) return Mode::InterruptReset;
    if (wde) return Mode::Reset;
    return Mode::Interrupt;
}

Watchdog::Timeout Watchdog::timeout() {
    const uint8_t r = WDTCSR;
    const uint8_t lo = uint8_t(r & 0x07u);
    const uint8_t hi = (r & (1u << WDP3)) ? 8u : 0u;
    return static_cast<Timeout>(hi + lo);
}

uint16_t Watchdog::timeoutMs() { return msOf(timeout()); }

void Watchdog::transitionToReset() {
    // WDIE сбрасываем намеренно: следующий тайм-аут должен дать сброс,
    // а не второе прерывание. Делитель не меняем.
    const uint8_t bits = uint8_t(WDTCSR & uint8_t((1u << WDP3) | 0x07u));
    write(uint8_t(bits | (1u << WDE)));
}

// ============================ прикладной слой ==============================
namespace wdt {

Result begin(Action a, Watchdog::Timeout t, Callback f, void* c) {
    {
        AtomicBlock lock;
        wdt_detail::cb = f;
        wdt_detail::ctx = c;
        wdt_detail::action = a;
        wdt_detail::count = 0;
        wdt_detail::isActive = true;
    }
    const Watchdog::Mode m = (a == Action::Repeat) ? Watchdog::Mode::Interrupt
                                                   : Watchdog::Mode::InterruptReset;
    const Result r = Watchdog::enable(m, t);
    if (!ok(r)) {
        AtomicBlock lock;
        wdt_detail::isActive = false;
        wdt_detail::cb = nullptr;
        wdt_detail::ctx = nullptr;
    }
    return r;
}

void end() {
    Watchdog::disable();
    AtomicBlock lock;
    wdt_detail::cb = nullptr;
    wdt_detail::ctx = nullptr;
    wdt_detail::isActive = false;
}

void feed() { Watchdog::feed(); }

bool active() { return wdt_detail::isActive; }

uint32_t fired() { return atomicRead32(&wdt_detail::count); }

}  // namespace wdt
}  // namespace fn

// ============================================================================
//  ISR — единственный в проекте для вектора WDT
// ============================================================================
ISR(WDT_vect) {
    using namespace fn;

    // wdr не нужен: счётчик сброшен аппаратно при входе в ISR.
    wdt_detail::count += 1UL;

    const wdt::Callback f = wdt_detail::cb;
    if (f) f(wdt_detail::ctx);

    if (!wdt_detail::isActive) return;      // коллбэк мог вызвать wdt::end()

    if (wdt_detail::action == wdt::Action::ResetAfter) {
        Watchdog::transitionToReset();
    } else {
        // Аппарат сбрасывает WDIE при срабатывании: переустанавливаем,
        // чтобы прерывания продолжались.
        WDTCSR = uint8_t(WDTCSR | (1u << WDIE));
    }
}

#endif  // F_USE_WDT
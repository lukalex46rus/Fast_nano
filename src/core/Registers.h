#pragma once
// ============================================================================
//  Registers.h — доступ к портам B/C/D без таблиц в RAM
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единая точка работы с GPIO. Вместо трёх массивов указателей (18 байт
//    SRAM + 18 байт образа во flash) адрес регистра вычисляется арифметикой:
//    на ATmega328P регистры портов лежат тройками подряд в порядке PIN, DDR, PORT.
//
//        PINB 0x23  DDRB 0x24  PORTB 0x25
//        PINC 0x26  DDRC 0x27  PORTC 0x28
//        PIND 0x29  DDRD 0x2A  PORTD 0x2B
//
//    База берётся из символа &PINB, а не хардкодится: подстановку делает
//    компилятор. Для константного порта адрес сворачивается в ldi.
//
//  API (namespace fn::regs)
//    sfr(Port, Reg) -> volatile uint8_t*   адрес регистра, nullptr при ошибке
//    valid(Port)                           порт существует
//    validMask(Port)                       маска физически доступных бит
//    layoutOk()                            проверка раскладки в рантайме
//
//    Порт целиком:
//      readInput(Port)      -> uint8_t     состояние выводов (PINx)
//      readLatch(Port)      -> uint8_t     защёлка (PORTx)
//      readDir(Port)        -> uint8_t     направление (DDRx)
//      write(Port, v)       -> Result
//      setMask / clearMask / writeMask(Port, mask[, level]) -> Result
//      setMaskAtomic / clearMaskAtomic / writeMaskAtomic    -> Result
//      toggle(Port, mask)   -> Result      аппаратный toggle записью в PINx
//
//    Режимы (порядок записи без глюков):
//      modeOutput(Port, mask, level = false) -> Result
//      modeInput(Port, mask)                 -> Result
//      modeInputPullup(Port, mask)           -> Result
//      modeOutputAtomic / modeInputAtomic / modeInputPullupAtomic
//
//    Один вывод (fn::Pin):
//      set(Pin) / clear(Pin) / toggle(Pin) / write(Pin, bool) -> Result
//      read(Pin) -> bool                   чтение PINx
//      modeOutput / modeInput / modeInputPullup(Pin[, level]) -> Result
//
//    Константный вывод (адрес известен компилятору -> sbi/cbi):
//      FastPin<PinId>::set/clear/toggle/write/read/mode
//      FN_FAST_PIN(port, bit)              объявление типа
//
//    Питание и сон:
//      struct Snapshot { uint8_t port[3]; uint8_t ddr[3]; }
//      save() / restore(Snapshot)
//      pullupsDisable(bool)                глобальный бит PUD в MCUCR
//      idleUnused(Port, mask)              неиспользуемые -> вход+подтяжка
//
//    Совместимость со старым API (замена таблиц):
//      portOut(idx) / portDir(idx) / portIn(idx) -> volatile uint8_t&
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Раскладка PIN/DDR/PORT с шагом 3 — свойство именно ATmega328P.
//      На других AVR модуль не переносится без пересчёта базы.
//      Проверить в рантайме: FN_ASSERT(fn::regs::layoutOk()).
//    - PC7 физически отсутствует, PC6 = RESET (доступен только при RSTDISBL,
//      после чего ISP-прошивка невозможна). PB6/PB7 заняты кварцем,
//      PD0/PD1 — UART. validMask() учитывает только PC7; остальные
//      ограничения — ответственность слоя pin/Board.h.
//    - toggle() пишет маску в PINx: на 328p это аппаратное переключение
//      соответствующих бит PORTx. Одна запись, без RMW, атомарно само по себе.
//      НЕ применять к регистрам, где запись единицы имеет другой смысл.
//    - Неатомарные setMask/clearMask — это RMW. Если порт трогает ISR,
//      берите версии *Atomic либо FastPin с константной маской (sbi/cbi).
//    - Проверки границ включаются FN_BITS_CHECKS (по умолчанию 1).
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Вычисление адреса — 2-4 команды против LDS×2 у таблицы.
//    FastPin с константным одиночным битом — sbi/cbi, 2 такта.
//
//  ПРИМЕР
//    using namespace fn;
//    constexpr Pin LED(Port::B, 5);
//    regs::modeOutput(LED, true);
//    regs::toggle(LED);                       // 1 запись в PINB
//
//    FN_FAST_PIN(Port::B, 5);                 // typedef FastPinB5
//    FastPinB5::set();                        // sbi 0x25, 5
//
//    regs::setMaskAtomic(Port::D, 0x3C);
//    auto snap = regs::save();                // перед сном
//    regs::restore(snap);                     // после пробуждения
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, Atomic.h, Assert.h, avr/io.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "Atomic.h"
#include "Assert.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_REGISTERS

#ifndef FN_BITS_CHECKS
#define FN_BITS_CHECKS 1
#endif

namespace fn {
namespace regs {

// ============================ адресация ====================================
inline constexpr bool valid(Port p) {
    return static_cast<uint8_t>(p) < PortCount;
}

// Маска физически существующих выводов порта. PC7 на 328p нет.
inline constexpr uint8_t validMask(Port p) {
    return (p == Port::C) ? uint8_t(0x7F) : uint8_t(0xFF);
}

inline volatile uint8_t* sfr(Port p, Reg r) {
#if FN_BITS_CHECKS
    if (!valid(p)) return nullptr;
#endif
    const uint8_t base = static_cast<uint8_t>(_SFR_MEM_ADDR(PINB));
    const uint8_t off = static_cast<uint8_t>(base + 3u * static_cast<uint8_t>(p) +
                                             static_cast<uint8_t>(r));
    return reinterpret_cast<volatile uint8_t*>(static_cast<uint16_t>(off));
}

// Проверка раскладки. constexpr по адресам SFR невозможен (reinterpret_cast
// из целого не является constant expression), поэтому проверка рантаймовая.
inline bool layoutOk() {
    for (uint8_t i = 0; i < PortCount; ++i) {
        const Port p = static_cast<Port>(i);
        volatile uint8_t* pin = sfr(p, Reg::Pin);
        volatile uint8_t* ddr = sfr(p, Reg::Ddr);
        volatile uint8_t* out = sfr(p, Reg::Port);
        if (ddr != pin + 1 || out != pin + 2) return false;
    }
    return sfr(Port::B, Reg::Port) == &PORTB &&
           sfr(Port::C, Reg::Port) == &PORTC &&
           sfr(Port::D, Reg::Port) == &PORTD;
}

// ============================ порт целиком =================================
inline uint8_t readInput(Port p) {
    volatile uint8_t* r = sfr(p, Reg::Pin);
    return r ? *r : 0u;
}
inline uint8_t readLatch(Port p) {
    volatile uint8_t* r = sfr(p, Reg::Port);
    return r ? *r : 0u;
}
inline uint8_t readDir(Port p) {
    volatile uint8_t* r = sfr(p, Reg::Ddr);
    return r ? *r : 0u;
}

inline Result write(Port p, uint8_t v) {
    volatile uint8_t* r = sfr(p, Reg::Port);
    if (!r) return Result::OutOfRange;
    *r = v;
    return Result::Ok;
}

namespace detail {

// Единственная реализация RMW по маске. Atomic — константа шаблона,
// мёртвая ветка вырезается компилятором.
template <bool Atomic>
inline Result maskRmw(Port p, uint8_t mask, bool setBits) {
    volatile uint8_t* r = sfr(p, Reg::Port);
    if (!r) return Result::OutOfRange;
    if (Atomic) {
        AtomicBlock lock;
        *r = setBits ? static_cast<uint8_t>(*r | mask)
                     : static_cast<uint8_t>(*r & static_cast<uint8_t>(~mask));
    } else {
        *r = setBits ? static_cast<uint8_t>(*r | mask)
                     : static_cast<uint8_t>(*r & static_cast<uint8_t>(~mask));
    }
    return Result::Ok;
}

// Единственная реализация смены режима. Порядок: сначала PORT, потом DDR —
// вывод не проходит через неопределённое состояние.
template <bool Atomic>
inline Result modeImpl(Port p, uint8_t mask, uint8_t dir, uint8_t level) {
    volatile uint8_t* out = sfr(p, Reg::Port);
    volatile uint8_t* ddr = sfr(p, Reg::Ddr);
    if (!out || !ddr) return Result::OutOfRange;
    if (Atomic) {
        AtomicBlock lock;
        *out = static_cast<uint8_t>((*out & static_cast<uint8_t>(~mask)) |
                                    (level & mask));
        *ddr = static_cast<uint8_t>((*ddr & static_cast<uint8_t>(~mask)) |
                                    (dir & mask));
    } else {
        *out = static_cast<uint8_t>((*out & static_cast<uint8_t>(~mask)) |
                                    (level & mask));
        *ddr = static_cast<uint8_t>((*ddr & static_cast<uint8_t>(~mask)) |
                                    (dir & mask));
    }
    return Result::Ok;
}

}  // namespace detail

inline Result setMask(Port p, uint8_t m)   { return detail::maskRmw<false>(p, m, true); }
inline Result clearMask(Port p, uint8_t m) { return detail::maskRmw<false>(p, m, false); }
inline Result setMaskAtomic(Port p, uint8_t m)   { return detail::maskRmw<true>(p, m, true); }
inline Result clearMaskAtomic(Port p, uint8_t m) { return detail::maskRmw<true>(p, m, false); }

inline Result writeMask(Port p, uint8_t m, bool level) {
    return level ? setMask(p, m) : clearMask(p, m);
}
inline Result writeMaskAtomic(Port p, uint8_t m, bool level) {
    return level ? setMaskAtomic(p, m) : clearMaskAtomic(p, m);
}

// Аппаратное переключение: запись единицы в PINx инвертирует бит PORTx.
inline Result toggle(Port p, uint8_t mask) {
    volatile uint8_t* r = sfr(p, Reg::Pin);
    if (!r) return Result::OutOfRange;
    *r = mask;
    return Result::Ok;
}

inline Result modeOutput(Port p, uint8_t m, bool level = false) {
    return detail::modeImpl<false>(p, m, m, level ? m : uint8_t(0));
}
inline Result modeInput(Port p, uint8_t m) {
    return detail::modeImpl<false>(p, m, 0, 0);
}
inline Result modeInputPullup(Port p, uint8_t m) {
    return detail::modeImpl<false>(p, m, 0, m);
}
inline Result modeOutputAtomic(Port p, uint8_t m, bool level = false) {
    return detail::modeImpl<true>(p, m, m, level ? m : uint8_t(0));
}
inline Result modeInputAtomic(Port p, uint8_t m) {
    return detail::modeImpl<true>(p, m, 0, 0);
}
inline Result modeInputPullupAtomic(Port p, uint8_t m) {
    return detail::modeImpl<true>(p, m, 0, m);
}

// ============================ один вывод ===================================
inline Result set(Pin pin) {
    return pin.valid() ? setMask(pin.port(), pin.mask()) : Result::BadArg;
}
inline Result clear(Pin pin) {
    return pin.valid() ? clearMask(pin.port(), pin.mask()) : Result::BadArg;
}
inline Result toggle(Pin pin) {
    return pin.valid() ? toggle(pin.port(), pin.mask()) : Result::BadArg;
}
inline Result write(Pin pin, bool level) {
    return level ? set(pin) : clear(pin);
}
inline bool read(Pin pin) {
    if (!pin.valid()) return false;
    return (readInput(pin.port()) & pin.mask()) != 0;
}
inline Result modeOutput(Pin pin, bool level = false) {
    return pin.valid() ? modeOutput(pin.port(), pin.mask(), level) : Result::BadArg;
}
inline Result modeInput(Pin pin) {
    return pin.valid() ? modeInput(pin.port(), pin.mask()) : Result::BadArg;
}
inline Result modeInputPullup(Pin pin) {
    return pin.valid() ? modeInputPullup(pin.port(), pin.mask()) : Result::BadArg;
}

// ============================ питание и сон ================================
struct Snapshot {
    uint8_t port[PortCount];
    uint8_t ddr[PortCount];
};

inline Snapshot save() {
    Snapshot s;
    for (uint8_t i = 0; i < PortCount; ++i) {
        const Port p = static_cast<Port>(i);
        s.port[i] = readLatch(p);
        s.ddr[i] = readDir(p);
    }
    return s;
}

inline void restore(const Snapshot& s) {
    for (uint8_t i = 0; i < PortCount; ++i) {
        const Port p = static_cast<Port>(i);
        volatile uint8_t* out = sfr(p, Reg::Port);
        volatile uint8_t* ddr = sfr(p, Reg::Ddr);
        if (!out || !ddr) continue;
        AtomicBlock lock;
        *out = s.port[i];
        *ddr = s.ddr[i];
    }
}

// PUD = 1 отключает все подтяжки разом (при DDR=0).
inline void pullupsDisable(bool off) {
    if (off) {
        MCUCR = static_cast<uint8_t>(MCUCR | (1u << PUD));
    } else {
        MCUCR = static_cast<uint8_t>(MCUCR & static_cast<uint8_t>(~(1u << PUD)));
    }
}

// Неиспользуемые выводы — вход с подтяжкой: минимальное потребление.
inline Result idleUnused(Port p, uint8_t mask) {
    return modeInputPullup(p, static_cast<uint8_t>(mask & validMask(p)));
}

// ============================ совместимость ================================
// Замена старых таблиц PORT_[i] / DDR_[i] / PIN_[i].
// Было: *regs::PORT_[i] |= m;   Стало: regs::portOut(i) |= m;
inline volatile uint8_t& portOut(uint8_t idx) { return *sfr(static_cast<Port>(idx), Reg::Port); }
inline volatile uint8_t& portDir(uint8_t idx) { return *sfr(static_cast<Port>(idx), Reg::Ddr); }
inline volatile uint8_t& portIn(uint8_t idx)  { return *sfr(static_cast<Port>(idx), Reg::Pin); }

}  // namespace regs

// ==================== константный вывод (sbi/cbi) ==========================
// Адрес и маска известны компилятору -> одиночные команды вместо RMW.
template <uint8_t PinId>
struct FastPin {
    static constexpr Pin pin() { return Pin::fromId(PinId); }
    static constexpr uint8_t mask() { return pin().mask(); }

    static volatile uint8_t* out() {
        return regs::sfr(pin().port(), Reg::Port);
    }
    static volatile uint8_t* in() {
        return regs::sfr(pin().port(), Reg::Pin);
    }

    static void set()   { *out() = static_cast<uint8_t>(*out() | mask()); }
    static void clear() { *out() = static_cast<uint8_t>(*out() & static_cast<uint8_t>(~mask())); }
    static void write(bool v) { v ? set() : clear(); }

    // Аппаратный toggle: одна запись, атомарно.
    static void toggle() { *in() = mask(); }

    static bool read() { return (*in() & mask()) != 0; }

    static void modeOutput(bool level = false) {
        regs::modeOutput(pin(), level);
    }
    static void modeInput() { regs::modeInput(pin()); }
    static void modeInputPullup() { regs::modeInputPullup(pin()); }
};

#define FN_FAST_PIN(portEnum, bit) \
    using FN_CONCAT(FastPin_, FN_CONCAT(portEnum, bit)) = \
        ::fn::FastPin<static_cast<uint8_t>((static_cast<uint8_t>(portEnum) << 3) | (bit))>

}  // namespace fn

#endif  // F_USE_REGISTERS

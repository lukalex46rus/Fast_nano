// ============================================================================
//  Irq.cpp — диспетчер INT0/INT1 (через ядро Arduino) и PCINT0..2 (свои ISR)
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Пул обработчиков с контекстом и единственные в проекте ISR для PCINT.
//    INT0/INT1 идут через attachInterrupt ядра Arduino: его ISR(INT0_vect) и
//    ISR(INT1_vect) определены в WInterrupts.c безусловно, свои определить
//    нельзя (multiple definition). Цена — один лишний косвенный вызов, ~10 тактов.
//
//  УСТРОЙСТВО
//    - Пул: FN_IRQ_SLOTS записей {Pin, Callback, ctx}. Линейный поиск: при 8
//      слотах дешевле хэша и не платит за неиспользуемые выводы.
//    - Маска PCMSKx пересчитывается из пула (refreshPort) с учётом флага on —
//      одно место истины, disable() не откатывается при attach/detach.
//    - PCICR включается, когда на порту есть активный вывод, гаснет, когда
//      не осталось ни одного.
//    - ISR хранит prev-снимок порта и передаёт (prev ^ now) & mask: только
//      реально изменившиеся биты. Прежняя версия передавала уровень и дёргала
//      коллбэки всех выводов порта при любом изменении.
//    - Простые коллбэки void(*)() оборачиваются в simpleShim, ctx = сам функтор.
//
//  ОГРАНИЧЕНИЯ
//    - Регистрация/снятие под AtomicBlock: ISR не увидит полусобранный слот.
//    - Коллбэки и ctx объявлены volatile: меняются в основном коде, читаются в ISR.
//    - attachInterrupt ядра требует Arduino.h — это прикладной слой, допустимо.
//    - Файл должен лежать в src/pin/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Irq.h, core/BitScan.h (ctz8), Arduino.h, avr/io.h, avr/interrupt.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_IRQ

#include "Irq.h"
#include "../core/BitScan.h"

#include <Arduino.h>
#include <avr/io.h>
#include <avr/interrupt.h>

// Владение векторами. INT0/INT1 остаются за ядром Arduino — мы лишь клиент.
FN_CLAIM_VECTOR(Pcint0, FN_OWNER_IRQ);
FN_CLAIM_VECTOR(Pcint1, FN_OWNER_IRQ);
FN_CLAIM_VECTOR(Pcint2, FN_OWNER_IRQ);

namespace fn {
namespace irq {
namespace detail {

struct Slot {
    Pin pin;
    Callback volatile cb;
    void* volatile ctx;
    bool on;                 ///< включён ли бит в PCMSK (G-4)
};

Slot slots[MaxSlots];

struct ExtSlot {
    Callback volatile cb;
    void* volatile ctx;
};

ExtSlot ext[2] = {{nullptr, nullptr}, {nullptr, nullptr}};

// Обёртка для простого коллбэка: ctx хранит сам указатель на функцию.
void simpleShim(void* ctx, Pin) {
    reinterpret_cast<SimpleCb>(ctx)();
}

inline volatile uint8_t* pcMaskReg(Port p) {
    switch (p) {
        case Port::B: return &PCMSK0;
        case Port::C: return &PCMSK1;
        default:      return &PCMSK2;
    }
}

inline uint8_t pcieBit(Port p) { return uint8_t(PCIE0 + static_cast<uint8_t>(p)); }

// Пересчёт маски порта из пула. Вызывать под AtomicBlock.
// Учитывает флаг on: disable() сохраняется при последующих attach/detach.
void refreshPort(Port p) {
    uint8_t mask = 0;
    for (uint8_t i = 0; i < MaxSlots; ++i) {
        if (slots[i].cb != nullptr && slots[i].on && slots[i].pin.port() == p) {
            mask = uint8_t(mask | slots[i].pin.mask());
        }
    }
    *pcMaskReg(p) = mask;
    if (mask) {
        PCICR = uint8_t(PCICR | pcieBit(p));
    } else {
        PCICR = uint8_t(PCICR & uint8_t(~pcieBit(p)));
    }
}

int8_t findSlot(Pin p) {
    for (uint8_t i = 0; i < MaxSlots; ++i) {
        if (slots[i].cb != nullptr && slots[i].pin == p) return int8_t(i);
    }
    return -1;
}

int8_t findFree() {
    for (uint8_t i = 0; i < MaxSlots; ++i) {
        if (slots[i].cb == nullptr) return int8_t(i);
    }
    return -1;
}

// Вызов обработчиков изменившихся битов. Только из ISR.
void dispatch(Port p, uint8_t changed) {
    while (changed) {
        const uint8_t bit = ctz8(changed);
        changed = uint8_t(changed & uint8_t(changed - 1));
        const Pin target(p, bit);
        for (uint8_t i = 0; i < MaxSlots; ++i) {
            const Callback c = slots[i].cb;
            if (c != nullptr && slots[i].pin == target) {
                c(slots[i].ctx, target);
                break;
            }
        }
    }
}

// Thunk'и для attachInterrupt ядра Arduino.
void extThunk0() {
    const Callback c = ext[0].cb;
    if (c) c(ext[0].ctx, Pin(Port::D, 2));
}
void extThunk1() {
    const Callback c = ext[1].cb;
    if (c) c(ext[1].ctx, Pin(Port::D, 3));
}

}  // namespace detail

// ============================ ExternalInt ==================================
bool ExternalInt::lineOf(Pin p, Line& out) {
    const int8_t l = intLineOf(p);
    if (l < 0) return false;
    out = static_cast<Line>(l);
    return true;
}

Result ExternalInt::attach(Line l, Edge e, Callback cb, void* ctx) {
    const uint8_t idx = static_cast<uint8_t>(l);
    if (idx > 1 || cb == nullptr) return Result::BadArg;

    {
        AtomicBlock lock;
        if (detail::ext[idx].cb != nullptr) return Result::Busy;
        detail::ext[idx].ctx = ctx;
        detail::ext[idx].cb = cb;
    }
    // Ядро само настроит EICRA/EIMSK и сбросит флаг.
    ::attachInterrupt(idx, (idx == 0) ? detail::extThunk0 : detail::extThunk1,
                      static_cast<int>(e));
    return Result::Ok;
}

Result ExternalInt::attach(Line l, Edge e, SimpleCb cb) {
    if (cb == nullptr) return Result::BadArg;
    return attach(l, e, detail::simpleShim, reinterpret_cast<void*>(cb));
}

void ExternalInt::detach(Line l) {
    const uint8_t idx = static_cast<uint8_t>(l);
    if (idx > 1) return;
    ::detachInterrupt(idx);
    AtomicBlock lock;
    detail::ext[idx].cb = nullptr;
    detail::ext[idx].ctx = nullptr;
}

void ExternalInt::setEdge(Line l, Edge e) {
    const uint8_t idx = static_cast<uint8_t>(l);
    if (idx > 1) return;
    // Ядро не даёт сменить режим без перерегистрации — снимаем и ставим заново.
    const Callback c = detail::ext[idx].cb;
    void* const ctx = detail::ext[idx].ctx;
    if (c == nullptr) return;
    ::detachInterrupt(idx);
    ::attachInterrupt(idx, (idx == 0) ? detail::extThunk0 : detail::extThunk1,
                      static_cast<int>(e));
}

bool ExternalInt::isAttached(Line l) {
    const uint8_t idx = static_cast<uint8_t>(l);
    return (idx <= 1) && (detail::ext[idx].cb != nullptr);
}

// ============================ PinChange ====================================
Result PinChange::attach(Pin p, Callback cb, void* ctx) {
    if (cb == nullptr || !p.valid()) return Result::BadArg;

    AtomicBlock lock;
    if (detail::findSlot(p) >= 0) return Result::Busy;
    const int8_t i = detail::findFree();
    if (i < 0) return Result::NoBuffer;

    detail::slots[i].pin = p;
    detail::slots[i].ctx = ctx;
    detail::slots[i].on = true;
    detail::slots[i].cb = cb;
    detail::refreshPort(p.port());
    return Result::Ok;
}

void PinChange::detach(Pin p) {
    if (!p.valid()) return;
    AtomicBlock lock;
    const int8_t i = detail::findSlot(p);
    if (i < 0) return;

    detail::slots[i].cb = nullptr;
    detail::slots[i].ctx = nullptr;
    detail::slots[i].on = false;

    // Уплотнение: последнюю занятую запись переносим на освободившееся место.
    uint8_t last = MaxSlots;
    for (uint8_t k = 0; k < MaxSlots; ++k) {
        if (detail::slots[k].cb != nullptr) last = k;
    }
    if (last < MaxSlots && last != static_cast<uint8_t>(i)) {
        detail::slots[i].pin = detail::slots[last].pin;
        detail::slots[i].ctx = detail::slots[last].ctx;
        detail::slots[i].on  = detail::slots[last].on;
        detail::slots[i].cb  = detail::slots[last].cb;
        detail::slots[last].cb = nullptr;
        detail::slots[last].ctx = nullptr;
        detail::slots[last].on = false;
        // Перенесённый слот мог быть с другого порта — обновляем оба.
        if (detail::slots[i].pin.port() != p.port()) {
            detail::refreshPort(detail::slots[i].pin.port());
        }
    }
    detail::refreshPort(p.port());
}

void PinChange::enable(Pin p) {
    if (!p.valid()) return;
    AtomicBlock lock;
    const int8_t i = detail::findSlot(p);
    if (i < 0) return;
    detail::slots[i].on = true;
    detail::refreshPort(p.port());
}

void PinChange::disable(Pin p) {
    if (!p.valid()) return;
    AtomicBlock lock;
    const int8_t i = detail::findSlot(p);
    if (i < 0) return;
    detail::slots[i].on = false;
    detail::refreshPort(p.port());
}

bool PinChange::isAttached(Pin p) {
    if (!p.valid()) return false;
    AtomicBlock lock;
    return detail::findSlot(p) >= 0;
}

uint8_t PinChange::used() {
    AtomicBlock lock;
    uint8_t n = 0;
    for (uint8_t i = 0; i < MaxSlots; ++i) {
        if (detail::slots[i].cb != nullptr) ++n;
    }
    return n;
}

}  // namespace irq
}  // namespace fn

// ============================================================================
//  ISR — только PCINT. INT0/INT1 принадлежат ядру Arduino.
// ============================================================================
ISR(PCINT0_vect) {
    static uint8_t prev = 0;
    const uint8_t now = PINB;
    const uint8_t changed = uint8_t((prev ^ now) & PCMSK0);
    prev = now;
    fn::irq::detail::dispatch(fn::Port::B, changed);
}

ISR(PCINT1_vect) {
    static uint8_t prev = 0;
    const uint8_t now = PINC;
    const uint8_t changed = uint8_t((prev ^ now) & PCMSK1);
    prev = now;
    fn::irq::detail::dispatch(fn::Port::C, changed);
}

ISR(PCINT2_vect) {
    static uint8_t prev = 0;
    const uint8_t now = PIND;
    const uint8_t changed = uint8_t((prev ^ now) & PCMSK2);
    prev = now;
    fn::irq::detail::dispatch(fn::Port::D, changed);
}

#endif  // F_USE_IRQ

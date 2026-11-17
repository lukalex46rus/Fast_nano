#pragma once
// ============================================================================
//  Irq.h — единый диспетчер внешних прерываний и Pin Change
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственная точка регистрации обработчиков INT0/INT1 и PCINT0..2.
//    Прикладные модули (Encoder, Quadrature, PulseInput, Keypad, UartSoft)
//    регистрируют коллбэк здесь, а не пишут свои ISR. Это снимает
//    "multiple definition of __vector_N" при любом сочетании модулей.
//
//  ПОЧЕМУ В pin/, А НЕ В core/
//    Диспетчер принимает номер вывода платы (D0..D13, A0..A5) и знает раскладку.
//    Ядро оперирует только «порт + бит». Зависимость односторонняя:
//    pin/Irq -> core/Registers, core/Types, core/Atomic, core/Resources, pin/Board.
//
//  КОЛЛБЭК С КОНТЕКСТОМ
//    Основной тип: void(*)(void* ctx, Pin changed). Контекст — указатель на
//    объект-владелец, changed — какой вывод сработал. Это позволяет одному
//    классу обслуживать N экземпляров без реестров и статических thunk'ов:
//      QuadDecoder регистрирует себя дважды (пин A и пин B) с ctx = this;
//      в коллбэке сравнивает changed со своими пинами.
//    Для простых случаев есть перегрузка с void(*)() — ctx передаётся nullptr.
//
//  API (namespace fn::irq)
//    using Callback = void (*)(void* ctx, Pin changed)
//    using SimpleCb = void (*)()
//    constexpr uint8_t MaxSlots = FN_IRQ_SLOTS   (по умолчанию 8)
//
//    ExternalInt (INT0 = D2, INT1 = D3):
//      enum class Line : uint8_t { Int0, Int1 }
//      enum class Edge : uint8_t { LowLevel, Change, Falling, Rising }
//      attach(Line, Edge, Callback, void* ctx) -> Result
//      attach(Line, Edge, SimpleCb)            -> Result
//      detach(Line)
//      setEdge(Line, Edge)
//      isAttached(Line) -> bool
//      lineOf(Pin, Line& out) -> bool
//
//    PinChange (любой цифровой вывод):
//      attach(Pin, Callback, void* ctx) -> Result
//      attach(Pin, SimpleCb)            -> Result
//      attach(uint8_t arduinoPin, Callback, void* ctx) -> Result
//      attach(uint8_t arduinoPin, SimpleCb)            -> Result
//      detach(Pin) / detach(uint8_t arduinoPin)
//      enable(Pin) / disable(Pin)        маска; коллбэк сохраняется
//      isAttached(Pin) -> bool
//      used() / free() -> uint8_t
//
//  КОДЫ ВОЗВРАТА attach
//    Ok          зарегистрировано и включено
//    BadArg      cb == nullptr, вывод не существует, PC7, или не D2/D3 для INT
//    Busy        на этот вывод/линию уже зарегистрирован обработчик
//    NoBuffer    пул слотов исчерпан (увеличьте FN_IRQ_SLOTS)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - INT0/INT1 обслуживаются ядром Arduino через attachInterrupt + thunk.
//      Свои ISR(INT0_vect)/ISR(INT1_vect) невозможны: они уже определены в
//      WInterrupts.c. Следствие: не смешивайте наш ExternalInt с прямыми
//      вызовами attachInterrupt на D2/D3 — ядро перезапишет указатель.
//    - PCINT0..2 — наши собственные ISR, в ядре Arduino их нет.
//    - Пул общий для трёх портов: FN_IRQ_SLOTS слотов по 5 байт
//      (Pin + Callback + ctx). Дефолт 8 = 40 байт SRAM + 6 байт на INT.
//    - Один вывод = один обработчик. Повторный attach -> Busy. Нужен веер —
//      делайте его внутри своего коллбэка.
//    - Коллбэк вызывается ИЗ ISR: короткий, без delay/Serial/malloc.
//      Разделяемые данные — volatile, многобайтовые читать atomicRead16/32.
//    - PCINT срабатывает на ЛЮБОЕ изменение. Направление определяется
//      сравнением с прошлым уровнем внутри коллбэка: fn::regs::read(changed).
//    - ISR передаёт только реально изменившиеся биты (prev ^ now), а не
//      текущий уровень: удержание одного вывода не дёргает чужие коллбэки.
//    - Порядок вызова внутри порта — по возрастанию номера бита.
//    - PB6/PB7 (кварц), PD0/PD1 (UART), PC6 (RESET) формально валидны:
//      ответственность за выбор вывода на прикладном коде.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: FN_IRQ_SLOTS * 5 + 6 байт. При 8 слотах = 46 байт.
//    Flash: ~400 байт вместе с тремя ISR и двумя thunk'ами.
//    ISR: поиск по пулу только для изменившихся битов (ctz8), не перебор 8.
//
//  ПРИМЕР
//    // Простой случай
//    volatile uint16_t ticks = 0;
//    void onTick() { ++ticks; }
//    fn::irq::PinChange::attach(4, onTick);
//
//    // Класс с несколькими экземплярами
//    class Encoder {
//        static void isr(void* ctx, Pin changed) {
//            static_cast<Encoder*>(ctx)->onChange(changed);
//        }
//    public:
//        fn::Result begin(uint8_t a, uint8_t b) {
//            auto r = fn::irq::PinChange::attach(a, isr, this);
//            if (!fn::ok(r)) return r;
//            return fn::irq::PinChange::attach(b, isr, this);
//        }
//        void onChange(fn::Pin p) { /* ... */ }
//    };
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h, core/Atomic.h,
//               core/Resources.h, pin/Board.h; реализация — Irq.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "Board.h"

#include <stdint.h>

#ifndef FN_IRQ_SLOTS
#define FN_IRQ_SLOTS 8
#endif

#if F_USE_IRQ

namespace fn {
namespace irq {

using Callback = void (*)(void* ctx, Pin changed);
using SimpleCb = void (*)();

constexpr uint8_t MaxSlots = FN_IRQ_SLOTS;

// ============================ внешние прерывания ===========================
class ExternalInt {
public:
    enum class Line : uint8_t { Int0 = 0, Int1 = 1 };
    enum class Edge : uint8_t { LowLevel = 0, Change = 1, Falling = 2, Rising = 3 };

    static Result attach(Line l, Edge e, Callback cb, void* ctx);
    static Result attach(Line l, Edge e, SimpleCb cb);
    static void detach(Line l);
    static void setEdge(Line l, Edge e);
    static bool isAttached(Line l);

    static bool lineOf(Pin p, Line& out);
};

// ============================ Pin Change ===================================
class PinChange {
public:
    static Result attach(Pin p, Callback cb, void* ctx);
    static Result attach(Pin p, SimpleCb cb);
    static Result attach(uint8_t arduinoPin, Callback cb, void* ctx);
    static Result attach(uint8_t arduinoPin, SimpleCb cb);

    static void detach(Pin p);
    static void detach(uint8_t arduinoPin);

    static void enable(Pin p);
    static void disable(Pin p);

    static bool isAttached(Pin p);
    static uint8_t used();
    static uint8_t free() { return uint8_t(MaxSlots - used()); }
};

}  // namespace irq
}  // namespace fn

#endif  // F_USE_IRQ

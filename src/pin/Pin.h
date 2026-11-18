#pragma once
// ============================================================================
//  Pin.h — compile-time вывод платы: операции сворачиваются в sbi/cbi/in
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Обёртка над fn::FastPin с нумерацией платы. Номер вывода известен
//    компилятору, поэтому адрес регистра и маска — константы, и одиночные
//    операции превращаются в sbi/cbi (2 такта) вместо read-modify-write.
//    Своей логики работы с регистрами здесь нет: она вся в core/Registers.h.
//
//  API
//    template <uint8_t N> class BoardPin      N = номер вывода платы (0..19)
//      output(level = false) / input() / inputPullup()
//      high() / low() / write(bool) / toggle()
//      read() -> bool
//      num() / pin() / port() / bitIndex() / mask()      всё constexpr
//      setAtomic() / clearAtomic()                      RMW под AtomicBlock
//
//    FN_BOARD_PIN(n, name)                    объявить тип с именем name
//    fn::pinOf<N>() -> fn::Pin                дескриптор ядра из номера
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Имя класса было Pin<P>; переименовано в BoardPin<P>, потому что fn::Pin
//      теперь занят упакованным дескриптором в core/Types.h. Старый код
//      fn::Pin<13>::high() -> fn::BoardPin<13>::high().
//    - output() сначала задаёт уровень в PORTx, потом направление в DDRx:
//      вывод не проходит через неопределённое состояние. По умолчанию LOW.
//    - high()/low() — RMW. Если тот же порт трогает ISR, берите setAtomic()
//      либо работайте с константной маской (компилятор сам даст sbi/cbi,
//      который атомарен).
//    - toggle() пишет маску в PINx: на 328p это аппаратная инверсия бита
//      PORTx, одна запись, атомарно.
//    - Проверки физических ограничений нет: PB6/PB7 (кварц), PD0/PD1 (UART),
//      PC6 (RESET) формально валидны. Ответственность на прикладном коде.
//    - static_assert на номер: невалидный вывод падает на компиляции.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Каждая операция — 1-2 машинные команды. Неиспользуемые
//    методы не инстанцируются.
//
//  ПРИМЕР
//    using Led = fn::BoardPin<13>;
//    Led::output();          // cbi PORTB,5 ; sbi DDRB,5
//    Led::toggle();          // sbi PINB,5
//    bool on = Led::read();  // sbic PINB,5
//
//    FN_BOARD_PIN(2, Button);
//    Button::inputPullup();
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h, core/Atomic.h,
//               pin/Board.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"
#include "../core/Atomic.h"
#include "Board.h"

#include <stdint.h>

namespace fn {

// Дескриптор ядра из номера вывода платы (constexpr).
template <uint8_t N>
inline constexpr Pin pinOf() {
    return boardPin(N);
}

template <uint8_t N>
class BoardPin {
    static_assert(pinValid(N), "fn::BoardPin<N>: номер вывода вне 0..19 (Nano/Uno)");

    using Impl = FastPin<boardPin(N).id()>;

public:
    // --- направление ---
    FN_INLINE static void output(bool level = false) { Impl::modeOutput(level); }
    FN_INLINE static void input()                    { Impl::modeInput(); }
    FN_INLINE static void inputPullup()              { Impl::modeInputPullup(); }

    // --- запись ---
    FN_INLINE static void high()        { Impl::set(); }
    FN_INLINE static void low()         { Impl::clear(); }
    FN_INLINE static void write(bool v) { Impl::write(v); }
    FN_INLINE static void toggle()      { Impl::toggle(); }

    // --- атомарные версии для порта, разделяемого с ISR ---
    FN_INLINE static void setAtomic() {
        AtomicBlock lock;
        Impl::set();
    }
    FN_INLINE static void clearAtomic() {
        AtomicBlock lock;
        Impl::clear();
    }

    // --- чтение ---
    FN_INLINE static bool read() { return Impl::read(); }

    // --- мета ---
    static constexpr uint8_t num()      { return N; }
    static constexpr Pin     pin()      { return boardPin(N); }
    static constexpr Port    port()     { return boardPin(N).port(); }
    static constexpr uint8_t bitIndex() { return boardPin(N).bitIndex(); }
    static constexpr uint8_t mask()     { return boardPin(N).mask(); }
};

}  // namespace fn

#define FN_BOARD_PIN(n, name) using name = ::fn::BoardPin<(n)>

#pragma once
// ============================================================================
//  PinRef.h — runtime-вывод платы с кэшированным дескриптором
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Вывод, номер которого известен только в рантайме (из конфигурации,
//    таблицы сигналов, параметра команды). Хранит один байт — упакованный
//    fn::Pin — и вызывает операции ядра. В 10-20 раз быстрее digitalWrite,
//    при этом не дублирует логику работы с регистрами.
//
//  API
//    PinRef() / PinRef(n, mode) / init(n, mode) -> Result
//    setMode(Mode) / setModeRaw(uint8_t)        Mode: Input, InputPullup, Output
//    high() / low() / write(bool) / toggle()
//    highAtomic() / lowAtomic()
//    read() -> bool
//    valid() / pin() / mask() / num()
//    explicit operator bool()
//
//    Статические операции по номеру (без объекта, ноль SRAM):
//      pinMode(n, Mode) / pinHigh(n) / pinLow(n) / pinWrite(n, v)
//      pinToggle(n) / pinRead(n)
//      pinModeRaw(n, uint8_t)                   совместимость с INPUT/OUTPUT
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Размер объекта 1 байт вместо прежних 7 (три указателя + маска). Адрес
//      регистра вычисляется арифметикой, кэшировать указатели незачем.
//    - setMode(Output) задаёт уровень LOW перед переключением направления.
//      Прежняя версия оставляла бит PORTx как был, и после INPUT_PULLUP вывод
//      сразу выдавал HIGH — для реле это щелчок при инициализации.
//      Нужен HIGH на старте: setMode(Mode::Output, true).
//    - Режимы задаются своим enum, а не макросами Arduino INPUT/OUTPUT:
//      препроцессор не знает про namespace, а INPUT_PULLUP == 2 не совпадает
//      с порядком нашего enum. Для старого кода есть setModeRaw/pinModeRaw.
//    - high()/low() — RMW, не атомарны относительно ISR по тому же порту.
//      Используйте highAtomic()/lowAtomic() либо BoardPin с константой.
//    - toggle() пишет в PINx (аппаратная инверсия), атомарен сам по себе.
//    - Невалидный номер: init возвращает Result::BadArg, объект остаётся
//      невалидным, все операции безопасно ничего не делают.
//
//  СТОИМОСТЬ (AVR)
//    sizeof(PinRef) == 1. Операции — вызов inline-функций ядра, 4-8 команд.
//    Статические методы объектов не создают вовсе.
//
//  ПРИМЕР
//    fn::PinRef relay(7, fn::PinRef::Mode::Output);   // стартует с LOW
//    relay.high();
//
//    const uint8_t rows[4] = {4, 5, 6, 7};            // сканер клавиатуры
//    for (uint8_t i = 0; i < 4; ++i) fn::PinRef::pinLow(rows[i]);
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

class PinRef {
public:
    enum class Mode : uint8_t { Input = 0, InputPullup = 1, Output = 2 };

    PinRef() = default;
    explicit PinRef(uint8_t n, Mode m = Mode::Input) { init(n, m); }

    Result init(uint8_t n, Mode m = Mode::Input) {
        _pin = boardPin(n);
        if (!_pin.valid()) return Result::BadArg;
        return setMode(m);
    }

    Result setMode(Mode m, bool level = false) {
        if (!_pin.valid()) return Result::NotReady;
        switch (m) {
            case Mode::Output:      return regs::modeOutput(_pin, level);
            case Mode::InputPullup: return regs::modeInputPullup(_pin);
            default:                return regs::modeInput(_pin);
        }
    }

    // Совместимость с INPUT / OUTPUT / INPUT_PULLUP из Arduino.h.
    Result setModeRaw(uint8_t m) {
        if (m == 1) return setMode(Mode::Output);          // OUTPUT == 1
        if (m == 2) return setMode(Mode::InputPullup);     // INPUT_PULLUP == 2
        return setMode(Mode::Input);                       // INPUT == 0
    }

    FN_INLINE void high()   { regs::set(_pin); }
    FN_INLINE void low()    { regs::clear(_pin); }
    FN_INLINE void write(bool v) { regs::write(_pin, v); }
    FN_INLINE void toggle() { regs::toggle(_pin); }
    FN_INLINE bool read() const { return regs::read(_pin); }

    FN_INLINE void highAtomic() {
        AtomicBlock lock;
        regs::set(_pin);
    }
    FN_INLINE void lowAtomic() {
        AtomicBlock lock;
        regs::clear(_pin);
    }

    bool     valid() const { return _pin.valid(); }
    Pin      pin()   const { return _pin; }
    uint8_t  mask()  const { return _pin.mask(); }
    uint8_t  num()   const { return boardNumber(_pin); }
    explicit operator bool() const { return valid(); }

    // ---------------- статические операции по номеру вывода ----------------
    static Result pinMode(uint8_t n, Mode m, bool level = false) {
        const Pin p = boardPin(n);
        if (!p.valid()) return Result::BadArg;
        switch (m) {
            case Mode::Output:      return regs::modeOutput(p, level);
            case Mode::InputPullup: return regs::modeInputPullup(p);
            default:                return regs::modeInput(p);
        }
    }

    static Result pinModeRaw(uint8_t n, uint8_t m) {
        if (m == 1) return pinMode(n, Mode::Output);
        if (m == 2) return pinMode(n, Mode::InputPullup);
        return pinMode(n, Mode::Input);
    }

    static void pinHigh(uint8_t n)   { regs::set(boardPin(n)); }
    static void pinLow(uint8_t n)    { regs::clear(boardPin(n)); }
    static void pinWrite(uint8_t n, bool v) { regs::write(boardPin(n), v); }
    static void pinToggle(uint8_t n) { regs::toggle(boardPin(n)); }
    static bool pinRead(uint8_t n)   { return regs::read(boardPin(n)); }

private:
    Pin _pin{};
};

static_assert(sizeof(PinRef) == 1, "fn::PinRef должен занимать один байт");

}  // namespace fn

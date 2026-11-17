#pragma once
// ============================================================================
//  Port.h — доступ к порту целиком (параллельная шина, group-write)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Тонкие адаптеры к core/Registers.h для случаев, когда нужен весь порт
//    одной записью: параллельная шина HD44780, матрица светодиодов, быстрый
//    вывод байта. Своей работы с регистрами не содержит.
//
//  API
//    struct PortB / PortC / PortD
//      out() / dir() / in()  -> volatile uint8_t&   прямой доступ к SFR
//      write(v) / setMask(m) / clearMask(m) / toggle(m)
//      read() / readLatch() / readDir() -> uint8_t
//      modeOutput(m, level=false) / modeInput(m) / modeInputPullup(m)
//      id() -> fn::Port                            constexpr
//
//    Обобщённо: fn::portOps<Port::B>::write(0xFF) и т.п.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - write() перезаписывает ВЕСЬ порт, включая PD0/PD1 (UART) и PB6/PB7
//      (кварц). Для шины выделяйте маску и пользуйтесь setMask/clearMask
//      либо read-modify-write через out().
//    - toggle(m) пишет маску в PINx — аппаратная инверсия, атомарно.
//    - setMask/clearMask — RMW; при работе с ISR берите атомарные версии
//      из core/Registers.h (regs::setMaskAtomic и т.п.).
//    - Порт C: PC7 отсутствует, PC6 = RESET. Маска 0x7F — максимум доступного.
//    - Прежние имена port::B::port()/ddr()/pin() сохранены как алиасы, чтобы
//      существующий код продолжал собираться.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Прямой доступ к SFR — одна команда in/out; обобщённые
//    операции с переменным портом — вычисление адреса (2-4 команды).
//
//  ПРИМЕР
//    fn::PortD::modeOutput(0xFC);            // PD2..PD7 на вывод, UART не трогаем
//    fn::PortD::out() = (fn::PortD::in() & 0x03) | (data << 2);
//
//    fn::PortB::toggle(0x20);                // мигнуть PB5
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"

#include <stdint.h>
#include <avr/io.h>

namespace fn {

// Обобщённый набор операций над портом, заданным параметром шаблона.
template <Port P>
struct portOps {
    static constexpr Port id() { return P; }

    FN_INLINE static volatile uint8_t& out() { return *regs::sfr(P, Reg::Port); }
    FN_INLINE static volatile uint8_t& dir() { return *regs::sfr(P, Reg::Ddr); }
    FN_INLINE static volatile uint8_t& in()  { return *regs::sfr(P, Reg::Pin); }

    FN_INLINE static void write(uint8_t v)    { regs::write(P, v); }
    FN_INLINE static void setMask(uint8_t m)  { regs::setMask(P, m); }
    FN_INLINE static void clearMask(uint8_t m){ regs::clearMask(P, m); }
    FN_INLINE static void toggle(uint8_t m)   { regs::toggle(P, m); }

    FN_INLINE static uint8_t read()      { return regs::readInput(P); }
    FN_INLINE static uint8_t readLatch() { return regs::readLatch(P); }
    FN_INLINE static uint8_t readDir()   { return regs::readDir(P); }

    FN_INLINE static void modeOutput(uint8_t m, bool level = false) {
        regs::modeOutput(P, m, level);
    }
    FN_INLINE static void modeInput(uint8_t m)        { regs::modeInput(P, m); }
    FN_INLINE static void modeInputPullup(uint8_t m)  { regs::modeInputPullup(P, m); }
};

using PortB = portOps<Port::B>;
using PortC = portOps<Port::C>;
using PortD = portOps<Port::D>;

// Совместимость со старым namespace fn::port.
namespace port {
struct B {
    static volatile uint8_t& port() { return PortB::out(); }
    static volatile uint8_t& ddr()  { return PortB::dir(); }
    static volatile uint8_t& pin()  { return PortB::in(); }
};
struct C {
    static volatile uint8_t& port() { return PortC::out(); }
    static volatile uint8_t& ddr()  { return PortC::dir(); }
    static volatile uint8_t& pin()  { return PortC::in(); }
};
struct D {
    static volatile uint8_t& port() { return PortD::out(); }
    static volatile uint8_t& ddr()  { return PortD::dir(); }
    static volatile uint8_t& pin()  { return PortD::in(); }
};
}  // namespace port

}  // namespace fn

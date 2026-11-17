#pragma once
// ============================================================================
//  Power.h — режимы сна, делитель тактовой частоты, отключение периферии
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Снижение потребления: сон с корректной последовательностью входа,
//    отключение неиспользуемой периферии через PRR, делитель частоты,
//    отключение BOD на время сна. Арбитраж Timer0 согласован с Resources.h.
//
//  API (namespace fn, класс Power — только статические методы)
//    enum class Mode : uint8_t { Idle, Adc, PowerDown, PowerSave, Standby, ExtStandby }
//    enum class Clock : uint8_t { Div1, Div2, Div4, Div8, Div16, Div32, Div64, Div128, Div256 }
//    enum Peripheral : uint16_t
//      PeripheralNone, PeripheralAdc, PeripheralAc, PeripheralTimer0,
//      PeripheralTimer1, PeripheralTimer2, PeripheralSpi, PeripheralUsart0,
//      PeripheralTwi, PeripheralAll
//
//    setMode(Mode) / mode() -> Mode
//    setClockDivider(Clock) -> Result       Busy, если Timer0 принадлежит ядру
//    clockDivider() -> Clock
//    adjustOsccal(int8_t delta) -> Result   только при работе от внутреннего RC
//    osccal() -> uint8_t
//    disable(uint16_t mask) -> Result
//    enable(uint16_t mask) -> Result
//    setBodInSleep(bool) / bodInSleep() -> bool
//    sleepOnce()                            один сон до ближайшего прерывания
//    lastError() -> Result
//
//  РЕЖИМЫ СНА И ИСТОЧНИКИ ПРОБУЖДЕНИЯ (ATmega328P)
//    Idle        всё работает, кроме CPU и flash. Просыпается от любого
//                прерывания. millis() продолжает идти. ~ hundreds мкА.
//    Adc         ADC Noise Reduction: гасит CPU и I/O, ADC работает.
//                Пробуждение: ADC, INT0/1, PCINT, TWI, WDT, Timer2 (async).
//    PowerDown   минимум потребления (~0.1 мкА + BOD). Тактирование остановлено.
//                Пробуждение: INT0/1 (level), PCINT, TWI address match, WDT.
//                Таймеры стоят, millis() не идёт.
//    PowerSave   как PowerDown, но Timer2 может работать асинхронно.
//                ВНИМАНИЕ: на Nano TOSC1/TOSC2 заняты основным кварцем,
//                асинхронный Timer2 недоступен (см. Timer2.h). Фактически
//                режим вырождается в PowerDown.
//    Standby     как PowerDown, но основной генератор продолжает работать:
//                пробуждение за 6 тактов вместо ~1000. Потребление выше.
//    ExtStandby  как Standby, но генератор тактируется внешним источником.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - setMode() пишет корректные биты SM2:SM0 регистра SMCR. Прежняя версия
//      в sleepOnce() повторно звала set_sleep_mode(uint8_t(g_mode)), подставляя
//      наш enum (0..5) вместо значений avr-libc (0/2/4/6/12/14): запрос
//      PowerDown давал ADC-mode, Standby давал PowerDown. Ток потребления
//      был на порядки выше ожидаемого. Теперь хранится готовая маска SMCR.
//    - sleepOnce() сохраняет и восстанавливает SREG. Внутри окна сна
//      прерывания разрешаются принудительно (иначе МК не проснётся), но
//      состояние вызывающего не меняется.
//    - Перед sleep_bod_disable() запрещаются прерывания: последовательность
//      BODS/BODSE действует 4 такта, ISR внутри окна отменяет отключение.
//    - sleepOnce() НЕ проверяет, разрешён ли хоть один источник пробуждения.
//      Если все запрещены, МК проспит до WDT или внешнего сброса. Настройте
//      источник (Irq, Watchdog, TWI) ДО вызова.
//    - setClockDivider меняет реальную частоту: F_CPU перестаёт ей
//      соответствовать. Ломаются millis/micros, baud UART, периоды таймеров
//      и все задержки. Возвращает Busy, если Timer0 принадлежит ядру Arduino
//      (FN_TIMER0_DEVICE == FN_OWNER_ARDUINO) — иначе время в скетче уйдёт
//      молча. Для смены частоты сначала освободите Timer0 в Resources.h.
//    - adjustOsccal на Nano/Uno с внешним кварцем НЕ ДЕЙСТВУЕТ: OSCCAL
//      управляет только внутренним RC-генератором. Кроме того диапазон
//      OSCCAL нелинейен: переход через 0x80 переключает поддиапазон и даёт
//      скачок частоты. Пригоден только для калибровки RC-варианта платы.
//    - disable(PeripheralTimer0) отвергается, пока Timer0 принадлежит ядру:
//      иначе millis()/micros()/delay() умирают без предупреждения.
//    - Отключать периферию нужно ПОСЛЕ её остановки: disable(Twi) при
//      активном I2c оставит шину в неопределённом состоянии. Порядок:
//      I2c::end(), затем Power::disable(PeripheralTwi).
//    - PeripheralAll не включает Timer0, если он принадлежит ядру: маска
//      фильтруется, а не отвергается целиком (удобно перед сном).
//    - Отключение ADC через ADEN не отключает цифровые входные буферы:
//      для минимального тока настройте DIDR0 в модуле Adc.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 4 байта (smcr, mode, flags, lastError). Flash: ~300 байт.
//    Функции avr/power.h — по 2-4 инструкции на бит PRR.
//
//  ПРИМЕР
//    // Пробуждение по кнопке на D2 (INT0), сон между нажатиями
//    fn::irq::ExternalInt::attach(fn::irq::ExternalInt::Line::Int0,
//                                 fn::irq::ExternalInt::Edge::LowLevel, onWake);
//    fn::Power::disable(fn::Power::PeripheralAdc | fn::Power::PeripheralAc |
//                       fn::Power::PeripheralSpi | fn::Power::PeripheralTwi);
//    fn::Power::setMode(fn::Power::Mode::PowerDown);
//    fn::Power::setBodInSleep(false);
//    for (;;) { fn::Power::sleepOnce(); handleWake(); }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               avr/sleep.h, avr/power.h; реализация — Power.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"

#include <stdint.h>

#if F_USE_POWER

namespace fn {

class Power {
public:
    enum class Mode : uint8_t { Idle, Adc, PowerDown, PowerSave, Standby, ExtStandby };

    enum class Clock : uint8_t {
        Div1 = 0, Div2, Div4, Div8, Div16, Div32, Div64, Div128, Div256
    };

    enum Peripheral : uint16_t {
        PeripheralNone   = 0x0000,
        PeripheralAdc    = 0x0001,
        PeripheralAc     = 0x0002,
        PeripheralTimer0 = 0x0004,
        PeripheralTimer1 = 0x0008,
        PeripheralTimer2 = 0x0010,
        PeripheralSpi    = 0x0020,
        PeripheralUsart0 = 0x0040,
        PeripheralTwi    = 0x0080,
        PeripheralAll    = 0x00FF
    };

    static void setMode(Mode m);
    static Mode mode();

    static Result setClockDivider(Clock c);
    static Clock clockDivider();

    static Result adjustOsccal(int8_t delta);
    static uint8_t osccal();

    static Result disable(uint16_t mask);
    static Result enable(uint16_t mask);

    static void setBodInSleep(bool enabled);
    static bool bodInSleep();

    static void sleepOnce();
    static Result lastError();

private:
    Power() = delete;
};

}  // namespace fn

#endif  // F_USE_POWER

// ============================================================================
//  Power.cpp — сон, PRR, делитель частоты, OSCCAL
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация управления питанием. Ключевая тонкость — соответствие
//    нашего enum Mode битам SM2:SM0 регистра SMCR и корректная
//    последовательность входа в сон.
//
//  ПОЧЕМУ ХРАНИМ ГОТОВУЮ МАСКУ
//    Значения avr-libc: IDLE=0, ADC=2, PWR_DOWN=4, PWR_SAVE=6, STANDBY=12,
//    EXT_STANDBY=14 (это биты SM0=1, SM1=2, SM2=3 в SMCR). Наш enum
//    последовательный: 0..5. Прежняя версия в sleepOnce() передавала
//    uint8_t(g_mode) прямо в set_sleep_mode(), из-за чего PowerDown(2)
//    превращался в ADC(2), а Standby(4) — в PowerDown(4). Теперь setMode()
//    один раз вычисляет маску, sleepOnce() её только применяет.
//
//  ПОСЛЕДОВАТЕЛЬНОСТЬ СНА
//    1. сохранить SREG;
//    2. cli() — окно BODS/BODSE и проверка источника не должны рваться ISR;
//    3. sleep_enable() (бит SE);
//    4. при отключённом BOD — sleep_bod_disable() (4-тактовая последовательность);
//    5. sei(); sleep_cpu() — на AVR прерывания разрешаются после следующей
//       инструкции, поэтому sleep гарантированно выполняется до входа в ISR;
//    6. sleep_disable(); восстановить SREG.
//
//  ОГРАНИЧЕНИЯ
//    - Источник пробуждения не проверяется: все прерывания запрещены ->
//      сон до WDT или сброса.
//    - Делитель частоты и Timer0: если таймер принадлежит ядру Arduino,
//      смена частоты отвергается (иначе millis врёт молча).
//    - OSCCAL действует только на внутренний RC; на плате с кварцем пустой.
//    - Файл должен лежать в src/peripheral/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Power.h, core/Resources.h, avr/sleep.h, avr/power.h, avr/io.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_POWER

#include "Power.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <avr/power.h>

namespace fn {
namespace power_detail {

uint8_t smcrBits = SLEEP_MODE_IDLE;      ///< готовая маска SM2:SM0
Power::Mode current = Power::Mode::Idle;
Power::Clock divider = Power::Clock::Div1;
bool bodOff = false;
Result last = Result::Ok;

// Timer0 принадлежит ядру Arduino? constexpr -> мёртвый код вырезается.
constexpr bool timer0Locked = (FN_TIMER0_DEVICE == FN_OWNER_ARDUINO);

inline Result fail(Result r) { last = r; return r; }
inline Result done() { last = Result::Ok; return Result::Ok; }

}  // namespace power_detail

// ============================ режим сна ====================================
void Power::setMode(Mode m) {
    using namespace power_detail;
    current = m;
    switch (m) {
        case Mode::Idle:       smcrBits = SLEEP_MODE_IDLE;        break;
        case Mode::Adc:        smcrBits = SLEEP_MODE_ADC;         break;
        case Mode::PowerDown:  smcrBits = SLEEP_MODE_PWR_DOWN;    break;
        case Mode::PowerSave:  smcrBits = SLEEP_MODE_PWR_SAVE;    break;
        case Mode::Standby:    smcrBits = SLEEP_MODE_STANDBY;     break;
        case Mode::ExtStandby: smcrBits = SLEEP_MODE_EXT_STANDBY; break;
    }
    set_sleep_mode(smcrBits);
}

Power::Mode Power::mode() { return power_detail::current; }

// ============================ делитель частоты =============================
Result Power::setClockDivider(Clock c) {
    // Смена частоты ломает millis()/UART/таймеры. Пока Timer0 у ядра — запрет.
    if (power_detail::timer0Locked) return power_detail::fail(Result::Busy);

    AtomicBlock lock;
    CLKPR = uint8_t(1u << CLKPCE);          // разрешить изменение
    CLKPR = uint8_t(c) & 0x0Fu;             // в пределах 4 тактов
    power_detail::divider = c;
    return power_detail::done();
}

Power::Clock Power::clockDivider() {
    return static_cast<Clock>(CLKPR & 0x0Fu);
}

// ============================ OSCCAL =======================================
Result Power::adjustOsccal(int8_t delta) {
    // Базовое заводское значение читаем один раз: повторный захват после
    // коррекции увёл бы точку отсчёта.
    static uint8_t base = OSCCAL;
    const int16_t v = int16_t(base) + int16_t(delta);
    if (v < 0 || v > 255) return power_detail::fail(Result::OutOfRange);

    AtomicBlock lock;
    OSCCAL = uint8_t(v);
    return power_detail::done();
}

uint8_t Power::osccal() { return OSCCAL; }

// ============================ периферия ====================================
Result Power::disable(uint16_t mask) {
    // Явный запрос Timer0 при владении ядром — отвергаем целиком.
    if (power_detail::timer0Locked && (mask & PeripheralTimer0)) {
        // В составе PeripheralAll фильтруем молча: это типовой сценарий сна.
        if (mask == uint16_t(PeripheralAll)) {
            mask = uint16_t(mask & uint16_t(~PeripheralTimer0));
        } else {
            return power_detail::fail(Result::Busy);
        }
    }

    AtomicBlock lock;
    if (mask & PeripheralAdc)    ADCSRA = uint8_t(ADCSRA & uint8_t(~(1u << ADEN)));
    if (mask & PeripheralAc)     ACSR   = uint8_t(ACSR | (1u << ACD));
    if (mask & PeripheralTimer0) power_timer0_disable();
    if (mask & PeripheralTimer1) power_timer1_disable();
    if (mask & PeripheralTimer2) power_timer2_disable();
    if (mask & PeripheralSpi)    power_spi_disable();
    if (mask & PeripheralUsart0) power_usart0_disable();
    if (mask & PeripheralTwi)    power_twi_disable();
    return power_detail::done();
}

Result Power::enable(uint16_t mask) {
    AtomicBlock lock;
    if (mask & PeripheralAdc)    ADCSRA = uint8_t(ADCSRA | (1u << ADEN));
    if (mask & PeripheralAc)     ACSR   = uint8_t(ACSR & uint8_t(~(1u << ACD)));
    if (mask & PeripheralTimer0) power_timer0_enable();
    if (mask & PeripheralTimer1) power_timer1_enable();
    if (mask & PeripheralTimer2) power_timer2_enable();
    if (mask & PeripheralSpi)    power_spi_enable();
    if (mask & PeripheralUsart0) power_usart0_enable();
    if (mask & PeripheralTwi)    power_twi_enable();
    return power_detail::done();
}

// ============================ BOD и сон ====================================
void Power::setBodInSleep(bool enabled) { power_detail::bodOff = !enabled; }
bool Power::bodInSleep() { return !power_detail::bodOff; }

void Power::sleepOnce() {
    using namespace power_detail;

    const uint8_t sreg = SREG;
    cli();                                   // окно BODS/BODSE не рвём ISR

    set_sleep_mode(smcrBits);                // корректная маска, не enum
    sleep_enable();

#if defined(sleep_bod_disable)
    if (bodOff) sleep_bod_disable();         // действует 4 такта
#endif

    sei();                                   // без этого МК не проснётся
    sleep_cpu();                             // выполняется до входа в ISR
    sleep_disable();

    SREG = sreg;                             // состояние вызывающего сохранено
}

Result Power::lastError() { return power_detail::last; }

}  // namespace fn

#endif  // F_USE_POWER

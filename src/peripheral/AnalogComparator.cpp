// ============================================================================
//  AnalogComparator.cpp — компаратор: входы, прерывания, связь с ADC и Timer1
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация аналогового компаратора. Единственный ISR(ANALOG_COMP_vect)
//    в проекте, вектор заявлен.
//
//  СВЯЗЬ С ADC
//    Минусовой вход берётся либо с AIN1 (D7), либо с мультиплексора ADC.
//    Второй вариант требует ADEN=0: по даташиту ACME действует только при
//    выключенном ADC. setNegativeMux() запоминает состояние ADEN, гасит его,
//    ставит ACME и выбирает канал в ADMUX. restoreAdc() возвращает как было.
//    Прежняя версия ставила ACME, не трогая ADEN — режим не включался вовсе.
//
//  СВЯЗЬ С TIMER1
//    Бит ACIC подаёт выход компаратора на вход захвата ICP1: момент
//    пересечения порога фиксируется аппаратно с точностью до такта.
//    Несовместимо с внешним захватом на D8 (pin/Pulse).
//
//  ОГРАНИЧЕНИЯ
//    - Коллбэк volatile, регистрация под AtomicBlock.
//    - Задержка компаратора ~1 мкс: read() сразу после смены входов недостоверен.
//    - Файл должен лежать в src/peripheral/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: AnalogComparator.h, core/Atomic.h, avr/io.h, avr/interrupt.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_AC

#include "AnalogComparator.h"

#include <avr/io.h>
#include <avr/interrupt.h>

FN_CLAIM_VECTOR(AnalogComp, FN_OWNER_ADC);

namespace fn {
namespace acomp_detail {

AnalogComparator::Callback volatile cb = nullptr;
Result last = Result::Ok;
uint8_t savedAden = 0;          ///< ADEN до входа в режим Mux
bool muxMode = false;

inline Result fail(Result r) { last = r; return r; }
inline Result done() { last = Result::Ok; return Result::Ok; }

}  // namespace acomp_detail

// ============================ инициализация ================================
Result AnalogComparator::begin(Positive p, Negative n) {
    AtomicBlock lock;
    acomp_detail::cb = nullptr;
    acomp_detail::muxMode = false;
    ACSR = uint8_t(1u << ACIE);           // прерывание выключим ниже
    ACSR = 0;                             // ACD=0 -> компаратор включён
    setPositive(p);
    setNegative(n);
    return acomp_detail::done();
}

void AnalogComparator::end() {
    AtomicBlock lock;
    ACSR = uint8_t(1u << ACD);            // выключить, потребление к нулю
    ADCSRB = uint8_t(ADCSRB & uint8_t(~(1u << ACME)));
    acomp_detail::cb = nullptr;
    acomp_detail::muxMode = false;
}

void AnalogComparator::enable() {
    AtomicBlock lock;
    ACSR = uint8_t(ACSR & uint8_t(~(1u << ACD)));
}

void AnalogComparator::disable() {
    AtomicBlock lock;
    ACSR = uint8_t(ACSR | (1u << ACD));
}

bool AnalogComparator::enabled() { return (ACSR & (1u << ACD)) == 0; }

// ============================ входы ========================================
void AnalogComparator::setPositive(Positive p) {
    AtomicBlock lock;
    if (p == Positive::Vbg) ACSR = uint8_t(ACSR | (1u << ACBG));
    else                    ACSR = uint8_t(ACSR & uint8_t(~(1u << ACBG)));
}

void AnalogComparator::setNegative(Negative n) {
    AtomicBlock lock;
    if (n == Negative::Mux) {
        // Без ADEN=0 режим не работает: включаем только если ADC уже выключен.
        if (ADCSRA & (1u << ADEN)) {
            acomp_detail::fail(Result::Busy);
            return;
        }
        ADCSRB = uint8_t(ADCSRB | (1u << ACME));
        acomp_detail::muxMode = true;
    } else {
        ADCSRB = uint8_t(ADCSRB & uint8_t(~(1u << ACME)));
        acomp_detail::muxMode = false;
    }
    acomp_detail::done();
}

Result AnalogComparator::setNegativeMux(uint8_t channel) {
    using namespace acomp_detail;
    if (channel > 7) return fail(Result::BadArg);
    if (ADCSRA & (1u << ADSC)) return fail(Result::Busy);   // идёт конверсия

    AtomicBlock lock;
    if (!muxMode) {
        savedAden = uint8_t(ADCSRA & (1u << ADEN));
        ADCSRA = uint8_t(ADCSRA & uint8_t(~(1u << ADEN)));  // обязательное условие
        muxMode = true;
    }
    ADMUX = uint8_t((ADMUX & 0xF0u) | (channel & 0x07u));
    ADCSRB = uint8_t(ADCSRB | (1u << ACME));
    return done();
}

void AnalogComparator::restoreAdc() {
    using namespace acomp_detail;
    AtomicBlock lock;
    ADCSRB = uint8_t(ADCSRB & uint8_t(~(1u << ACME)));
    if (muxMode && savedAden) {
        ADCSRA = uint8_t(ADCSRA | savedAden);
    }
    muxMode = false;
}

bool AnalogComparator::read() { return (ACSR & (1u << ACO)) != 0; }

// ============================ прерывания ===================================
Result AnalogComparator::attach(Callback f, Trigger t) {
    AtomicBlock lock;
    acomp_detail::cb = f;
    if (f) {
        ACSR = uint8_t((ACSR & 0xFCu) | uint8_t(t));   // ACIS1:0
        ACSR = uint8_t(ACSR | (1u << ACIE));
    } else {
        ACSR = uint8_t(ACSR & uint8_t(~(1u << ACIE)));
    }
    return acomp_detail::done();
}

void AnalogComparator::detach() { attach(nullptr, Trigger::Change); }

bool AnalogComparator::isAttached() { return acomp_detail::cb != nullptr; }

// ============================ связи и буферы ===============================
void AnalogComparator::enableInputCapture(bool on) {
    AtomicBlock lock;
    if (on) ACSR = uint8_t(ACSR | (1u << ACIC));
    else    ACSR = uint8_t(ACSR & uint8_t(~(1u << ACIC)));
}

void AnalogComparator::disableDigitalInput(bool ain0, bool ain1) {
    AtomicBlock lock;
    uint8_t m = 0;
    if (ain0) m = uint8_t(m | (1u << AIN0D));
    if (ain1) m = uint8_t(m | (1u << AIN1D));
    DIDR1 = uint8_t(DIDR1 | m);
}

Result AnalogComparator::lastError() { return acomp_detail::last; }

}  // namespace fn

// ============================================================================
//  ISR — единственный в проекте для вектора аналогового компаратора
// ============================================================================
ISR(ANALOG_COMP_vect) {
    const fn::AnalogComparator::Callback f = fn::acomp_detail::cb;
    if (f) f();
}

#endif  // F_USE_AC
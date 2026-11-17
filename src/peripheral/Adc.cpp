// ============================================================================
//  Adc.cpp — АЦП: конверсия с таймаутом, пауза после смены входа, Vcc, температура
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация ADC. Единственный ISR(ADC_vect) в проекте, вектор заявлен.
//
//  АТОМАРНОСТЬ ЧТЕНИЯ
//    Пара ADCL/ADCH аппаратно защищена: чтение ADCL блокирует обновление
//    до чтения ADCH. Если ISR между ними прочитает ADC, основное чтение
//    получит рассогласованные байты. Поэтому read10() под AtomicBlock.
//
//  ПАУЗА ПОСЛЕ СМЕНЫ ВХОДА
//    Схема выборки-хранения должна дозарядиться до нового напряжения.
//    Даташит рекомендует отбросить первую конверсию после смены MUX/REF.
//    readMux() делает холостую конверсию, затем рабочую. При переходе на
//    Internal/Vbg/TempSensor добавляется пауза 10 мкс на установление
//    внутреннего источника.
//
//  ИЗМЕРЕНИЕ ПИТАНИЯ
//    Vbg (1.1 В) измеряется относительно AVcc: ADC = 1024 * Vbg / Vcc.
//    Отсюда Vcc[мВ] = 1100 * 1023 / ADC. Разброс Vbg по даташиту 1.0..1.2 В,
//    поэтому без калибровки точность около ±10%.
//
//  ТЕМПЕРАТУРА
//    Типовые параметры: 314 мВ при 25 °C, наклон 1.22 мВ/°C. Смещение не
//    нормируется от экземпляра к экземпляру: без калибровки ±10 °C.
//    Расчёт целочисленный, без float.
//
//  ОГРАНИЧЕНИЯ
//    - Таймаут через fn::expiredUs(), не счётчик итераций.
//    - Коллбэк volatile, регистрация под AtomicBlock.
//    - Файл должен лежать в src/peripheral/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Adc.h, timer/Tick.h, avr/io.h, avr/interrupt.h, avr/sleep.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_ADC

#include "Adc.h"
#include "../timer/Tick.h"

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/sleep.h>
#include <util/delay.h>

FN_CLAIM_VECTOR(Adc, FN_OWNER_ADC);

namespace fn {
namespace adc_detail {

Adc::Callback volatile cb = nullptr;
Result last = Result::Ok;
volatile uint16_t cntTimeout = 0;

inline Result fail(Result r) {
    last = r;
    if (r == Result::Timeout) ++cntTimeout;
    return r;
}
inline Result done() { last = Result::Ok; return Result::Ok; }

// Ожидание завершения конверсии с таймаутом.
bool waitDone(uint32_t timeoutUs) {
    const uint32_t t0 = ::fn::micros();
    while (ADCSRA & (1u << ADSC)) {
        if (elapsed(t0, ::fn::micros()) >= timeoutUs) return false;
    }
    return true;
}

uint16_t prescaleOf(Adc::Prescaler p) {
    return uint16_t(1u) << uint8_t(p);      // P2=1 -> 2, P128=7 -> 128
}

// Холостая конверсия: заряд выборки после смены входа.
bool discardOne(uint32_t timeoutUs) {
    ADCSRA = uint8_t(ADCSRA | (1u << ADSC));
    return waitDone(timeoutUs);
}

}  // namespace adc_detail

// ============================ инициализация ================================
Result Adc::begin(Ref r, Prescaler p, Res res) {
    using namespace adc_detail;

    // Идёт конверсия — прерывать её нельзя, результат будет мусором.
    if (ADCSRA & (1u << ADSC)) return fail(Result::Busy);

    AtomicBlock lock;
    ADCSRA = 0;
    ADCSRB = 0;
    setReference(r);
    setPrescaler(p);
    setResolution(res);
    cb = nullptr;
    cntTimeout = 0;
    ADCSRA = uint8_t(ADCSRA | (1u << ADEN));
    done();

    // Вне спецификации 10 бит — предупреждаем кодом, настройку оставляем.
    if (!clockInRange()) return fail(Result::OutOfRange);
    return Result::Ok;
}

void Adc::end() {
    AtomicBlock lock;
    ADCSRA = 0;
    ADCSRB = 0;
    adc_detail::cb = nullptr;
}

bool Adc::enabled() { return (ADCSRA & (1u << ADEN)) != 0; }
bool Adc::ready()   { return (ADCSRA & (1u << ADSC)) == 0; }

uint32_t Adc::clock() {
    const uint8_t bits = uint8_t(ADCSRA & 0x07u);
    if (bits == 0) return F_CPU / 2UL;      // ADPS=0 трактуется как /2
    return F_CPU / (uint32_t(1u) << bits);
}

bool Adc::clockInRange() {
    const uint32_t f = clock();
    return (f >= 50000UL) && (f <= 200000UL);
}

// ============================ настройка ====================================
void Adc::setMux(Mux m) {
    AtomicBlock lock;
    ADMUX = uint8_t((ADMUX & 0xF0u) | (uint8_t(m) & 0x0Fu));
}

void Adc::setReference(Ref r) {
    AtomicBlock lock;
    ADMUX = uint8_t((ADMUX & 0x3Fu) | (uint8_t(r) << 6));
}

void Adc::setPrescaler(Prescaler p) {
    AtomicBlock lock;
    ADCSRA = uint8_t((ADCSRA & 0xF8u) | uint8_t(p));
}

void Adc::setResolution(Res r) {
    AtomicBlock lock;
    if (r == Res::Bits8) ADMUX = uint8_t(ADMUX | (1u << ADLAR));
    else                 ADMUX = uint8_t(ADMUX & uint8_t(~(1u << ADLAR)));
}

Adc::Mux Adc::mux() { return static_cast<Mux>(ADMUX & 0x0Fu); }

Adc::Ref Adc::reference() {
    const uint8_t v = uint8_t((ADMUX >> 6) & 0x03u);
    return (v == 3) ? Ref::Internal : static_cast<Ref>(v);
}

Adc::Res Adc::resolution() {
    return (ADMUX & (1u << ADLAR)) ? Res::Bits8 : Res::Bits10;
}

// ============================ конверсия ====================================
void Adc::start() { ADCSRA = uint8_t(ADCSRA | (1u << ADSC)); }

uint16_t Adc::read10() {
    AtomicBlock lock;
    return ADC;                 // avr-libc читает ADCL, затем ADCH
}

uint8_t Adc::read8() { return ADCH; }

Result Adc::readBlocking(uint16_t& out, uint32_t timeoutUs) {
    using namespace adc_detail;
    if (!enabled()) return fail(Result::NotReady);
    if (ADCSRA & (1u << ADSC)) {
        // Конверсия уже идёт (например, автозапуск): дожидаемся её.
        if (!waitDone(timeoutUs)) return fail(Result::Timeout);
    } else {
        start();
        if (!waitDone(timeoutUs)) return fail(Result::Timeout);
    }
    out = read10();
    return done();
}

int32_t Adc::readBlocking() {
    uint16_t v = 0;
    return ok(readBlocking(v, FN_ADC_TIMEOUT_US)) ? int32_t(v) : int32_t(-1);
}

Result Adc::readMux(Mux m, uint16_t& out, uint32_t timeoutUs) {
    using namespace adc_detail;
    if (!enabled()) return fail(Result::NotReady);

    setMux(m);
    // Внутренние источники устанавливаются медленнее внешнего входа.
    if (m == Mux::TempSensor || m == Mux::Vbg) _delay_us(10);

    if (!discardOne(timeoutUs)) return fail(Result::Timeout);   // холостая
    start();
    if (!waitDone(timeoutUs)) return fail(Result::Timeout);
    out = read10();
    return done();
}

int32_t Adc::readMux(Mux m) {
    uint16_t v = 0;
    return ok(readMux(m, v, FN_ADC_TIMEOUT_US)) ? int32_t(v) : int32_t(-1);
}

Result Adc::readNoiseReduced(uint16_t& out) {
    using namespace adc_detail;
    if (!enabled()) return fail(Result::NotReady);

    const uint8_t savedA = ADCSRA;
    const uint8_t savedMode = SMCR;

    // Пробуждение по завершению конверсии: ADIE обязателен.
    ADCSRA = uint8_t(ADCSRA | (1u << ADIE));

    set_sleep_mode(SLEEP_MODE_ADC);
    const uint8_t sreg = SREG;
    cli();
    sleep_enable();
    sei();
    sleep_cpu();                // вход в режим сам запускает конверсию
    sleep_disable();
    SREG = sreg;

    SMCR = savedMode;
    ADCSRA = savedA;

    if (!waitDone(FN_ADC_TIMEOUT_US)) return fail(Result::Timeout);
    out = read10();
    return done();
}

// ============================ автозапуск ===================================
void Adc::enableAutoTrigger(Trigger t) {
    AtomicBlock lock;
    ADCSRB = uint8_t((ADCSRB & 0xF8u) | uint8_t(t));
    ADCSRA = uint8_t(ADCSRA | (1u << ADATE));
}

void Adc::disableAutoTrigger() {
    AtomicBlock lock;
    ADCSRA = uint8_t(ADCSRA & uint8_t(~(1u << ADATE)));
}

// ============================ коллбэк ======================================
Result Adc::attach(Callback f) {
    AtomicBlock lock;
    adc_detail::cb = f;
    if (f) ADCSRA = uint8_t(ADCSRA | (1u << ADIE));
    else   ADCSRA = uint8_t(ADCSRA & uint8_t(~(1u << ADIE)));
    return adc_detail::done();
}

void Adc::detach() { attach(nullptr); }

bool Adc::isAttached() { return adc_detail::cb != nullptr; }

// ============================ цифровые буферы ==============================
void Adc::disableDigitalInput(uint8_t mask) {
    AtomicBlock lock;
    DIDR0 = uint8_t(DIDR0 | (mask & 0x3Fu));   // ADC0D..ADC5D
}

void Adc::enableDigitalInput(uint8_t mask) {
    AtomicBlock lock;
    DIDR0 = uint8_t(DIDR0 & uint8_t(~(mask & 0x3Fu)));
}

// ============================ производные измерения ========================
uint16_t Adc::readVccMillivolts() {
    const Res savedRes = resolution();
    const Ref savedRef = reference();
    const Mux savedMux = mux();

    setResolution(Res::Bits10);
    setReference(Ref::Avcc);
    _delay_us(10);

    uint16_t raw = 0;
    const Result r = readMux(Mux::Vbg, raw, FN_ADC_TIMEOUT_US);

    setReference(savedRef);
    setResolution(savedRes);
    setMux(savedMux);

    if (!ok(r) || raw == 0) return 0;
    // Vcc = Vbg * 1023 / ADC, Vbg принят 1100 мВ.
    return uint16_t((1100UL * 1023UL) / raw);
}

int16_t Adc::readTemperatureC() {
    const Res savedRes = resolution();
    const Ref savedRef = reference();
    const Mux savedMux = mux();

    setResolution(Res::Bits10);
    setReference(Ref::Internal);
    _delay_us(10);

    uint16_t raw = 0;
    const Result r = readMux(Mux::TempSensor, raw, FN_ADC_TIMEOUT_US);

    setReference(savedRef);
    setResolution(savedRes);
    setMux(savedMux);

    if (!ok(r)) return 0;

    // Пересчёт в милливольты через номинал Internal = 1100 мВ.
    const int32_t mv = (int32_t(raw) * 1100L) / 1023L;
    // T = 25 + (mv - offset) / slope; slope в мкВ/°C -> умножаем на 1000.
    const int32_t dc = ((mv - FN_ADC_TEMP_OFFSET_MV) * 1000L) / FN_ADC_TEMP_SLOPE_UV;
    return int16_t(25 + dc);
}

// ============================ диагностика ==================================
Result Adc::lastError() { return adc_detail::last; }

uint16_t Adc::timeouts() {
    AtomicBlock lock;
    return adc_detail::cntTimeout;
}

void Adc::clearStats() {
    AtomicBlock lock;
    adc_detail::cntTimeout = 0;
}

}  // namespace fn

// ============================================================================
//  ISR — единственный в проекте для вектора ADC
// ============================================================================
ISR(ADC_vect) {
    const fn::Adc::Callback f = fn::adc_detail::cb;
    if (f) f();
}

#endif  // F_USE_ADC

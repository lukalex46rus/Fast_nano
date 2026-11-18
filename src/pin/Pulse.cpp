// ============================================================================
//  Pulse.cpp — Input Capture поверх Timer1: захват, эпохи, пересчёт
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация измерения периода аппаратным захватом. Единственные в проекте
//    ISR(TIMER1_CAPT_vect) и ISR(TIMER1_OVF_vect). Регистров Timer1 не касается
//    напрямую: вся настройка и чтение через API timer/Timer1.
//
//  КОРРЕКЦИЯ ПЕРЕПОЛНЕНИЯ (главная тонкость, G2)
//    Захват происходит аппаратно в момент фронта, ISR входит позже. Если
//    между фронтом и входом в ISR таймер переполнился, флаг TOV1 выставлен,
//    а счётчик эпох overflows ещё не увеличен (ISR переполнения имеет меньший
//    приоритет: вектор 13 против 10). Нужно решить, относится ли это
//    переполнение к захваченному значению.
//
//    Правило детерминированное, без порогов:
//      TOV1 сброшен            -> переполнения нет или уже учтено; ничего.
//      TOV1 выставлен, cnt>=icr -> с захвата счётчик не заворачивался,
//                                  значит переполнение было ДО захвата
//                                  и ещё не учтено: локально ov + 1.
//      TOV1 выставлен, cnt<icr  -> счётчик завернулся ПОСЛЕ захвата,
//                                  переполнение относится к следующему
//                                  периоду: ничего не делаем.
//
//    Порядок чтений строго ICR1 -> TOV1 -> TCNT1. При обратном порядке
//    переполнение может произойти между чтением счётчика и чтением флага
//    и даст ложное +1. Глобальный overflows не меняется: ISR переполнения
//    сам доведёт счётчик, иначе получилось бы двойное учитывание.
//
//    Прежняя версия использовала порог icr < 0x8000. Он работоспособен
//    (задержка ISR никогда не достигает 32768 тактов), но выглядит как
//    эвристика и не объясняет, почему граница именно такая. Сравнение
//    cnt >= icr эквивалентно по результату и не содержит магических чисел.
//
//  ПЕРЕСЧЁТ БЕЗ ПЕРЕПОЛНЕНИЯ (G3)
//    us = ticks * presc / TicksPerUs. Умножение до деления переполняло uint32
//    при длинных периодах. Здесь ticks делится на TicksPerUs заранее, остаток
//    домножается отдельно: q*presc + (r*presc)/TicksPerUs. TicksPerUs —
//    константа компиляции (16 или 8), деление и остаток сводятся к сдвигу и маске.
//    Переполнение результата проверяется сдвигом (presc — степень двойки)
//    и даёт насыщение 0xFFFFFFFF, а не заворот.
//
//  ОГРАНИЧЕНИЯ
//    - Коллбэк volatile, регистрация под AtomicBlock.
//    - Таймер захватывается через Timer1::acquire(Client::Pulse): если его
//      держит Pwm/Servo/Stepper, begin вернёт Busy.
//    - Файл должен лежать в src/pin/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Pulse.h, timer/Timer1.h, core/Atomic.h, avr/interrupt.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_PULSE

#include "Pulse.h"

#include <avr/interrupt.h>

// Timer1 — разделяемый ресурс: владение устройством только в рантайме
// (Timer1::acquire). Векторы CAPT/OVF определяет этот файл — заявляем их.
FN_CLAIM_VECTOR(Timer1Capt, FN_OWNER_PULSE);
FN_CLAIM_VECTOR(Timer1Ovf, FN_OWNER_PULSE);

namespace fn {
namespace pulse_detail {

volatile uint32_t overflows = 0;   ///< эпохи: число переполнений Timer1
volatile uint32_t last = 0;        ///< момент последнего захвата
volatile uint32_t prev = 0;        ///< момент предпоследнего захвата
volatile uint32_t per = 0;         ///< период в тактах
volatile uint32_t count = 0;       ///< число захватов
volatile uint16_t lost = 0;        ///< потерянные захваты
volatile uint16_t presc = 1;       ///< делитель
volatile uint8_t  sh = 0;          ///< log2(делителя)
volatile bool     isActive = false;

InputCapture::Callback volatile cb = nullptr;
Result lastErr = Result::Ok;       ///< последняя ошибка (не путать с last — моментом захвата)

constexpr uint32_t TicksPerUs = F_CPU / 1000000UL;

inline Result fail(Result r) { lastErr = r; return r; }
inline Result done() { lastErr = Result::Ok; return Result::Ok; }

inline tmr::Clock clockOf(InputCapture::Presc p) {
    switch (p) {
        case InputCapture::Presc::Div1:    return tmr::Clock::Div1;
        case InputCapture::Presc::Div8:    return tmr::Clock::Div8;
        case InputCapture::Presc::Div64:   return tmr::Clock::Div64;
        case InputCapture::Presc::Div256:  return tmr::Clock::Div256;
        default:                           return tmr::Clock::Div1024;
    }
}

inline uint8_t shiftOf(InputCapture::Presc p) {
    switch (p) {
        case InputCapture::Presc::Div1:    return 0;
        case InputCapture::Presc::Div8:    return 3;
        case InputCapture::Presc::Div64:   return 6;
        case InputCapture::Presc::Div256:  return 8;
        default:                           return 10;
    }
}

inline bool validPresc(InputCapture::Presc p) {
    return p == InputCapture::Presc::Div1  || p == InputCapture::Presc::Div8 ||
           p == InputCapture::Presc::Div64 || p == InputCapture::Presc::Div256 ||
           p == InputCapture::Presc::Div1024;
}

}  // namespace pulse_detail

// ============================ управление ===================================
Result InputCapture::begin(Edge e, Presc p, bool noiseFilter) {
    using namespace pulse_detail;

    if (!validPresc(p)) return fail(Result::BadArg);

    // Таймер — общий ресурс: Pwm/Servo/Stepper могут держать его.
    const Result a = Timer1::acquire(Timer1::Client::Pulse);
    if (!ok(a)) return fail(a);

    // D8 = PB0 как вход; подтяжку задаёт вызывающий при необходимости.
    regs::modeInput(boardPin(8));

    {
        AtomicBlock lock;
        overflows = 0;
        last = 0;
        prev = 0;
        per = 0;
        count = 0;
        lost = 0;
        cb = nullptr;
        presc = uint16_t(p);
        sh = shiftOf(p);
    }

    // Вся настройка железа — через драйвер Timer1.
    Timer1::takeover(Timer1::Mode::Normal, clockOf(p));
    Timer1::setCounter(0);
    Timer1::noiseCancel(noiseFilter);
    Timer1::captureEdge(e == Edge::Rising);
    Timer1::clearCaptureFlag();
    Timer1::clearOvfFlag();
    Timer1::enableCapture();
    Timer1::enableOvf();

    AtomicBlock lock;
    isActive = true;
    return done();
}

void InputCapture::end() {
    Timer1::disableCapture();
    Timer1::disableOvf();
    Timer1::end();
    Timer1::release(Timer1::Client::Pulse);

    AtomicBlock lock;
    pulse_detail::cb = nullptr;
    pulse_detail::isActive = false;
}

void InputCapture::setEdge(Edge e) {
    Timer1::captureEdge(e == Edge::Rising);   // драйвер сам сбрасывает ICF1
}

bool InputCapture::active() { return pulse_detail::isActive; }

// ============================ чтения =======================================
uint32_t InputCapture::lastTicks()   { return atomicRead32(&pulse_detail::last); }
uint32_t InputCapture::periodTicks() { return atomicRead32(&pulse_detail::per); }
uint32_t InputCapture::captures()    { return atomicRead32(&pulse_detail::count); }

uint16_t InputCapture::missed() {
    AtomicBlock lock;
    return pulse_detail::lost;
}

uint16_t InputCapture::prescaler() { return pulse_detail::presc; }

uint32_t InputCapture::periodMicros() {
    const uint32_t t = periodTicks();
    if (t == 0) return 0;

    const uint8_t  s = pulse_detail::sh;
    const uint32_t q = t / pulse_detail::TicksPerUs;   // константа -> сдвиг
    const uint32_t r = t % pulse_detail::TicksPerUs;   // константа -> маска

    // q * 2^s должно влезть в uint32; presc — степень двойки, поэтому
    // граница считается сдвигом, без деления на переменную.
    if (q > (0xFFFFFFFFUL >> s)) return 0xFFFFFFFFUL;

    return uint32_t(q << s) + uint32_t((r << s) / pulse_detail::TicksPerUs);
}

uint32_t InputCapture::frequencyMhz() {
    const uint32_t us = periodMicros();
    if (us == 0 || us == 0xFFFFFFFFUL) return 0;
    return 1000000000UL / us;   // Гц * 1000; погрешность квантования до 1 мкс
}

// ============================ коллбэк ======================================
Result InputCapture::attach(Callback f) {
    if (f == nullptr) return pulse_detail::fail(Result::BadArg);
    AtomicBlock lock;
    if (pulse_detail::cb != nullptr) return pulse_detail::fail(Result::Busy);
    pulse_detail::cb = f;
    return pulse_detail::done();
}

void InputCapture::detach() {
    AtomicBlock lock;
    pulse_detail::cb = nullptr;
}

bool InputCapture::isAttached() { return pulse_detail::cb != nullptr; }

Result InputCapture::lastError() { return pulse_detail::lastErr; }

}  // namespace fn

// ============================================================================
//  ISR — единственные в проекте для этих векторов
// ============================================================================
ISR(TIMER1_CAPT_vect) {
    using namespace fn;
    using namespace fn::pulse_detail;

    // Порядок чтений критичен: ICR1 -> TOV1 -> TCNT1.
    const uint16_t icr  = Timer1::capture();
    const bool     pend = Timer1::overflowPending();
    const uint16_t cnt  = Timer1::counter();

    // ICF1 выставлен снова на входе в ISR: предыдущий захват не прочитан.
    if (Timer1::capturePending()) ++lost;

    uint32_t ov = overflows;
    if (pend && (cnt >= icr)) {
        // Переполнение случилось ДО захвата и ещё не учтено счётчиком эпох.
        // Поправка локальная: overflows не трогаем, ISR OVF доведёт сам.
        ++ov;
    }

    const uint32_t now = uint32_t(ov << 16) | icr;

    if (count == 0) {
        prev = now;
        last = now;
        count = 1;
        return;
    }

    per = uint32_t(now - prev);
    prev = now;
    last = now;
    ++count;

    const InputCapture::Callback f = cb;
    if (f) f(per);
}

ISR(TIMER1_OVF_vect) {
    fn::pulse_detail::overflows += 1UL;
}

#endif  // F_USE_PULSE
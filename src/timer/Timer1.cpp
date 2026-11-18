// ============================================================================
//  Timer1.cpp — 16-битный таймер: режимы, период, захват, арбитраж
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация Timer1. ISR не определяет: векторы CAPT/OVF/COMPA/COMPB
//    отдаются потребителю. Драйвер владение не заявляет (R19).
//
//  ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ПРЕЖНЕЙ ВЕРСИИ
//    - setMode сохранял COM маской 0x0C (reserved-биты) и обнулял оба выхода.
//    - takeover содержал FN_CLAIM_TIMER(1, FN_OWNER_TIMER1): static_assert
//      запрещал любого потребителя, включая Pulse. Убрано.
//    - presc[]/cs[] в .data (14 байт SRAM), uint64-арифметика (~500 Б flash).
//    - resume() пустой; begin() возвращал false при любом тактировании.
//    - periodTicks для PhaseCorrect давал TOP+1 вместо 2*TOP.
//    - include Timer0.h ради enum — перевёрнутая зависимость.
//
//  АТОМАРНОСТЬ
//    TCNT1/OCR1x/ICR1 читаются и пишутся под AtomicBlock: старший байт идёт
//    через TEMP, и ISR между байтами портит значение. Внутри ISR захвата
//    это стоит 4 такта и не влияет на точность: момент защёлкнут аппаратно.
//
//  ОГРАНИЧЕНИЯ
//    - setPeriodUs/setFrequency всегда FastPwmTop; CTC задаётся отдельно.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Timer1.h, TimerCommon.h, core/Atomic.h, core/Resources.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_TIMER1

#include "Timer1.h"

namespace fn {
namespace timer1_detail {

uint8_t savedCs = 0;
Timer1::Client currentClient = Timer1::Client::None;
Result last = Result::Ok;

inline Result fail(Result r) { last = r; return r; }
inline Result done() { last = Result::Ok; return Result::Ok; }

inline void wgmOf(Timer1::Mode m, uint8_t& a, uint8_t& b) {
    a = 0; b = 0;
    switch (m) {
        case Timer1::Mode::Normal:          break;
        case Timer1::Mode::Ctc:             b = uint8_t(1u << WGM12); break;
        case Timer1::Mode::CtcIcr:          b = uint8_t((1u << WGM13) | (1u << WGM12)); break;
        case Timer1::Mode::FastPwm8:        a = uint8_t(1u << WGM10);
                                            b = uint8_t(1u << WGM12); break;
        case Timer1::Mode::FastPwm9:        a = uint8_t(1u << WGM11);
                                            b = uint8_t(1u << WGM12); break;
        case Timer1::Mode::FastPwm10:       a = uint8_t((1u << WGM11) | (1u << WGM10));
                                            b = uint8_t(1u << WGM12); break;
        case Timer1::Mode::PhaseCorrect8:   a = uint8_t(1u << WGM10); break;
        case Timer1::Mode::PhaseCorrect9:   a = uint8_t(1u << WGM11); break;
        case Timer1::Mode::PhaseCorrect10:  a = uint8_t((1u << WGM11) | (1u << WGM10)); break;
        case Timer1::Mode::FastPwmTop:      a = uint8_t(1u << WGM11);
                                            b = uint8_t((1u << WGM13) | (1u << WGM12)); break;
        case Timer1::Mode::PhaseCorrectTop: a = uint8_t(1u << WGM11);
                                            b = uint8_t(1u << WGM13); break;
    }
}

// Записать WGM и делитель, сохранив COM (A) и ICNC1/ICES1 (B).
inline void applyTopMode(uint32_t n, uint8_t cs) {
    uint8_t a = 0, b = 0;
    wgmOf(Timer1::Mode::FastPwmTop, a, b);
    TCCR1A = uint8_t((TCCR1A & 0xF0u) | a);
    ICR1 = uint16_t(n - 1u);
    TCCR1B = uint8_t((TCCR1B & 0xE0u) | b | cs);
    savedCs = cs;
}

}  // namespace timer1_detail

// ============================ арбитраж =====================================
Result Timer1::acquire(Client c) {
#if F_USE_TIMER_ARBITER
    // Единый реестр: Pulse/Pwm/Servo/Stepper видят один и тот же владелец.
    const tmr::Client tc = static_cast<tmr::Client>(static_cast<uint8_t>(c));
    const Result r = tmr::acquire(1u, tc);
    if (!ok(r)) return timer1_detail::fail(r);
#endif
    AtomicBlock lock;
    if (timer1_detail::currentClient != Client::None &&
        timer1_detail::currentClient != c) {
        return timer1_detail::fail(Result::Busy);
    }
    timer1_detail::currentClient = c;
    return timer1_detail::done();
}

void Timer1::release(Client c) {
#if F_USE_TIMER_ARBITER
    tmr::release(1u, static_cast<tmr::Client>(static_cast<uint8_t>(c)));
#endif
    AtomicBlock lock;
    if (timer1_detail::currentClient == c) timer1_detail::currentClient = Client::None;
}

Timer1::Client Timer1::client() { return timer1_detail::currentClient; }

// ============================ владение =====================================
Result Timer1::begin(Mode m, Clock c) {
    if (TCCR1B & 0x07u) return timer1_detail::fail(Result::Busy);
    const Result a = acquire(Client::Generic);
    if (!ok(a)) return a;
    return takeover(m, c);
}

Result Timer1::takeover(Mode m, Clock c) {
    // Владение заявляет потребитель, не драйвер (R19).
    AtomicBlock lock;
    setMode(m);
    const uint8_t cs = tmr::clockBits(c);
    TCCR1B = uint8_t((TCCR1B & 0xF8u) | cs);
    timer1_detail::savedCs = cs;
    return timer1_detail::done();
}

void Timer1::end() {
    // Освобождаем общий реестр тем клиентом, который им владеет: иначе
    // захват останется висеть после остановки таймера (D69).
    const Client c = timer1_detail::currentClient;
    if (c != Client::None) {
#if F_USE_TIMER_ARBITER
        tmr::release(1u, static_cast<tmr::Client>(static_cast<uint8_t>(c)));
#endif
    }
    AtomicBlock lock;
    TIMSK1 = 0;
    TCCR1A = 0;
    TCCR1B = 0;
    TIFR1 = uint8_t((1u << ICF1) | (1u << OCF1A) | (1u << OCF1B) | (1u << TOV1));
    timer1_detail::savedCs = 0;
    timer1_detail::currentClient = Client::None;
}

bool Timer1::ownsTimer() { return (TCCR1B & 0x07u) != 0; }

// ============================ режимы =======================================
void Timer1::setMode(Mode m) {
    uint8_t a = 0, b = 0;
    timer1_detail::wgmOf(m, a, b);
    TCCR1A = uint8_t((TCCR1A & 0xF0u) | a);   // сохранить COM1A/COM1B
    TCCR1B = uint8_t((TCCR1B & 0xE0u) | b);   // сохранить ICNC1/ICES1/CS
}

void Timer1::setClock(Clock c) {
    AtomicBlock lock;
    const uint8_t cs = tmr::clockBits(c);
    TCCR1B = uint8_t((TCCR1B & 0xF8u) | cs);
    timer1_detail::savedCs = cs;
}

Timer1::Clock Timer1::clock() { return static_cast<Clock>(TCCR1B & 0x07u); }

void Timer1::pause() {
    AtomicBlock lock;
    timer1_detail::savedCs = uint8_t(TCCR1B & 0x07u);
    TCCR1B = uint8_t(TCCR1B & 0xF8u);
}

void Timer1::resume() {
    AtomicBlock lock;
    TCCR1B = uint8_t((TCCR1B & 0xF8u) | timer1_detail::savedCs);
}

// ============================ TOP и период =================================
void Timer1::setTop(uint16_t v) { AtomicBlock l; ICR1 = v; }
uint16_t Timer1::top() { AtomicBlock l; return ICR1; }

uint32_t Timer1::periodTicks() {
    const uint8_t wgm = uint8_t(((TCCR1B & 0x18u) >> 1) | (TCCR1A & 0x03u));
    switch (wgm) {
        case 4:  return uint32_t(ocrA()) + 1u;      // CTC, TOP=OCR1A
        case 12: return uint32_t(top()) + 1u;       // CTC, TOP=ICR1
        case 14: return uint32_t(top()) + 1u;       // FastPWM, TOP=ICR1
        case 15: return uint32_t(ocrA()) + 1u;      // FastPWM, TOP=OCR1A
        case 5:  return 256u;
        case 6:  return 512u;
        case 7:  return 1024u;
        case 1:  return 510u;                       // PhaseCorrect: 2*TOP
        case 2:  return 1022u;
        case 3:  return 2046u;
        case 10: return uint32_t(top()) * 2u;
        case 11: return uint32_t(ocrA()) * 2u;
        default: return 65536u;                     // Normal
    }
}

uint32_t Timer1::setPeriodUs(uint32_t us) {
    tmr::Clock c;
    const uint32_t cycles = us * tmr::TicksPerUs;
    uint32_t n = tmr::bestClock(cycles, 65536UL, c);
    if (n == 0) { timer1_detail::fail(Result::OutOfRange); return 0; }

    AtomicBlock lock;
    timer1_detail::applyTopMode(n, tmr::clockBits(c));
    timer1_detail::done();
    return tmr::ticksToUs(n, c);
}

uint32_t Timer1::setFrequency(uint32_t hz) {
    if (hz == 0) return 0;
    tmr::Clock c;
    const uint32_t cycles = F_CPU / hz;      // без округления до целых мкс
    uint32_t n = tmr::bestClock(cycles, 65536UL, c);
    if (n == 0) { timer1_detail::fail(Result::OutOfRange); return 0; }

    AtomicBlock lock;
    timer1_detail::applyTopMode(n, tmr::clockBits(c));
    timer1_detail::done();
    return (F_CPU / tmr::prescaleOf(c)) / n;
}

// ============================ выходы и OCR =================================
void Timer1::outputA(Out m) {
    AtomicBlock lock;
    TCCR1A = uint8_t((TCCR1A & 0x3Fu) | (tmr::comBits(m) << 6));
}

void Timer1::outputB(Out m) {
    AtomicBlock lock;
    TCCR1A = uint8_t((TCCR1A & 0xCFu) | (tmr::comBits(m) << 4));
}

void Timer1::setOcrA(uint16_t v) { AtomicBlock l; OCR1A = v; }
void Timer1::setOcrB(uint16_t v) { AtomicBlock l; OCR1B = v; }
uint16_t Timer1::ocrA() { AtomicBlock l; return OCR1A; }
uint16_t Timer1::ocrB() { AtomicBlock l; return OCR1B; }

uint16_t Timer1::counter() { AtomicBlock l; return TCNT1; }
void Timer1::setCounter(uint16_t v) { AtomicBlock l; TCNT1 = v; }

// ============================ захват =======================================
void Timer1::captureEdge(bool rising) {
    AtomicBlock lock;
    if (rising) TCCR1B = uint8_t(TCCR1B | (1u << ICES1));
    else        TCCR1B = uint8_t(TCCR1B & uint8_t(~(1u << ICES1)));
    TIFR1 = uint8_t(1u << ICF1);       // смена фронта сама выставляет флаг
}

void Timer1::noiseCancel(bool on) {
    AtomicBlock lock;
    if (on) TCCR1B = uint8_t(TCCR1B | (1u << ICNC1));
    else    TCCR1B = uint8_t(TCCR1B & uint8_t(~(1u << ICNC1)));
}

uint16_t Timer1::capture() { AtomicBlock l; return ICR1; }
bool Timer1::capturePending()  { return (TIFR1 & (1u << ICF1)) != 0; }
bool Timer1::overflowPending() { return (TIFR1 & (1u << TOV1)) != 0; }
void Timer1::clearCaptureFlag() { TIFR1 = uint8_t(1u << ICF1); }
void Timer1::clearOvfFlag()     { TIFR1 = uint8_t(1u << TOV1); }

// ============================ снапшот ======================================
Timer1::State Timer1::save() {
    AtomicBlock lock;
    State s;
    s.tccrA = TCCR1A;
    s.tccrB = TCCR1B;
    s.timsk = TIMSK1;
    s.ocrA = OCR1A;
    s.ocrB = OCR1B;
    s.icr = ICR1;
    return s;
}

void Timer1::restore(const State& s) {
    AtomicBlock lock;
    TCCR1B = 0;
    TCCR1A = s.tccrA;
    OCR1A = s.ocrA;
    OCR1B = s.ocrB;
    ICR1 = s.icr;
    TIMSK1 = s.timsk;
    TCCR1B = s.tccrB;
    timer1_detail::savedCs = uint8_t(s.tccrB & 0x07u);
}

Result Timer1::lastError() { return timer1_detail::last; }

}  // namespace fn

#endif  // F_USE_TIMER1
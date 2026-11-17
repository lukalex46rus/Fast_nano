// ============================================================================
//  Timer0.cpp — 8-битный таймер: захват, режимы, период без 64-битной арифметики
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация Timer0. ISR не определяет: TIMER0_OVF принадлежит ядру Arduino,
//    COMPA/COMPB отдаются потребителю через enableCompA/B.
//
//  ЗАХВАТ И ОТВЕТСТВЕННОСТЬ
//    millisSafe() — constexpr от FN_TIMER0_DEVICE. Пока таймер за ядром,
//    begin/setPeriodUs/setFrequency отказывают (Busy): смена WGM/TOP/CS
//    ломает millis() в разы, а не «немного». takeover() снимает запрет и
//    помечает флаг: с этого момента ответственность на вызывающем.
//    restoreArduino() возвращает конфигурацию wiring.c.
//
//  ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ПРЕЖНЕЙ ВЕРСИИ
//    - begin() проверял CS и возвращал false всегда: ядро уже запустило
//      таймер с делителем 64. Теперь Busy с явной причиной + takeover().
//    - setPeriodUs держал таблицы presc[]/cs[] в .data (14 байт SRAM) и
//      считал в uint64 (__muldi3 + __udivdi3, ~500 Б flash).
//    - resume() был пустым: после pause() таймер не запускался.
//    - outCom дублировался здесь и в TimerCommon.h; теперь один tmr::comBits.
//    - Гейт был "#if F_USE_PWM || 1" — всегда истинно.
//
//  ОГРАНИЧЕНИЯ
//    - periodTicks() для PhaseCorrect = 2*TOP, не TOP+1.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Timer0.h, TimerCommon.h, core/Atomic.h, core/Resources.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_TIMER0

#include "Timer0.h"

namespace fn {
namespace timer0_detail {

uint8_t savedCs = 0;
bool    isTaken = false;
Result  last = Result::Ok;

inline Result fail(Result r) { last = r; return r; }
inline Result done() { last = Result::Ok; return Result::Ok; }

// WGM02..00 упакованы: бит 2 -> TCCR0B (WGM02), биты 1:0 -> TCCR0A.
inline void wgmOf(Timer0::Mode m, uint8_t& a, uint8_t& b) {
    a = 0; b = 0;
    switch (m) {
        case Timer0::Mode::Normal:          break;
        case Timer0::Mode::Ctc:             a = uint8_t(1u << WGM01); break;
        case Timer0::Mode::FastPwm:         a = uint8_t((1u << WGM01) | (1u << WGM00)); break;
        case Timer0::Mode::PhaseCorrect:    a = uint8_t(1u << WGM00); break;
        case Timer0::Mode::FastPwmTop:      a = uint8_t((1u << WGM01) | (1u << WGM00));
                                            b = uint8_t(1u << WGM02); break;
        case Timer0::Mode::PhaseCorrectTop: a = uint8_t(1u << WGM00);
                                            b = uint8_t(1u << WGM02); break;
    }
}

}  // namespace timer0_detail

bool Timer0::taken() { return timer0_detail::isTaken; }

bool Timer0::canReconfigure() {
    return !millisSafe() || timer0_detail::isTaken;
}

// ============================ владение =====================================
Result Timer0::begin(Mode m, Clock c) {
    if (!canReconfigure()) return timer0_detail::fail(Result::Busy);
    if (TCCR0B & 0x07u) return timer0_detail::fail(Result::Busy);
    return takeover(m, c);
}

Result Timer0::takeover(Mode m, Clock c) {
    AtomicBlock lock;
    timer0_detail::isTaken = true;
    setMode(m);
    const uint8_t cs = tmr::clockBits(c);
    TCCR0B = uint8_t((TCCR0B & 0xF8u) | cs);
    timer0_detail::savedCs = cs;
    return timer0_detail::done();
}

void Timer0::end() {
    AtomicBlock lock;
    TIMSK0 = 0;
    TCCR0A = 0;
    TCCR0B = 0;
    TIFR0 = uint8_t((1u << OCF0A) | (1u << OCF0B) | (1u << TOV0));
    timer0_detail::savedCs = 0;
    timer0_detail::isTaken = false;
}

void Timer0::restoreArduino() {
    AtomicBlock lock;
    // Ровно то, что ставит wiring.c: FastPWM, делитель 64, TOIE0.
    TCCR0A = uint8_t((1u << WGM01) | (1u << WGM00));
    TCCR0B = 0;
    OCR0A = 0;
    OCR0B = 0;
    TCNT0 = 0;
    TIFR0 = uint8_t((1u << OCF0A) | (1u << OCF0B) | (1u << TOV0));
    TIMSK0 = uint8_t(1u << TOIE0);
    TCCR0B = uint8_t(1u << CS01) | uint8_t(1u << CS00);   // 64
    timer0_detail::savedCs = uint8_t(1u << CS01) | uint8_t(1u << CS00);
    timer0_detail::isTaken = false;
}

// ============================ режимы =======================================
void Timer0::setMode(Mode m) {
    uint8_t a = 0, b = 0;
    timer0_detail::wgmOf(m, a, b);
    // 0xF0 сохраняет COM0A/COM0B; CS сохраняется отдельно.
    TCCR0A = uint8_t((TCCR0A & 0xF0u) | a);
    TCCR0B = uint8_t((TCCR0B & 0x07u) | b);
}

void Timer0::setClock(Clock c) {
    AtomicBlock lock;
    const uint8_t cs = tmr::clockBits(c);
    TCCR0B = uint8_t((TCCR0B & 0xF8u) | cs);
    timer0_detail::savedCs = cs;
}

Timer0::Clock Timer0::clock() { return static_cast<Clock>(TCCR0B & 0x07u); }

void Timer0::pause() {
    AtomicBlock lock;
    timer0_detail::savedCs = uint8_t(TCCR0B & 0x07u);
    TCCR0B = uint8_t(TCCR0B & 0xF8u);
}

void Timer0::resume() {
    AtomicBlock lock;
    TCCR0B = uint8_t((TCCR0B & 0xF8u) | timer0_detail::savedCs);
}

// ============================ период =======================================
uint16_t Timer0::periodTicks() {
    const uint8_t wgm = uint8_t(((TCCR0B & 0x08u) >> 1) | (TCCR0A & 0x03u));
    switch (wgm) {
        case 2: return uint16_t(OCR0A) + 1u;         // CTC
        case 7: return uint16_t(OCR0A) + 1u;         // FastPWM TOP=OCRA
        case 1: return 510u;                         // PhaseCorrect, 2*255
        case 5: return uint16_t(OCR0A) * 2u;         // PhaseCorrect TOP=OCRA
        case 3: return 256u;                         // FastPWM
        default: return 256u;                        // Normal
    }
}

uint32_t Timer0::setPeriodUs(uint32_t us) {
    if (!canReconfigure()) { timer0_detail::fail(Result::Busy); return 0; }

    tmr::Clock c;
    const uint32_t cycles = us * tmr::TicksPerUs;
    uint32_t n = tmr::bestClock(cycles, 256UL, c);
    if (n == 0) { timer0_detail::fail(Result::OutOfRange); return 0; }

    AtomicBlock lock;
    TCCR0A = uint8_t((TCCR0A & 0xF0u) | (1u << WGM01));   // CTC
    TCCR0B = uint8_t(TCCR0B & 0x07u);                     // WGM02 = 0
    OCR0A = uint8_t(n - 1u);
    const uint8_t cs = tmr::clockBits(c);
    TCCR0B = uint8_t((TCCR0B & 0xF8u) | cs);
    timer0_detail::savedCs = cs;
    timer0_detail::done();
    return tmr::ticksToUs(n, c);
}

uint32_t Timer0::setFrequency(uint32_t hz) {
    if (hz == 0) return 0;
    if (!canReconfigure()) { timer0_detail::fail(Result::Busy); return 0; }

    tmr::Clock c;
    const uint32_t cycles = F_CPU / hz;      // без промежуточного округления до мкс
    uint32_t n = tmr::bestClock(cycles, 256UL, c);
    if (n == 0) { timer0_detail::fail(Result::OutOfRange); return 0; }

    AtomicBlock lock;
    TCCR0A = uint8_t((TCCR0A & 0xF0u) | (1u << WGM01));
    TCCR0B = uint8_t(TCCR0B & 0x07u);
    OCR0A = uint8_t(n - 1u);
    const uint8_t cs = tmr::clockBits(c);
    TCCR0B = uint8_t((TCCR0B & 0xF8u) | cs);
    timer0_detail::savedCs = cs;
    timer0_detail::done();
    return (F_CPU / tmr::prescaleOf(c)) / n;
}

// ============================ выходы =======================================
void Timer0::outputA(Out m) {
    AtomicBlock lock;
    TCCR0A = uint8_t((TCCR0A & 0x3Fu) | (tmr::comBits(m) << 6));
}

void Timer0::outputB(Out m) {
    AtomicBlock lock;
    TCCR0A = uint8_t((TCCR0A & 0xCFu) | (tmr::comBits(m) << 4));
}

// ============================ снапшот ======================================
Timer0::State Timer0::save() {
    AtomicBlock lock;
    State s;
    s.tccrA = TCCR0A;
    s.tccrB = TCCR0B;
    s.ocrA = OCR0A;
    s.ocrB = OCR0B;
    s.timsk = TIMSK0;
    return s;
}

void Timer0::restore(const State& s) {
    AtomicBlock lock;
    TCCR0B = 0;
    TCCR0A = s.tccrA;
    OCR0A = s.ocrA;
    OCR0B = s.ocrB;
    TIMSK0 = s.timsk;
    TCCR0B = s.tccrB;
    timer0_detail::savedCs = uint8_t(s.tccrB & 0x07u);
}

Result Timer0::lastError() { return timer0_detail::last; }

}  // namespace fn

#endif  // F_USE_TIMER0
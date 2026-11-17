// ============================================================================
//  Timer2.cpp — 8-битный таймер со своим набором делителей
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация Timer2. Подбор делителя собственный (набор 1/8/32/64/128/256/1024),
//    без таблиц в RAM и без 64-битной арифметики.
//
//  ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ПРЕЖНЕЙ ВЕРСИИ
//    - Использовал Timer0::Clock: код 3 означал Div64 вместо Div32, код 5 —
//      ExtFall вместо Div128. Периоды были неверными.
//    - presc[]/cs[] в .data (14 байт SRAM).
//    - uint64 арифметика -> __muldi3/__udivdi3 (~500 Б flash).
//    - resume() пустой.
//    - enableAsync() включал AS2 и зависал в while(ASSR...): на Nano
//      TOSC1/TOSC2 заняты основным кварцем (PB6/PB7). Функции удалены.
//    - Отсутствовал гейт F_USE_TIMER2.
//
//  ОГРАНИЧЕНИЯ
//    - setPeriodUs переводит в CTC (TOP=OCR2A). Для PWM задайте режим сами.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Timer2.h, TimerCommon.h, core/Atomic.h, core/Resources.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_TIMER2

#include "Timer2.h"

namespace fn {
namespace timer2_detail {

uint8_t savedCs = 0;
bool owned = false;

// Подбор минимального делителя Timer2, при котором cycles влезает в maxCount.
uint32_t bestClock2(uint32_t cycles, uint32_t maxCount, Timer2::Clock2& out) {
    static const Timer2::Clock2 kClocks[7] = {
        Timer2::Clock2::Div1,   Timer2::Clock2::Div8,   Timer2::Clock2::Div32,
        Timer2::Clock2::Div64,  Timer2::Clock2::Div128, Timer2::Clock2::Div256,
        Timer2::Clock2::Div1024
    };
    for (uint8_t i = 0; i < 7; ++i) {
        const uint16_t d = Timer2::prescaleOf(kClocks[i]);
        const uint32_t t = cycles / d;
        if (t >= 1 && t <= maxCount) {
            out = kClocks[i];
            return t;
        }
    }
    out = Timer2::Clock2::Div1024;
    return 0;
}

inline uint32_t ticksToUs2(uint32_t ticks, Timer2::Clock2 c) {
    const uint16_t d = Timer2::prescaleOf(c);
    if (d == 0) return 0;
    if (ticks > (0xFFFFFFFFUL / d)) return (ticks / tmr::TicksPerUs) * d;
    return (ticks * d) / tmr::TicksPerUs;
}

}  // namespace timer2_detail

// ============================ владение =====================================
Result Timer2::begin(Mode m, Clock2 c) {
    if (TCCR2B & 0x07u) return Result::Busy;
    return takeover(m, c);
}

Result Timer2::takeover(Mode m, Clock2 c) {
    FN_CLAIM_TIMER(2, FN_OWNER_TIMER2);
    AtomicBlock lock;
    timer2_detail::owned = true;
    setMode(m);
    const uint8_t cs = uint8_t(c) & 0x07u;
    TCCR2B = uint8_t((TCCR2B & 0xF8u) | cs);
    timer2_detail::savedCs = cs;
    return Result::Ok;
}

void Timer2::end() {
    AtomicBlock lock;
    TIMSK2 = 0;
    TCCR2A = 0;
    TCCR2B = 0;
    TIFR2 = uint8_t((1u << OCF2A) | (1u << OCF2B) | (1u << TOV2));
    timer2_detail::savedCs = 0;
    timer2_detail::owned = false;
}

bool Timer2::ownsTimer() { return timer2_detail::owned; }

// ============================ режимы =======================================
void Timer2::setMode(Mode m) {
    uint8_t a = 0, b = 0;
    switch (m) {
        case Mode::Normal:          break;
        case Mode::Ctc:             a = uint8_t(1u << WGM21); break;
        case Mode::FastPwm:         a = uint8_t((1u << WGM21) | (1u << WGM20)); break;
        case Mode::PhaseCorrect:    a = uint8_t(1u << WGM20); break;
        case Mode::FastPwmTop:      a = uint8_t((1u << WGM21) | (1u << WGM20));
                                    b = uint8_t(1u << WGM22); break;
        case Mode::PhaseCorrectTop: a = uint8_t(1u << WGM20);
                                    b = uint8_t(1u << WGM22); break;
    }
    TCCR2A = uint8_t((TCCR2A & 0xF0u) | a);   // сохранить COM2A/COM2B
    TCCR2B = uint8_t((TCCR2B & 0xF0u) | b);   // сохранить FOC2A/B и CS
}

void Timer2::setClock(Clock2 c) {
    AtomicBlock lock;
    const uint8_t cs = uint8_t(c) & 0x07u;
    TCCR2B = uint8_t((TCCR2B & 0xF8u) | cs);
    timer2_detail::savedCs = cs;
}

Timer2::Clock2 Timer2::clock() {
    return static_cast<Clock2>(TCCR2B & 0x07u);
}

void Timer2::pause() {
    AtomicBlock lock;
    timer2_detail::savedCs = uint8_t(TCCR2B & 0x07u);
    TCCR2B = uint8_t(TCCR2B & 0xF8u);
}

void Timer2::resume() {
    AtomicBlock lock;
    TCCR2B = uint8_t((TCCR2B & 0xF8u) | timer2_detail::savedCs);
}

// ============================ период =======================================
uint16_t Timer2::periodTicks() {
    const uint8_t wgm = uint8_t(((TCCR2B & 0x08u) >> 1) | (TCCR2A & 0x03u));
    switch (wgm) {
        case 2: return uint16_t(OCR2A) + 1u;   // CTC
        case 7: return uint16_t(OCR2A) + 1u;   // FastPWM TOP=OCRA
        case 1: return 512u;                   // PhaseCorrect
        case 5: return uint16_t(OCR2A) * 2u;
        case 3: return 256u;                   // FastPWM
        default: return 256u;                  // Normal
    }
}

uint32_t Timer2::setPeriodUs(uint32_t us) {
    Clock2 c;
    const uint32_t cycles = us * tmr::TicksPerUs;
    uint32_t n = timer2_detail::bestClock2(cycles, 256UL, c);
    if (n == 0) { n = 256UL; c = Clock2::Div1024; }

    AtomicBlock lock;
    timer2_detail::owned = true;
    TCCR2A = uint8_t((TCCR2A & 0xF0u) | (1u << WGM21));   // CTC
    OCR2A = uint8_t(n - 1u);
    const uint8_t cs = uint8_t(c) & 0x07u;
    TCCR2B = uint8_t((TCCR2B & 0xF0u) | cs);
    timer2_detail::savedCs = cs;

    return timer2_detail::ticksToUs2(n, c);
}

uint32_t Timer2::setFrequency(uint32_t hz) {
    if (hz == 0) return 0;
    Clock2 c;
    const uint32_t cycles = (F_CPU / hz);
    uint32_t n = timer2_detail::bestClock2(cycles, 256UL, c);
    if (n == 0) { n = 256UL; c = Clock2::Div1024; }

    AtomicBlock lock;
    timer2_detail::owned = true;
    TCCR2A = uint8_t((TCCR2A & 0xF0u) | (1u << WGM21));
    OCR2A = uint8_t(n - 1u);
    const uint8_t cs = uint8_t(c) & 0x07u;
    TCCR2B = uint8_t((TCCR2B & 0xF0u) | cs);
    timer2_detail::savedCs = cs;

    return (F_CPU / prescaleOf(c)) / n;
}

// ============================ выходы =======================================
void Timer2::outputA(Out m) {
    AtomicBlock lock;
    TCCR2A = uint8_t((TCCR2A & 0x3Fu) | (tmr::comBits(m) << 6));
}

void Timer2::outputB(Out m) {
    AtomicBlock lock;
    TCCR2A = uint8_t((TCCR2A & 0xCFu) | (tmr::comBits(m) << 4));
}

// ============================ снапшот ======================================
Timer2::State Timer2::save() {
    AtomicBlock lock;
    State s;
    s.tccrA = TCCR2A;
    s.tccrB = TCCR2B;
    s.timsk = TIMSK2;
    s.ocrA = OCR2A;
    s.ocrB = OCR2B;
    return s;
}

void Timer2::restore(const State& s) {
    AtomicBlock lock;
    TCCR2B = 0;
    TCCR2A = s.tccrA;
    OCR2A = s.ocrA;
    OCR2B = s.ocrB;
    TIMSK2 = s.timsk;
    TCCR2B = s.tccrB;
    timer2_detail::savedCs = uint8_t(s.tccrB & 0x07u);
}

}  // namespace fn

#endif  // F_USE_TIMER2

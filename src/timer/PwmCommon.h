#pragma once
// ============================================================================
//  PwmCommon.h — единое ядро ШИМ для трёх таймеров
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Одна реализация логики ШИМ. Таймеры отличаются разрядностью, набором
//    делителей, адресами регистров, наличием ICR и маской сохраняемых битов
//    TCCRnB — всё это параметры traits. Прежние PwmTimer0/1/2 были тремя
//    копиями одного кода.
//
//  КОНТРАКТ TRAITS
//    using value_type;                        uint8_t или uint16_t
//    static constexpr uint8_t bits;           8 или 16
//    static constexpr bool hasIcr;            ICR как TOP (только Timer1)
//    static constexpr bool lockedByCore;      таймер принадлежит ядру Arduino
//    static constexpr uint8_t tccrBKeep;      маска сохраняемых битов TCCRnB
//    static constexpr uint8_t wgmBMask;       маска B-части WGM в упаковке (<<1 в регистр)
//    static constexpr uint8_t prescaleCount;  число доступных делителей
//    static volatile uint8_t& tccrA();
//    static volatile uint8_t& tccrB();
//    static uint8_t wgmBits(PwmMode);         WGM упакован: [3:2]->B, [1:0]->A
//    static uint16_t prescaleAt(uint8_t i);
//    static uint8_t clockBits(uint16_t d);
//    static uint16_t prescaleOf(uint8_t cs);
//    static void writeTop(uint32_t);
//    static uint32_t readTop();
//    static void writeOcrA(uint32_t) / writeOcrB(uint32_t);
//    static uint32_t readOcrA() / readOcrB();
//
//  API
//    begin(PwmMode) -> Result
//    end()                                  отключить выходы, таймер не гасить
//    mode() / ownsTimer()
//    setFrequency(hz) -> uint32_t            фактическая частота; 0 если нельзя
//    frequency() -> uint32_t
//    setTop(n) -> Result                     разрешение n градаций
//    top() / resolution() -> uint32_t
//    attachA(PwmOut) / attachB(PwmOut) -> Result
//    detachA() / detachB()
//    setDutyA(v) / setDutyB(v) -> Result     сырое значение OCR
//    dutyA() / dutyB() -> uint32_t
//    setDutyPercentA/B(pct) -> Result        0..100
//    setDutyPermilleA/B(pm) -> Result        0..1000, шаг 0.1%
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - begin() захватывает таймер через tmr::acquire(N, Client::Pwm). Если его
//      держит Pulse/Servo/Stepper, вернёт Busy и ничего не изменит. end()
//      освобождает захват. Реестр advisory: драйверы Timer0/1/2 владение не
//      заявляют, поэтому прямой вызов Timer1::takeover мимо acquire конфликт
//      не поймает — потребитель обязан захватывать.
//    - lockedByCore (Timer0): режимы *Top и setFrequency запрещены. Меняются
//      только COM и OCR0B — millis() жив.
//    - В *Top на таймерах без ICR TOP = OCRnA, поэтому канал A недоступен:
//      attachA/setDutyA вернут Unsupported.
//    - 100% скважность: в FastPWM при TOP = MAX значение OCR = TOP+1 не
//      помещается в регистр, поэтому кламп идёт до TOP. Максимум = TOP/(TOP+1),
//      то есть 255/256 = 99.6%. Для гарантированного HIGH переключите вывод
//      в GPIO. На Timer1 при TOP < 65535 значение TOP+1 проходит и 100% работает.
//    - setFrequency сохраняет выбранный наклон (Fast или PhaseCorrect):
//      в begin(PhaseCorrectTop) частота считается по формуле двойного наклона.
//    - Все RMW по TCCR и все 16-битные обращения под AtomicBlock.
//    - attachX сбрасывает ОБА COM-бита канала: иначе Inverted -> Normal
//      оставлял инверсию.
//    - Маска TCCRnB берётся из traits, а не литерал 0xE0: у Timer0/2 бит 4
//      это WGM02/WGM22, и его сохранение ломало смену режима.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта на инстанцированный таймер (mode + cs + owned).
//    Flash: ~250-400 байт на таймер; неиспользуемые методы не инстанцируются.
//    Таблиц делителей в RAM нет.
//
//  ЗАЯВКА РЕСУРСОВ
//    Engine не содержит FN_CLAIM_*: это шаблон, static_assert в его теле
//    сработал бы при инстанцировании любого метода любого таймера.
//    Заявки ставятся в прикладном коде на уровне файла — см. шапки
//    PwmTimer0.h / PwmTimer1.h / PwmTimer2.h.
//
//  ПРИМЕР
//    using P = fn::pwm::Engine<fn::pwm::Traits1>;
//    P::begin(fn::PwmMode::FastTop);
//    P::setFrequency(20000);
//    P::attachB(fn::PwmOut::Normal);
//    P::setDutyPermilleB(750);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, timer/Pwm.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "Pwm.h"
#include "TimerCommon.h"

#include <stdint.h>

#if F_USE_PWM

namespace fn {
namespace pwm {

template <typename Tr>
class Engine {
public:
    using value_type = typename Tr::value_type;

    // ------------------------------ запуск ---------------------------------
    static Result begin(PwmMode m) {
        if (Tr::lockedByCore && modeUsesOcrATop(m)) return Result::Unsupported;

        // Разделяемый таймер: захватываем до любой записи регистров.
#if F_USE_TIMER_ARBITER
        if (!Tr::lockedByCore) {
            const Result a = tmr::acquire(Tr::timerIndex, tmr::Client::Pwm);
            if (!ok(a)) return a;
        }
#endif

        AtomicBlock lock;
        applyWgm(m);
        s_mode = m;

        if (!Tr::lockedByCore) {
            const uint16_t d = (Tr::bits == 8) ? 64u : 8u;
            const uint8_t cs = Tr::clockBits(d);
            Tr::tccrB() = uint8_t((Tr::tccrB() & 0xF8u) | cs);
            s_cs = cs;
        } else {
            s_cs = uint8_t(Tr::tccrB() & 0x07u);
        }
        s_owned = true;
        return Result::Ok;
    }

    static void end() {
        AtomicBlock lock;
        Tr::tccrA() = uint8_t(Tr::tccrA() & 0x0Fu);   // COM = 0, WGM сохранить
        s_owned = false;
        s_cs = 0;
#if F_USE_TIMER_ARBITER
        if (!Tr::lockedByCore) tmr::release(Tr::timerIndex, tmr::Client::Pwm);
#endif
    }

    static PwmMode mode() { return s_mode; }
    static bool ownsTimer() { return s_owned; }

    // ------------------------------ частота --------------------------------
    static uint32_t setFrequency(uint32_t hz) {
        if (hz == 0 || Tr::lockedByCore) return 0;
#if F_USE_TIMER_ARBITER
        if (!s_owned) {
            const Result a = tmr::acquire(Tr::timerIndex, tmr::Client::Pwm);
            if (!ok(a)) return 0;
        }
#endif

        const bool phase = modeIsPhase(s_mode);
        const uint32_t maxTop = fixedTop();
        uint32_t bestN = 0;
        uint16_t bestD = 0;
        uint8_t bestCs = 0;

        for (uint8_t i = 0; i < Tr::prescaleCount; ++i) {
            const uint16_t d = Tr::prescaleAt(i);
            const uint32_t perTick = F_CPU / d;
            // Fast: f = perTick / n;  PhaseCorrect: f = perTick / (2n)
            const uint32_t denom = phase ? (hz * 2u) : hz;
            if (denom == 0 || perTick < denom) continue;
            const uint32_t n = perTick / denom;
            if (n < 1 || n > maxTop) continue;
            bestN = n; bestD = d; bestCs = Tr::clockBits(d);
            break;
        }
        if (bestN == 0) return 0;

        AtomicBlock lock;
        s_mode = phase ? PwmMode::PhaseCorrectTop : PwmMode::FastTop;
        applyWgm(s_mode);
        Tr::writeTop(bestN - 1u);
        Tr::tccrB() = uint8_t((Tr::tccrB() & Tr::tccrBKeep) |
                              wgmB(s_mode) | bestCs);
        s_cs = bestCs;
        s_owned = true;

        const uint32_t div = phase ? 2u : 1u;
        return (F_CPU / bestD) / (bestN * div);
    }

    static uint32_t frequency() {
        const uint16_t d = Tr::prescaleOf(s_cs);
        if (d == 0) return 0;
        const uint32_t n = resolution();
        if (n == 0) return 0;
        const uint32_t div = modeIsPhase(s_mode) ? 2u : 1u;
        return (F_CPU / d) / (n * div);
    }

    // ------------------------------ TOP ------------------------------------
    static Result setTop(uint32_t n) {
        if (Tr::lockedByCore) return Result::Unsupported;
        if (!modeUsesOcrATop(s_mode)) return Result::Unsupported;
        if (n == 0 || n > fixedTop()) return Result::OutOfRange;
        AtomicBlock lock;
        Tr::writeTop(n - 1u);
        return Result::Ok;
    }

    static uint32_t top() {
        return modeUsesOcrATop(s_mode) ? Tr::readTop() : fixedTop();
    }

    static uint32_t resolution() { return top() + 1u; }

    // ------------------------------ выходы ---------------------------------
    static Result attachA(PwmOut o) {
        if (modeUsesOcrATop(s_mode) && !Tr::hasIcr) return Result::Unsupported;
        AtomicBlock lock;
        Tr::tccrA() = uint8_t((Tr::tccrA() & 0x3Fu) | (uint8_t(o) << 6));
        return Result::Ok;
    }

    static Result attachB(PwmOut o) {
        AtomicBlock lock;
        Tr::tccrA() = uint8_t((Tr::tccrA() & 0xCFu) | (uint8_t(o) << 4));
        return Result::Ok;
    }

    static void detachA() { attachA(PwmOut::Disabled); }
    static void detachB() { attachB(PwmOut::Disabled); }

    // ------------------------------ скважность -----------------------------
    static Result setDutyA(uint32_t v) {
        if (modeUsesOcrATop(s_mode) && !Tr::hasIcr) return Result::Unsupported;
        AtomicBlock lock;
        Tr::writeOcrA(clampDuty(v));
        return Result::Ok;
    }

    static Result setDutyB(uint32_t v) {
        AtomicBlock lock;
        Tr::writeOcrB(clampDuty(v));
        return Result::Ok;
    }

    static uint32_t dutyA() { return Tr::readOcrA(); }
    static uint32_t dutyB() { return Tr::readOcrB(); }

    static Result setDutyPercentA(uint8_t pct) {
        if (pct > 100) return Result::OutOfRange;
        return setDutyA(scaleDuty(pct, 100u));
    }
    static Result setDutyPercentB(uint8_t pct) {
        if (pct > 100) return Result::OutOfRange;
        return setDutyB(scaleDuty(pct, 100u));
    }
    static Result setDutyPermilleA(uint16_t pm) {
        if (pm > 1000) return Result::OutOfRange;
        return setDutyA(scaleDuty(pm, 1000u));
    }
    static Result setDutyPermilleB(uint16_t pm) {
        if (pm > 1000) return Result::OutOfRange;
        return setDutyB(scaleDuty(pm, 1000u));
    }

private:
    Engine() = delete;

    static constexpr uint32_t fixedTop() { return (uint32_t(1) << Tr::bits) - 1u; }

		// B-часть WGM: упакованные биты [3:2] сдвигаются на позицию регистра.
    // Timer1: WGM13:12 -> биты 4:3 (сдвиг 1 от маски 0x0C).
    // Timer0/2: WGMn2 -> бит 3 (сдвиг 1 от маски 0x04).
    static uint8_t wgmB(PwmMode m) {
        return uint8_t((Tr::wgmBits(m) & Tr::wgmBMask) << 1);
    }

    // Записать WGM, сохранив COM (7:4) в A, значимые биты в B и CS (2:0).
    static void applyWgm(PwmMode m) {
        const uint8_t w = Tr::wgmBits(m);
        Tr::tccrA() = uint8_t((Tr::tccrA() & 0xF0u) | (w & 0x03u));
        Tr::tccrB() = uint8_t((Tr::tccrB() & Tr::tccrBKeep) |
                              wgmB(m) | (Tr::tccrB() & 0x07u));
    }

    // Кламп до TOP, а не до TOP+1: при TOP=MAX значение 256 не влезает
    // в uint8_t и превратилось бы в 0 (инверсия вместо полного уровня).
    static uint32_t clampDuty(uint32_t v) {
        const uint32_t t = top();
        const uint32_t lim = (t < fixedTop()) ? (t + 1u) : t;
        return (v > lim) ? lim : v;
    }

    // value * resolution / full в uint32: value<=1000, res<=65536 -> <=65.5e6,
    // переполнения нет. Предварительное деление теряло младшие разряды.
    static uint32_t scaleDuty(uint32_t value, uint32_t full) {
        return (value * resolution()) / full;
    }

    static PwmMode s_mode;
    static uint8_t s_cs;
    static bool s_owned;
};

template <typename Tr> PwmMode Engine<Tr>::s_mode = PwmMode::Fast;
template <typename Tr> uint8_t Engine<Tr>::s_cs = 0;
template <typename Tr> bool Engine<Tr>::s_owned = false;

}  // namespace pwm
}  // namespace fn

#endif  // F_USE_PWM

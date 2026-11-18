#pragma once
// ============================================================================
//  PwmTimer0.h — ШИМ на Timer0: D6 (канал A), D5 (канал B)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Traits и алиас движка для Timer0. Таймер принадлежит ядру Arduino
//    (millis/micros/delay), поэтому ШИМ работает в режиме совместимости:
//    меняются ТОЛЬКО COM-биты и OCR0A/OCR0B. Делитель и TOP держит ядро.
//
//  API
//    using Pwm0 = fn::pwm::Engine<fn::pwm::Traits0>
//    begin(PwmMode::Fast | PhaseCorrect) -> Result
//      *Top вернёт Unsupported: смена TOP ломает millis().
//    setFrequency(hz) -> 0        частоту держит ядро
//    frequency()                  ~976 Гц (Fast) / ~490 Гц (PhaseCorrect)
//    attachA / attachB / detachA / detachB
//    setDutyA / setDutyB / setDutyPercent* / setDutyPermille*
//    resolution() = 256
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - begin(Fast) ставит WGM=011 — тот режим, что использует ядро. millis()
//      работает корректно. begin(PhaseCorrect) ставит WGM=001: частота ШИМ
//      падает вдвое, millis() остаётся верным (TOP и делитель те же).
//    - Разрешение 8 бит. Максимальная скважность 255/256 = 99.6%: значение
//      256 не помещается в OCR. Для гарантированного HIGH — GPIO.
//    - D5 совпадает с T1 (внешний тактовый вход Timer1). Если Timer1
//      тактируется от T1, D5 под ШИМ использовать нельзя.
//    - analogWrite(5,x) / analogWrite(6,x) после attach перезапишут COM и OCR.
//    - Направление вывода задаёт вызывающий: regs::modeOutput(boardPin(5)).
//
//  ЗАЯВКА РЕСУРСОВ (обязательна в прикладном коде)
//    Движок шаблонный, FN_CLAIM_* внутри него стоять не может. Заявку
//    ставит вызывающий на уровне файла:
//
//        FN_CLAIM_TIMER(0, FN_OWNER_ARDUINO);
//        FN_CLAIM_VECTOR(Timer0Ovf, FN_OWNER_ARDUINO);
//
//    Владелец — ARDUINO, а не PWM: Pwm0 НЕ захватывает таймер. Заявка
//    FN_CLAIM_TIMER(0, FN_OWNER_PWM) здесь ошибочна — она разрешила бы
//    смену TOP и сломала millis().
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта. Flash: ~180 байт при полном использовании.
//
//  ПРИМЕР
//    FN_CLAIM_TIMER(0, FN_OWNER_ARDUINO);
//    fn::Pwm0::begin(fn::PwmMode::Fast);
//    fn::regs::modeOutput(fn::boardPin(5));
//    fn::Pwm0::attachB(fn::PwmOut::Normal);
//    fn::Pwm0::setDutyPercentB(30);
//
//  ЗАВИСИМОСТИ: timer/PwmCommon.h, timer/Pwm.h, core/*
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "Pwm.h"
#include "PwmCommon.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_PWM

namespace fn {
namespace pwm {

struct Traits0 {
    using value_type = uint8_t;

    static constexpr uint8_t bits = 8;
    static constexpr bool hasIcr = false;
    static constexpr bool lockedByCore = true;
    static constexpr uint8_t prescaleCount = 5;
		static constexpr uint8_t timerIndex = 0;

    // У Timer0 биты 7:6 TCCR0B — FOC0A/FOC0B (write-only, читаются 0),
    // бит 3 — WGM02, биты 5:4 — reserved. Сохранять нечего: WGM перезаписываем.
    static constexpr uint8_t tccrBKeep = 0x00;
    // WGM02 живёт в бите 3 TCCR0B; упакован в бите 2 значения wgmBits.
    static constexpr uint8_t wgmBMask = 0x04u;

    static volatile uint8_t& tccrA() { return TCCR0A; }
    static volatile uint8_t& tccrB() { return TCCR0B; }

    // WGM02..00 упакованы: [3:2] -> TCCR0B (только бит 2 значим), [1:0] -> A.
    static uint8_t wgmBits(PwmMode m) {
        switch (m) {
            case PwmMode::Fast:            return 0x03u;  // 011
            case PwmMode::PhaseCorrect:    return 0x01u;  // 001
            case PwmMode::FastTop:         return 0x07u;  // 011 + WGM02 (запрещён)
            default:                       return 0x05u;  // 001 + WGM02 (запрещён)
        }
    }

    static uint16_t prescaleAt(uint8_t i) {
        return (i == 0) ? 1u : (i == 1) ? 8u : (i == 2) ? 64u : (i == 3) ? 256u : 1024u;
    }
    static uint8_t clockBits(uint16_t d) {
        return (d == 1) ? 1u : (d == 8) ? 2u : (d == 64) ? 3u : (d == 256) ? 4u : 5u;
    }
    static uint16_t prescaleOf(uint8_t cs) {
        return (cs == 1) ? 1u : (cs == 2) ? 8u : (cs == 3) ? 64u
             : (cs == 4) ? 256u : (cs == 5) ? 1024u : 0u;
    }

    static void writeTop(uint32_t v) { OCR0A = uint8_t(v); }
    static uint32_t readTop() { return OCR0A; }
    static void writeOcrA(uint32_t v) { OCR0A = uint8_t(v); }
    static void writeOcrB(uint32_t v) { OCR0B = uint8_t(v); }
    static uint32_t readOcrA() { return OCR0A; }
    static uint32_t readOcrB() { return OCR0B; }
};

}  // namespace pwm

using Pwm0 = pwm::Engine<pwm::Traits0>;

}  // namespace fn

#endif  // F_USE_PWM

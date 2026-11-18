#pragma once
// ============================================================================
//  PwmTimer2.h — ШИМ на Timer2: D11 (канал A), D3 (канал B)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Traits и алиас движка для Timer2. Не связан с millis(), поэтому режимы
//    *Top и произвольная частота доступны. TOP = OCR2A, следовательно в
//    *Top-режимах канал A (D11) недоступен как выход — только канал B (D3).
//
//  API
//    using Pwm2 = fn::pwm::Engine<fn::pwm::Traits2>
//    begin(PwmMode) -> Result
//    setFrequency(hz) -> uint32_t
//    setTop(n) -> Result                n = 1..256
//    resolution() / top() / frequency()
//    attachA / attachB / detachA / detachB
//    setDutyA / setDutyB / setDutyPercent* / setDutyPermille*
//
//  ДЕЛИТЕЛИ TIMER2 ОТЛИЧАЮТСЯ
//    1, 8, 32, 64, 128, 256, 1024 — семь значений против пяти у Timer0/1.
//    Коды CS2x трактуются иначе: 3 = 32 (не 64), 5 = 128 (не ExtFall).
//    Прежняя версия использовала общий переключатель с bool isT2 и для
//    Timer0 при Div32 возвращала код 3 = Div64 — тихая ошибка частоты.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - В *Top TOP = OCR2A: attachA и setDutyA вернут Unsupported. Нужны оба
//      канала с произвольной частотой — берите Timer1 (TOP = ICR1).
//    - Минимум частоты: 256 * 1024 / F_CPU = 16.4 мс -> ~61 Гц. Ниже — Timer1.
//    - D3 совпадает с INT1: при ШИМ на D3 внешнее прерывание INT1 недоступно.
//    - D11 совпадает с MOSI: при активном SPI ШИМ на D11 использовать нельзя.
//    - Асинхронный режим (AS2) не поддерживается: TOSC1/TOSC2 на Nano заняты
//      основным кварцем (PB6/PB7).
//    - Максимальная скважность 255/256 при TOP=255; при TOP<255 значение
//      TOP+1 проходит и 100% работает.
//    - analogWrite(3,x) / analogWrite(11,x) применять нельзя.
//
//  ЗАЯВКА РЕСУРСОВ (обязательна в прикладном коде)
//        FN_CLAIM_TIMER(2, FN_OWNER_PWM);
//        FN_CLAIM_VECTOR(Timer2Ovf, FN_OWNER_NONE);
//    В core/Resources.h: FN_TIMER2_DEVICE = FN_OWNER_PWM.
//    ШИМ не использует прерывания Timer2; заявка фиксирует отсутствие
//    чужого ISR.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта. Flash: ~220 байт при полном использовании.
//
//  ПРИМЕР
//    FN_CLAIM_TIMER(2, FN_OWNER_PWM);
//    fn::Pwm2::begin(fn::PwmMode::FastTop);
//    fn::Pwm2::setFrequency(2000);
//    fn::regs::modeOutput(fn::boardPin(3));
//    fn::Pwm2::attachB(fn::PwmOut::Normal);
//    fn::Pwm2::setDutyPercentB(50);
//
//  ЗАВИСИМОСТИ: timer/PwmCommon.h, timer/Pwm.h, core/*
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "Pwm.h"
#include "PwmCommon.h"

#include <stdint.h>
#include <avr/io.h>

#if F_USE_PWM

namespace fn {
namespace pwm {

struct Traits2 {
    using value_type = uint8_t;

    static constexpr uint8_t bits = 8;
    static constexpr bool hasIcr = false;
    static constexpr bool lockedByCore = false;
    static constexpr uint8_t prescaleCount = 7;
    static constexpr uint8_t timerIndex = 2;

    // TCCR2B: биты 7:6 FOC2A/FOC2B (write-only), бит 4 reserved,
    // бит 3 WGM22 — перезаписываем. Сохранять нечего.
    // WGM22 живёт в бите 3 TCCR2B; упакован в бите 2 значения wgmBits.
    static constexpr uint8_t wgmBMask = 0x04u;

    static volatile uint8_t& tccrA() { return TCCR2A; }
    static volatile uint8_t& tccrB() { return TCCR2B; }

    // WGM22..20 упакованы: [3:2] -> TCCR2B (значим бит 2), [1:0] -> TCCR2A.
    static uint8_t wgmBits(PwmMode m) {
        switch (m) {
            case PwmMode::Fast:            return 0x03u;  // 011
            case PwmMode::PhaseCorrect:    return 0x01u;  // 001
            case PwmMode::FastTop:         return 0x07u;  // 111
            default:                       return 0x05u;  // 101
        }
    }

    static uint16_t prescaleAt(uint8_t i) {
        return (i == 0) ? 1u : (i == 1) ? 8u : (i == 2) ? 32u : (i == 3) ? 64u
             : (i == 4) ? 128u : (i == 5) ? 256u : 1024u;
    }
    static uint8_t clockBits(uint16_t d) {
        return (d == 1) ? 1u : (d == 8) ? 2u : (d == 32) ? 3u : (d == 64) ? 4u
             : (d == 128) ? 5u : (d == 256) ? 6u : 7u;
    }
    static uint16_t prescaleOf(uint8_t cs) {
        return (cs == 1) ? 1u : (cs == 2) ? 8u : (cs == 3) ? 32u : (cs == 4) ? 64u
             : (cs == 5) ? 128u : (cs == 6) ? 256u : (cs == 7) ? 1024u : 0u;
    }

    static void writeTop(uint32_t v) { OCR2A = uint8_t(v); }
    static uint32_t readTop() { return OCR2A; }
    static void writeOcrA(uint32_t v) { OCR2A = uint8_t(v); }
    static void writeOcrB(uint32_t v) { OCR2B = uint8_t(v); }
    static uint32_t readOcrA() { return OCR2A; }
    static uint32_t readOcrB() { return OCR2B; }
};

}  // namespace pwm

using Pwm2 = pwm::Engine<pwm::Traits2>;

}  // namespace fn

#endif  // F_USE_PWM

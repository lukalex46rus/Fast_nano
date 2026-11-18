#pragma once
// ============================================================================
//  PwmTimer1.h — ШИМ на Timer1: D9 (канал A), D10 (канал B)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Traits и алиас движка для 16-битного Timer1. TOP = ICR1 в *Top-режимах,
//    поэтому ОБА канала доступны при произвольной частоте. Основной выбор
//    для моторов, сервосигналов и точного ШИМ.
//
//  API
//    using Pwm1 = fn::pwm::Engine<fn::pwm::Traits1>
//    begin(PwmMode) -> Result
//    setFrequency(hz) -> uint32_t
//    setTop(n) -> Result                разрешение n градаций (1..65536)
//    resolution() / top() / frequency()
//    attachA / attachB / detachA / detachB
//    setDutyA / setDutyB / setDutyPercent* / setDutyPermille*
//
//  РЕЖИМЫ И РАЗРЯДНОСТЬ
//    Fast            WGM=0111, TOP=0x01FF (9 бит, 512 градаций)
//    PhaseCorrect    WGM=0011, TOP=0x01FF (9 бит, двойной наклон)
//    FastTop         WGM=1110, TOP=ICR1  (до 16 бит)
//    PhaseCorrectTop WGM=1010, TOP=ICR1  (до 16 бит)
//    Прежняя версия подписывала Fast как 10-битный — фактически 9.
//    Нужно ровно 10 бит: begin(FastTop) + setTop(1024).
//
//  ДИАПАЗОН ЧАСТОТ (16 МГц, FastTop)
//    20 кГц  -> TOP+1 = 100 при Div8,  разрешение 100
//    1 кГц   -> TOP+1 = 2000 при Div8, разрешение 2000
//    50 Гц   -> TOP+1 = 40000 при Div8, разрешение 40000
//    минимум ~0.24 Гц (TOP=65536, Div1024)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - begin() захватывает таймер через tmr::acquire(N, Client::Pwm). Если его
//      держит Pulse/Servo/Stepper, вернёт Busy и ничего не изменит. end()
//      освобождает захват. Реестр advisory: драйверы Timer0/1/2 владение не
//      заявляют, поэтому прямой вызов Timer1::takeover мимо acquire конфликт
//      не поймает — потребитель обязан захватывать.
//    - Timer1 делится с InputCapture (pin/Pulse), Servo, StepperDriver.
//      Одновременно работает ровно один потребитель.
//    - ICR1 буферизуется: запись применяется на следующем BOTTOM/TOP.
//      Смена частоты на работающем ШИМ даёт один искажённый период.
//    - Разрешение и частота связаны: f = F_CPU / (div * resolution).
//      16 бит разрешения на 20 кГц невозможны.
//    - 100% скважность работает при TOP < 65535: значение TOP+1 помещается
//      в uint16_t. При TOP = 65535 максимум 65535/65536.
//    - Все 16-битные обращения атомарны (старший байт через TEMP).
//    - analogWrite(9,x) / analogWrite(10,x) применять нельзя.
//
//  ЗАЯВКА РЕСУРСОВ (обязательна в прикладном коде)
//        FN_CLAIM_TIMER(1, FN_OWNER_PWM);
//        FN_CLAIM_VECTOR(Timer1Ovf, FN_OWNER_NONE);
//    В core/Resources.h согласовать:
//        #define FN_TIMER1_DEVICE    FN_OWNER_PWM
//        #define FN_TIMER1_OVF_OWNER FN_OWNER_NONE
//    Иначе static_assert упадёт с именем вектора и кодом владельца.
//    ШИМ не использует прерывания Timer1: заявка на вектор фиксирует, что
//    его никто не занимает. Если ваш модуль вешает ISR — заявляйте свой код.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта. Flash: ~320 байт при полном использовании.
//
//  ПРИМЕР
//    FN_CLAIM_TIMER(1, FN_OWNER_PWM);
//    fn::Pwm1::begin(fn::PwmMode::FastTop);
//    fn::Pwm1::setFrequency(20000);
//    fn::regs::modeOutput(fn::boardPin(9));
//    fn::Pwm1::attachA(fn::PwmOut::Normal);
//    fn::Pwm1::setDutyPermilleA(250);      // 25.0%
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

struct Traits1 {
    using value_type = uint16_t;

    static constexpr uint8_t bits = 16;
    static constexpr bool hasIcr = true;
    static constexpr bool lockedByCore = false;
    static constexpr uint8_t prescaleCount = 5;
    static constexpr uint8_t timerIndex = 1;

    // TCCR1B: бит 7 ICNC1, бит 6 ICES1, бит 5 reserved — сохраняем.
    // Биты 4:3 WGM13/WGM12 перезаписываем.
    static constexpr uint8_t tccrBKeep = 0xE0;
		
		// WGM13:12 живут в битах 4:3 TCCR1B; упакованы в битах 3:2 значения wgmBits.
    static constexpr uint8_t wgmBMask = 0x0Cu;

    static volatile uint8_t& tccrA() { return TCCR1A; }
    static volatile uint8_t& tccrB() { return TCCR1B; }

    // WGM13..10 упакованы: [3:2] -> TCCR1B (WGM13,WGM12), [1:0] -> TCCR1A.
    static uint8_t wgmBits(PwmMode m) {
        switch (m) {
            case PwmMode::Fast:            return 0x07u;  // 0111: FastPWM 9-bit
            case PwmMode::PhaseCorrect:    return 0x03u;  // 0011: PC 9-bit
            case PwmMode::FastTop:         return 0x0Eu;  // 1110: Fast, TOP=ICR1
            default:                       return 0x0Au;  // 1010: PC, TOP=ICR1
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

    static void writeTop(uint32_t v) { ICR1 = uint16_t(v); }
    static uint32_t readTop() { return ICR1; }
    static void writeOcrA(uint32_t v) { OCR1A = uint16_t(v); }
    static void writeOcrB(uint32_t v) { OCR1B = uint16_t(v); }
    static uint32_t readOcrA() { return OCR1A; }
    static uint32_t readOcrB() { return OCR1B; }
};

}  // namespace pwm

using Pwm1 = pwm::Engine<pwm::Traits1>;

}  // namespace fn

#endif  // F_USE_PWM

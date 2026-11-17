#pragma once
// ============================================================================
//  PwmImpl.h — umbrella ШИМ-слоя
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Одно подключение всего ШИМ. Прежняя версия была заглушкой со ссылкой
//    на несуществующий Fpwm_common.h.
//
//  API
//    fn::Pwm0   Timer0: D6 (A), D5 (B)   — частота фиксирована ядром Arduino
//    fn::Pwm1   Timer1: D9 (A), D10 (B)  — 16 бит, произвольная частота
//    fn::Pwm2   Timer2: D11 (A), D3 (B)  — 8 бит, произвольная частота
//    Типы: fn::PwmMode, fn::PwmOut, fn::PwmChannel
//    Карта: fn::channelPin / channelTimer / channelIsA
//
//  ОГРАНИЧЕНИЯ
//    - Слой ЗАМЕЩАЕТ analogWrite() на D3/D5/D6/D9/D10/D11. Смешивать нельзя.
//    - Направление выводов задаёт вызывающий через fn::regs::modeOutput.
//    - Timer1 конфликтует с Pulse/Servo/Stepper: арбитраж через Resources.h.
//    - Один таймер = одна частота на оба канала.
//    - Заявки FN_CLAIM_* обязательны в прикладном коде, см. шапки таймеров.
//
//  ПРИМЕР
//    #include "timer/PwmImpl.h"
//    FN_CLAIM_TIMER(1, FN_OWNER_PWM);
//    fn::Pwm1::begin(fn::PwmMode::FastTop);
//    fn::Pwm1::setFrequency(1000);
//    fn::regs::modeOutput(fn::boardPin(10));
//    fn::Pwm1::attachB(fn::PwmOut::Normal);
//    fn::Pwm1::setDutyPercentB(75);
//
//  ЗАВИСИМОСТИ: Pwm.h, PwmCommon.h, PwmTimer0.h, PwmTimer1.h, PwmTimer2.h
// ============================================================================

#include "Pwm.h"
#include "PwmCommon.h"
#include "PwmTimer0.h"
#include "PwmTimer1.h"
#include "PwmTimer2.h"

#pragma once
// ============================================================================
//  TimerCommon.h — общий словарь таймерного слоя
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единые enum'ы и хелперы для Timer0/1/2 и всех PWM-модулей. Ни от одного
//    таймера не зависит: наоборот, каждый таймерный модуль включает этот файл.
//    Прежняя версия включала Timer0.h ради Timer0::Out — перевёрнутая
//    зависимость, из-за которой любой PWM тянул весь Timer0.
//
//  API (namespace fn::tmr)
//    enum class Clock : uint8_t
//      Stopped, Div1, Div8, Div64, Div256, Div1024, ExtFall, ExtRise
//      Значения совпадают с полем CSn2..CSn0 в TCCRnB — пишутся напрямую.
//    enum class Out : uint8_t
//      Disable, Toggle, Normal, Inverted
//      Значения НЕ совпадают с COMn1..COMn0: порядок изменён так, чтобы
//      Toggle == 1 (как в железе). Преобразуется функцией comBits().
//    enum class Channel : uint8_t { A = 0, B = 1 }
//    enum class Waveform : uint8_t
//      Normal, PhaseCorrect, Ctc, FastPwm         базовые режимы
//      TopOcrA / TopIcr1 добавляются модулями через wgmBits()
//
//    comBits(Out) -> uint8_t          код COM для записи в TCCRnA
//    clockBits(Clock) -> uint8_t      код CS (тождественно, но с проверкой)
//    prescaleOf(Clock) -> uint16_t    делитель 1/8/64/256/1024; 0 для Stopped/Ext
//    bestClock(uint32_t cycles, uint32_t maxCount, Clock& out) -> uint32_t
//                                   подбор минимального делителя, при котором
//                                   cycles укладывается в maxCount; возвращает
//                                   число тактов таймера, 0 если не влезает
//    ticksToUs(uint32_t ticks, Clock c) -> uint32_t
//    usToTicks(uint32_t us, Clock c) -> uint32_t
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Все функции constexpr или inline, таблиц в RAM нет.
//    - bestClock работает в uint32: максимум 4.29e9 тактов = 268 с при 16 МГц.
//      Для больших интервалов считайте в тиках вручную.
//    - ticksToUs делит на константу (F_CPU/1e6 = 16 или 8) — GCC сводит к
//      сдвигу. Никакого 64-битного деления и soft-float.
//    - ExtFall/ExtRise доступны только Timer0 (T0=PD4) и Timer1 (T1=PD5).
//      У Timer2 внешнего тактового входа НЕТ: clockBits вернёт код, но
//      железо его проигнорирует. Проверяйте на уровне модуля.
//    - Значения Out намеренно отличаются от прежнего Timer0::Out
//      (было Disable/Normal/Inverted/Toggle). Legacy-алиасы даны ниже.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт flash и RAM при неиспользовании. bestClock — цикл по 5
//    делителям, ~60 байт flash, вызывается только при настройке периода.
//
//  ПРИМЕР
//    fn::tmr::Clock c;
//    uint32_t ticks = fn::tmr::bestClock(16000, 256, c);   // 1000 мкс при 16 МГц
//    // ticks = 250, c = Div64
//    OCR0A = uint8_t(ticks - 1);
//    TCCR0B = (TCCR0B & 0xF8) | fn::tmr::clockBits(c);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Compiler.h, core/Types.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Compiler.h"
#include "../core/Types.h"

#include <stdint.h>

namespace fn {
namespace tmr {

// ---------------------------- тактирование ---------------------------------
// Значения = поле CSn2..CSn0 регистра TCCRnB.
enum class Clock : uint8_t {
    Stopped  = 0,
    Div1     = 1,
    Div8     = 2,
    Div64    = 3,
    Div256   = 4,
    Div1024  = 5,
    ExtFall  = 6,   ///< внешний вход T0 (PD4) / T1 (PD5), спад
    ExtRise  = 7    ///< внешний вход T0 (PD4) / T1 (PD5), фронт
};

// ---------------------------- выходной каскад ------------------------------
// Значения = поле COMnx1..COMnx0 для не-PWM и PWM-режимов (non-inverting).
enum class Out : uint8_t {
    Disable  = 0,
    Toggle   = 1,   ///< только non-PWM; в FastPWM Toggle означает WGM-зависимый режим
    Normal   = 2,   ///< non-inverting: сброс на compare, установка на BOTTOM
    Inverted = 3
};

enum class Channel : uint8_t { A = 0, B = 1 };

enum class Waveform : uint8_t {
    Normal       = 0,   ///< TOP = MAX
    PhaseCorrect = 1,   ///< TOP = MAX, двойной наклон
    Ctc          = 2,   ///< TOP = OCRA, сброс на compare
    FastPwm      = 3    ///< TOP = MAX, одинарный наклон
};

// ---------------------------- преобразования -------------------------------
inline constexpr uint8_t clockBits(Clock c) { return uint8_t(c) & 0x07u; }

inline constexpr uint8_t comBits(Out m) { return uint8_t(m) & 0x03u; }

// Делитель; 0 для Stopped и внешних источников (период не определён).
inline constexpr uint16_t prescaleOf(Clock c) {
    return (c == Clock::Div1)    ? 1u
         : (c == Clock::Div8)    ? 8u
         : (c == Clock::Div64)   ? 64u
         : (c == Clock::Div256)  ? 256u
         : (c == Clock::Div1024) ? 1024u
                                 : 0u;
}

// Тактов на микросекунду: 16 при 16 МГц, 8 при 8 МГц.
constexpr uint32_t TicksPerUs = F_CPU / 1000000UL;

// Подбор минимального делителя, при котором cycles влезает в maxCount.
// Возвращает число тактов таймера (1..maxCount), 0 — не влезает ни при каком.
// out получает найденный делитель; при неудаче — Div1024.
inline uint32_t bestClock(uint32_t cycles, uint32_t maxCount, Clock& out) {
    static const Clock kClocks[5] = {
        Clock::Div1, Clock::Div8, Clock::Div64, Clock::Div256, Clock::Div1024
    };
    for (uint8_t i = 0; i < 5; ++i) {
        const uint16_t d = prescaleOf(kClocks[i]);
        const uint32_t t = cycles / d;
        if (t >= 1 && t <= maxCount) {
            out = kClocks[i];
            return t;
        }
    }
    out = Clock::Div1024;
    return 0;
}

// Тики -> мкс с учётом делителя. Деление на константу, без 64 бит.
inline uint32_t ticksToUs(uint32_t ticks, Clock c) {
    const uint16_t d = prescaleOf(c);
    if (d == 0) return 0;
    // (ticks * d) / TicksPerUs. Умножаем в uint32: при d<=1024 и разумных
    // ticks переполнения нет; для очень длинных периодов делите заранее.
    if (ticks > (0xFFFFFFFFUL / d)) {
        return (ticks / TicksPerUs) * d;   // грубый путь без переполнения
    }
    return (ticks * d) / TicksPerUs;
}

inline uint32_t usToTicks(uint32_t us, Clock c) {
    const uint16_t d = prescaleOf(c);
    if (d == 0) return 0;
    return (us * TicksPerUs) / d;
}

// ---------------------------- legacy-алиасы --------------------------------
// Прежний порядок Timer0::Out был Disable/Normal/Inverted/Toggle.
// Старый код, писавший Out::Normal, получит Normal и здесь — совпадает.
// Out::Toggle сменил числовое значение (было 3, стало 1), но имя то же,
// поэтому исходники не требуют правки.
using OutMode = Out;

}  // namespace tmr
}  // namespace fn

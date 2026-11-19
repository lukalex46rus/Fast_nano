#pragma once
// ============================================================================
//  Tick.h — база времени и неблокирующие таймауты
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Время берётся у ядра Arduino (Timer0, millis/micros/delay). Модуль НЕ
//    трогает регистры Timer0 — это зафиксировано заявкой FN_CLAIM_TIMER(0,
//    FN_OWNER_ARDUINO). Добавлено то, чего в ядре нет: корректные таймауты
//    с учётом переполнения счётчика и точная микросекундная задержка.
//
//  API (namespace fn)
//    millis() / micros() -> uint32_t          как в Arduino, разрешение 1 мс / 4 мкс
//    delayMs(ms) / delayUs(us)                блокирующие, как в Arduino
//    delayUsPrecise(us)                       точная задержка, прерывания запрещены
//
//    elapsed(since, now) -> uint32_t          now - since с учётом wrap-around
//    expired(since, timeoutMs) -> bool        истёк ли интервал (мс)
//    expiredUs(since, timeoutUs) -> bool      то же в мкс
//
//    class Timeout                            неблокирующий таймер
//      Timeout() / Timeout(uint32_t ms)
//      start(ms) / restart()
//      expired() -> bool
//      remaining() -> uint32_t                0 если истёк
//      elapsedMs() -> uint32_t
//
//    class Stopwatch                          замер интервалов
//      start() / stop()
//      running() -> bool
//      elapsedUs() -> uint32_t
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - millis() переполняется через 49.7 суток, micros() — через 71.6 минут.
//      ВСЕ сравнения времени делайте через expired()/elapsed(), а не через
//      `millis() > deadline`: последнее ломается на переполнении.
//    - Разрешение micros() при 16 МГц — 4 мкс (Timer0 tick = 1024 мкс / 256).
//      Для точности до такта используйте InputCapture (pin/Pulse.h).
//    - delayUsPrecise запрещает прерывания на всё время задержки. При us=1000
//      это 1 мс без обслуживания ISR: UART может потерять байт. Держите
//      задержки короткими (< 100 мкс) либо используйте обычный delayUs.
//    - delayUsPrecise(us=0) возвращается сразу (в отличие от _delay_loop_2(0),
//      который делает 65536 итераций).
//    - Если Timer0 захвачен другим модулем (fn::Timer0::begin), millis/micros
//      перестают работать. Заявка FN_CLAIM_TIMER ловит это на компиляции.
//    - sizeof(Timeout) == 8, sizeof(Stopwatch) == 8. Объекты не создают
//      прерываний и не занимают таймеров. У Stopwatch флаг работы упакован
//      в старший бит аккумулятора: диапазон накопления 2^31 мкс ≈ 35.8 мин.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM без объектов. millis/micros — вызов ядра (~20 байт).
//    delayUsPrecise — ~40 байт flash, без 64-битной арифметики.
//
//  ПРИМЕР
//    // Неблокирующее мигание без delay
//    fn::Timeout blink(500);
//    void loop() {
//        if (blink.expired()) {
//            fn::BoardPin<13>::toggle();
//            blink.restart();
//        }
//    }
//
//    // Таймаут ожидания ответа
//    uint32_t t0 = fn::millis();
//    while (!ready()) {
//        if (fn::expired(t0, 100)) return fn::Result::Timeout;
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               Arduino.h; реализация — Tick.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"

#include <stdint.h>
#include <Arduino.h>

namespace fn {

// ---------------------------- база времени ---------------------------------
inline uint32_t millis() { return ::millis(); }
inline uint32_t micros() { return ::micros(); }
inline void delayMs(uint32_t ms) { ::delay(ms); }
inline void delayUs(uint16_t us) { ::delayMicroseconds(us); }

// Точная задержка с запрещёнными прерываниями. Накладные расходы учтены.
void delayUsPrecise(uint16_t us);

// ---------------------------- арифметика времени ---------------------------
// Корректно при переполнении счётчика (беззнаковое вычитание по модулю 2^32).
inline constexpr uint32_t elapsed(uint32_t since, uint32_t now) {
    return uint32_t(now - since);
}

inline bool expired(uint32_t since, uint32_t timeoutMs) {
    return elapsed(since, ::millis()) >= timeoutMs;
}

inline bool expiredUs(uint32_t since, uint32_t timeoutUs) {
    return elapsed(since, ::micros()) >= timeoutUs;
}

// ---------------------------- неблокирующий таймер -------------------------
class Timeout {
public:
    Timeout() : _start(0), _period(0) {}
    explicit Timeout(uint32_t ms) : _start(::millis()), _period(ms) {}

    void start(uint32_t ms) {
        _start = ::millis();
        _period = ms;
    }
    void restart() { _start = ::millis(); }

    bool expired() const { return elapsed(_start, ::millis()) >= _period; }

    uint32_t elapsedMs() const { return elapsed(_start, ::millis()); }

    uint32_t remaining() const {
        const uint32_t e = elapsedMs();
        return (e >= _period) ? 0u : uint32_t(_period - e);
    }

private:
    uint32_t _start;
    uint32_t _period;
};

// ---------------------------- секундомер -----------------------------------
// sizeof == 8: флаг работы упакован в старший бит аккумулятора (G-1).
// Диапазон накопления — 2^31 мкс ≈ 35.8 мин; при превышении wraps, как micros().
class Stopwatch {
public:
    Stopwatch() : _start(0), _acc(0) {}

    void start() {
        if (running()) return;
        _start = ::micros();
        _acc = uint32_t(_acc | RunBit);
    }

    void stop() {
        if (!running()) return;
        _acc = uint32_t((_acc & AccMask) + elapsed(_start, ::micros()));
    }

    void reset() { _acc = 0; }

    bool running() const { return (_acc & RunBit) != 0; }

    uint32_t elapsedUs() const {
        const uint32_t a = uint32_t(_acc & AccMask);
        return running() ? uint32_t(a + elapsed(_start, ::micros())) : a;
    }

private:
    static constexpr uint32_t RunBit = 0x80000000UL;
    static constexpr uint32_t AccMask = 0x7FFFFFFFUL;

    uint32_t _start;
    uint32_t _acc;
    
};  // namespace fn

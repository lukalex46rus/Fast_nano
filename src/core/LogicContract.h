#pragma once
// ============================================================================
//  LogicContract.h — контракты слоя logic: задачи, фильтры, время
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Фиксирует форму API для Debounce/Button/Encoder/Filter/Pid/Task/Looper/
//    StateMachine, чтобы восемь модулей не дали восемь несовместимых
//    планировщиков и фильтров. Виртуальных функций нет: vtable на AVR уезжает
//    в RAM, полиморфизм здесь статический (шаблоны).
//    Плюс общие примитивы периодичности, которые иначе копировались бы
//    в каждом модуле.
//
//  КОНТРАКТ ЗАДАЧИ (Task, Looper, StateMachine, Sequencer)
//    void   tick()            один шаг, неблокирующий, целевое время < 100 мкс
//    bool   active() const    нужна ли работа сейчас
//    Result begin(...)        настройка; Ok / BadArg / Busy / NoBuffer
//    void   end()             освобождение своих ресурсов
//    Задача НЕ знает про планировщик и НЕ занимает таймер. Время берётся
//    из timer/Tick, сравнения — через fn::expired()/elapsed().
//
//  КОНТРАКТ ФИЛЬТРА (Filter, Smooth, Hysteresis, Kalman, Pid)
//    T    update(T input)     один шаг, возвращает новое состояние
//    T    value() const       текущее состояние без пересчёта
//    void reset(T v = T{})    сброс состояния
//    Тип T — либо float, либо fn::fx::Fix (Q16.16). Целочисленный путь
//    по умолчанию: soft-float стоит ~600 Б flash на операцию.
//    Фильтр не хранит время: dt задаёт вызывающий (фиксированный шаг).
//
//  API (namespace fn::logic)
//    class Periodic                   интервал в миллисекундах
//      Periodic() / Periodic(uint32_t ms)
//      start(ms) / restart() / stop()
//      due() -> bool                  истёк интервал (не сбрасывает)
//      next() -> bool                 due() + автоматический restart
//      elapsedMs() / remainingMs() -> uint32_t
//      period() -> uint32_t
//      running() -> bool
//
//    class Rate                       то же, но задаётся частотой в Гц
//      Rate(uint16_t hz)
//      setRate(hz) / hz() -> uint16_t
//      далее как Periodic
//
//    class StopwatchUs                замер длительности в мкс
//      start() / stop() / reset()
//      running() -> bool
//      elapsedUs() -> uint32_t
//
//    every(ms, f) -> bool             вызвать f(), если интервал истёк;
//                                     состояние хранится в статике вызова
//    template <typename Pred> waitUntil(pred, timeoutMs) -> Result
//                                     переэкспорт bus::waitUntil для logic
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Все сравнения времени через elapsed(): прямое millis() > deadline
//      ломается на переполнении через 49.7 суток (micros — 71.6 минут).
//    - Periodic::next() сдвигает точку отсчёта на ТЕКУЩЕЕ время, а не на
//      «прошлое + период». При опоздании обработки интервал не накапливается
//      (нет catch-up), зато нет и пачки вызовов подряд после задержки.
//      Нужен строгий ритм — считайте точку сами: last += period.
//    - every() использует static-переменную внутри шаблона: один вызов
//      в одном месте кода = один таймер. Два вызова в разных местах —
//      разные таймеры. Вызов в цикле по массиву объектов даст ОДИН общий
//      таймер на всех: там нужен Periodic в каждом объекте.
//    - Periodic не занимает таймеров и прерываний: sizeof == 8 байт
//      (start 4 + period 4). Rate — 8 байт. StopwatchUs — 9 байт.
//    - Источник времени фиксирован по назначению модуля:
//        Debounce / Button / Task / Looper — millis() (разрешение 1 мс);
//        Encoder / Quadrature — прерывания, время не используется;
//        Filter / Pid / Ramp — dt задаёт вызывающий, внутри времени нет.
//      Модуль не выбирает источник сам и не зовёт delay().
//    - tick() обязан быть неблокирующим. Любой delay/wait внутри задачи
//      останавливает весь планировщик. Для ожидания события — состояние
//      в StateMachine, а не цикл ожидания.
//    - Коллбэки задач вызываются из основного кода, не из ISR. Если задача
//      регистрируется в диспетчере прерываний — это отдельный контракт
//      (pin/Irq), и там действуют ограничения ISR.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM без объектов. Periodic/Rate — по 8 байт на экземпляр,
//    StopwatchUs — 9. Flash: ~120 байт на Periodic при полном использовании.
//    Никаких таблиц и динамической памяти.
//
//  ПРИМЕР
//    // Задача по контракту
//    class Blink {
//    public:
//        fn::Result begin(uint8_t pin, uint32_t ms) {
//            _pin = fn::boardPin(pin);
//            fn::regs::modeOutput(_pin);
//            _period.start(ms);
//            _on = true;
//            return fn::Result::Ok;
//        }
//        void end() { _on = false; }
//        bool active() const { return _on; }
//        void tick() {
//            if (_period.next()) fn::regs::toggle(_pin);
//        }
//    private:
//        fn::Pin _pin{};
//        fn::logic::Periodic _period;
//        bool _on = false;
//    };
//
//    // Разовое действие без объекта
//    if (fn::logic::every(1000, []{ Serial.println(fn::millis()); })) {}
//
//    // Фильтр по контракту, целочисленный
//    class Ema {
//    public:
//        fn::fx::Fix update(fn::fx::Fix x) {
//            _y = fn::fx::addSat(_y, fn::fx::mul(_k, fn::fx::subSat(x, _y)));
//            return _y;
//        }
//        fn::fx::Fix value() const { return _y; }
//        void reset(fn::fx::Fix v = 0) { _y = v; }
//    private:
//        fn::fx::Fix _y = 0;
//        fn::fx::Fix _k = fn::fx::fromInt(1) / 8;
//    };
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, timer/Tick.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "../timer/Tick.h"

#include <stdint.h>

namespace fn {
namespace logic {

// ============================ периодичность ================================
class Periodic {
public:
    Periodic() : _start(0), _period(0) {}
    explicit Periodic(uint32_t ms) : _start(::fn::millis()), _period(ms) {}

    void start(uint32_t ms) {
        _period = ms;
        _start = ::fn::millis();
    }
    void restart() { _start = ::fn::millis(); }
    void stop() { _period = 0; }

    bool running() const { return _period != 0; }
    uint32_t period() const { return _period; }

    // Истёк ли интервал. Точку отсчёта НЕ сдвигает.
    bool due() const {
        return _period != 0 && elapsed(_start, ::fn::millis()) >= _period;
    }

    // due() + сдвиг точки отсчёта на текущее время (без catch-up).
    bool next() {
        if (!due()) return false;
        _start = ::fn::millis();
        return true;
    }

    uint32_t elapsedMs() const { return elapsed(_start, ::fn::millis()); }

    uint32_t remainingMs() const {
        if (_period == 0) return 0;
        const uint32_t e = elapsedMs();
        return (e >= _period) ? 0u : uint32_t(_period - e);
    }

private:
    uint32_t _start;
    uint32_t _period;
};

// ============================ частота ======================================
class Rate {
public:
    Rate() : _p(), _hz(0) {}
    explicit Rate(uint16_t hz) : _p(), _hz(0) { setRate(hz); }

    void setRate(uint16_t hz) {
        _hz = hz;
        if (hz == 0) { _p.stop(); return; }
        uint32_t ms = 1000UL / hz;
        if (ms == 0) ms = 1;              // быстрее 1 кГц millis не различает
        _p.start(ms);
    }

    uint16_t hz() const { return _hz; }
    bool running() const { return _p.running(); }
    void restart() { _p.restart(); }
    void stop() { _p.stop(); _hz = 0; }
    bool due() const { return _p.due(); }
    bool next() { return _p.next(); }
    uint32_t elapsedMs() const { return _p.elapsedMs(); }
    uint32_t remainingMs() const { return _p.remainingMs(); }

private:
    Periodic _p;
    uint16_t _hz;
};

// ============================ секундомер ===================================
class StopwatchUs {
public:
    StopwatchUs() : _start(0), _acc(0), _run(false) {}

    void start() {
        if (_run) return;
        _start = ::fn::micros();
        _run = true;
    }

    void stop() {
        if (!_run) return;
        _acc = uint32_t(_acc + elapsed(_start, ::fn::micros()));
        _run = false;
    }

    void reset() { _acc = 0; _run = false; }
    bool running() const { return _run; }

    uint32_t elapsedUs() const {
        return _run ? uint32_t(_acc + elapsed(_start, ::fn::micros())) : _acc;
    }

private:
    uint32_t _start;
    uint32_t _acc;
    bool _run;
};

// ============================ разовые хелперы ==============================
// Один вызов в одном месте кода = один таймер (static внутри шаблона).
// В цикле по массиву объектов даст общий таймер — там нужен Periodic в объекте.
template <typename F>
inline bool every(uint32_t ms, F f) {
    static Periodic p;
    if (!p.running()) p.start(ms);
    if (!p.next()) return false;
    f();
    return true;
}

template <typename Pred>
inline Result waitUntil(Pred pred, uint32_t timeoutMs) {
    const uint32_t t0 = ::fn::millis();
    for (;;) {
        if (pred()) return Result::Ok;
        if (elapsed(t0, ::fn::millis()) >= timeoutMs) return Result::Timeout;
    }
}

}  // namespace logic
}  // namespace fn
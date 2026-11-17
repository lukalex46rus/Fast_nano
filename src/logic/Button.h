#pragma once
// Кнопка: расширенный автомат событий (нажатие, клики, удержание,
// импульсное удержание, таймаут) + простые методы для старых скетчей.
//
// Время внутри — младшие 16 бит millis(), поэтому все интервалы
// ограничены ~65 секундами. Это сделано специально: на AVR экономит RAM.
//
// Пример:
//   fn::logic::Button btn(2);          // кнопка на GND, INPUT_PULLUP
//   void loop() {
//       if (btn.tick()) {
//           if (btn.click()) Serial.println("click");
//           if (btn.step())  Serial.println("step");
//       }
//       if (btn.timeout(2000)) Serial.println("timeout");
//   }

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

#include "../pin/PinRef.h"
#include "../pin/Pin.h"
#include "../timer/Tick.h"
#include "Debounce.h"

// Настройки по умолчанию. Можно переопределить до #include <Fast_nano.h>.
#ifndef F_BTN_DEBOUNCE_MS
#define F_BTN_DEBOUNCE_MS 20
#endif
#ifndef F_BTN_HOLD_MS
#define F_BTN_HOLD_MS 600
#endif
#ifndef F_BTN_STEP_MS
#define F_BTN_STEP_MS 400
#endif
#ifndef F_BTN_STEP_PERIOD_MS
#define F_BTN_STEP_PERIOD_MS 200
#endif
#ifndef F_BTN_CLICK_WAIT_MS
#define F_BTN_CLICK_WAIT_MS 500
#endif

namespace fn {
namespace logic {

// Чистая логика кнопки без привязки к пину.
// Используется напрямую клавиатурами (MatrixKey/AnalogKey) и виртуальными кнопками.
class ButtonCore {
   public:
    enum State : uint8_t {
        Idle,          // простаивает [состояние]
        Press,         // нажатие [событие]
        Click,         // клик (отпущено до удержания) [событие]
        WaitHold,      // ожидание удержания [состояние]
        Hold,          // удержание [событие]
        ReleaseHold,   // отпущено до импульсов [событие]
        WaitStep,      // ожидание импульсов [состояние]
        Step,          // импульс [событие]
        WaitNextStep,  // ожидание следующего импульса [состояние]
        ReleaseStep,   // отпущено после импульсов [событие]
        Release,       // отпущено (в любом случае) [событие]
        WaitClicks,    // ожидание кликов [состояние]
        Clicks,        // зафиксированы клики [событие]
        WaitTimeout,   // ожидание таймаута [состояние]
        Timeout,       // таймаут [событие]
        SkipEvents,    // пропускает события до отпускания [состояние]
    };

    static const uint8_t MAX_CLICKS = 15;
    static const uint8_t MAX_STEPS = 127;

    ButtonCore(uint16_t debounceMs = F_BTN_DEBOUNCE_MS,
               uint16_t holdMs = F_BTN_HOLD_MS,
               uint16_t stepMs = F_BTN_STEP_MS,
               uint16_t stepPeriodMs = F_BTN_STEP_PERIOD_MS,
               uint16_t clickWaitMs = F_BTN_CLICK_WAIT_MS)
        : _debounce(debounceMs),
          _holdMs(holdMs),
          _stepMs(stepMs),
          _stepPeriodMs(stepPeriodMs),
          _clickWaitMs(clickWaitMs) {}

    // ================= настройка =================

    void setTimings(uint16_t debounceMs, uint16_t holdMs, uint16_t stepMs,
                    uint16_t stepPeriodMs, uint16_t clickWaitMs) {
        _debounce.setWindow(debounceMs);
        _holdMs = holdMs;
        _stepMs = stepMs;
        _stepPeriodMs = stepPeriodMs;
        _clickWaitMs = clickWaitMs;
    }

    void setDebounce(uint16_t ms) { _debounce.setWindow(ms); }
    void setHoldTime(uint16_t ms) { _holdMs = ms; }
    void setStepTime(uint16_t ms) { _stepMs = ms; }
    void setStepPeriod(uint16_t ms) { _stepPeriodMs = ms; }
    void setClickWait(uint16_t ms) { _clickWaitMs = ms; }

    // ================= управление =================

    // Сбросить состояние (принудительно закончить обработку).
    void reset() {
        _state = State::Idle;
        _clicks = 0;
        _steps = 0;
        _debounce.reset(false);
    }

    // Игнорировать все события до отпускания кнопки.
    void skipEvents() {
        if (pressing()) _state = State::SkipEvents;
    }

    // ================= события =================

    bool press() const { return _state == State::Press; }
    bool press(uint8_t clicks) const { return _clicks == clicks && press(); }

    bool click() const { return _state == State::Click; }
    bool click(uint8_t clicks) const { return _clicks == clicks && click(); }

    bool hold() const { return _state == State::Hold; }
    bool hold(uint8_t clicks) const { return _clicks == clicks && hold(); }

    bool releaseHold() const { return _state == State::ReleaseHold; }
    bool releaseHold(uint8_t clicks) const { return _clicks == clicks && releaseHold(); }

    bool step() const { return _state == State::Step; }
    bool step(uint8_t clicks) const { return _clicks == clicks && step(); }

    bool releaseStep() const { return _state == State::ReleaseStep; }
    bool releaseStep(uint8_t clicks) const { return _clicks == clicks && releaseStep(); }

    bool releaseHoldStep() const {
        return _state == State::ReleaseStep || _state == State::ReleaseHold;
    }
    bool releaseHoldStep(uint8_t clicks) const {
        return _clicks == clicks && releaseHoldStep();
    }

    bool release() const { return _state == State::Release; }
    bool release(uint8_t clicks) const { return _clicks == clicks && release(); }

    bool hasClicks() const { return _state == State::Clicks; }
    bool hasClicks(uint8_t clicks) const { return _clicks == clicks && hasClicks(); }

    bool timeout() const { return _state == State::Timeout; }

    // Вызывать вне if (tick()) — переводит WaitTimeout в Timeout по времени.
    bool timeout(uint16_t ms) {
        if (_state == State::WaitTimeout && elapsed() >= ms) {
            _state = State::Timeout;
            return true;
        }
        return false;
    }

    // ================= состояния =================

    bool pressing() const {
        switch (_state) {
            case State::Press:
            case State::WaitHold:
            case State::Hold:
            case State::WaitStep:
            case State::Step:
            case State::WaitNextStep:
            case State::SkipEvents:
                return true;
            default:
                return false;
        }
    }
    bool pressing(uint8_t clicks) const { return _clicks == clicks && pressing(); }

    bool holding() const {
        switch (_state) {
            case State::Hold:
            case State::WaitStep:
            case State::Step:
            case State::WaitNextStep:
                return true;
            default:
                return false;
        }
    }
    bool holding(uint8_t clicks) const { return _clicks == clicks && holding(); }

    bool stepping() const {
        switch (_state) {
            case State::Step:
            case State::WaitNextStep:
                return true;
            default:
                return false;
        }
    }
    bool stepping(uint8_t clicks) const { return _clicks == clicks && stepping(); }

    bool waiting() const { return _state == State::WaitClicks; }
    bool busy() const { return _state != State::Idle; }

    // ================= время =================

    uint16_t pressFor() const {
        switch (_state) {
            case State::WaitHold:
                return elapsed();
            case State::Hold:
            case State::WaitStep:
            case State::Step:
            case State::WaitNextStep:
                return uint16_t(_holdMs + holdFor());
            default:
                return 0;
        }
    }
    bool pressFor(uint16_t ms) const { return pressFor() >= ms; }

    uint16_t holdFor() const {
        switch (_state) {
            case State::WaitStep:
                return elapsed();
            case State::Step:
            case State::WaitNextStep:
                return uint16_t(_stepMs + stepFor());
            default:
                return 0;
        }
    }
    bool holdFor(uint16_t ms) const { return holdFor() >= ms; }

    uint16_t stepFor() const {
        switch (_state) {
            case State::Step:
            case State::WaitNextStep:
                return uint16_t(uint16_t(_steps) * _stepPeriodMs + elapsed());
            default:
                return 0;
        }
    }
    bool stepFor(uint16_t ms) const { return stepFor() >= ms; }

    // ================= значения =================

    State state() const { return State(_state); }
    State getState() const { return state(); }
    uint8_t getClicks() const { return _clicks; }
    uint8_t getSteps() const { return _steps; }

    // ================= совместимость =================
    // Старый простой API fn::logic::Button.

    bool pressed() const { return pressing(); }
    bool released() const { return !pressing(); }
    bool pressedEvent() { return press(); }
    bool releasedEvent() { return release(); }
    bool clicked() { return click(); }
    uint32_t heldMs() const { return pressFor(); }

    // ================= обработка =================

    // Кнопка нажата в прерывании.
    void pressISR() {
        _debounce.force(true);
    }

    // Обработка с антидребезгом. pressed — кнопка физически нажата.
    // Вернёт true при смене состояния.
    bool poll(bool pressed) {
        _debounce.update(pressed);
        return pollRaw(_debounce.state());
    }

    // Обработка без антидребезга. Вернёт true при смене состояния.
    bool pollRaw(bool pressed) {
        State pstate = State(_state);

        switch (_state) {
            case State::Idle:
                if (pressed) _state = State::Press;
                break;

            case State::Press:
                _state = State::WaitHold;
                _resetTime();
                break;

            case State::WaitHold:
                if (!pressed) {
                    _state = State::Click;
                    if (_clicks < MAX_CLICKS) ++_clicks;
                } else if (elapsed() >= _holdMs) {
                    _state = State::Hold;
                    _resetTime();
                }
                break;

            case State::Hold:
                _state = State::WaitStep;
                break;

            case State::WaitStep:
                if (!pressed) {
                    _state = State::ReleaseHold;
                } else if (elapsed() >= _stepMs) {
                    _state = State::Step;
                    _resetTime();
                }
                break;

            case State::Step:
                _state = State::WaitNextStep;
                break;

            case State::WaitNextStep:
                if (!pressed) {
                    _state = State::ReleaseStep;
                } else if (elapsed() >= _stepPeriodMs) {
                    _state = State::Step;
                    if (_steps < MAX_STEPS) ++_steps;
                    _resetTime();
                }
                break;

            case State::SkipEvents:
                if (!pressed) _state = State::Release;
                break;

            case State::ReleaseHold:
            case State::ReleaseStep:
                _state = State::Release;
                _clicks = 0;
                break;

            case State::Click:
                _state = State::Release;
                break;

            case State::Release:
                _steps = 0;
                _state = _clicks ? State::WaitClicks : State::WaitTimeout;
                _resetTime();
                break;

            case State::WaitClicks:
                if (pressed) {
                    _state = State::Press;
                } else if (elapsed() >= _clickWaitMs) {
                    _state = State::Clicks;
                    _resetTime();
                }
                break;

            case State::Clicks:
                _clicks = 0;
                _state = State::WaitTimeout;
                break;

            case State::WaitTimeout:
                if (pressed) _state = State::Press;
                break;

            case State::Timeout:
                _state = State::Idle;
                break;
        }

        return pstate != State(_state);
    }

   protected:
    // Принудительно перейти к ожиданию таймаута (используется EncoderButton).
    void skipToTimeout() {
        _resetTime();
        _state = State::WaitTimeout;
    }

    // Принудительно игнорировать события до отпускания.
    void skipToRelease() { _state = State::SkipEvents; }

    uint16_t elapsed() const { return uint16_t(uint16_t(millis()) - _tmr); }
    void _resetTime() { _tmr = uint16_t(millis()); }

   private:
    DebounceValue<bool> _debounce;
    uint16_t _tmr = 0;
    uint16_t _holdMs;
    uint16_t _stepMs;
    uint16_t _stepPeriodMs;
    uint16_t _clickWaitMs;

    uint8_t _clicks = 0;
    uint8_t _steps = 0;
    uint8_t _state = State::Idle;
};

// Кнопка на runtime-пине (PinRef).
// activeLow=true — кнопка замыкает на GND, пин INPUT_PULLUP.
class Button : public ButtonCore {
   public:
    Button(uint8_t pin, bool activeLow = true,
           uint16_t debounceMs = F_BTN_DEBOUNCE_MS,
           uint16_t holdMs = F_BTN_HOLD_MS,
           uint16_t stepMs = F_BTN_STEP_MS,
           uint16_t stepPeriodMs = F_BTN_STEP_PERIOD_MS,
           uint16_t clickWaitMs = F_BTN_CLICK_WAIT_MS)
        : ButtonCore(debounceMs, holdMs, stepMs, stepPeriodMs, clickWaitMs),
          _pin(pin, activeLow ? INPUT_PULLUP : INPUT),
          _activeLow(activeLow) {}

    // Вызывать в loop. Вернёт true при смене состояния.
    bool tick() { return poll(readRaw()); }

    // Опрос без антидребезга. Вернёт true при смене состояния.
    bool tickRaw() { return pollRaw(readRaw()); }

    // Прочитать текущее состояние кнопки (уже с учётом activeLow).
    bool readButton() const { return readRaw(); }

   private:
    bool readRaw() const {
        bool v = _pin.read();
        return _activeLow ? !v : v;
    }

    PinRef _pin;
    bool _activeLow;
};

// Две кнопки, нажатые одновременно, как одна «мультикнопка».
// Пока обе удерживаются, события самих кнопок подавляются, а эта
// кнопка проходит обычный автомат (нажатие, удержание, отпускание).
// Повторное срабатывание — только после отпускания обеих.
class ButtonMulti : public ButtonCore {
   public:
    ButtonMulti(uint16_t debounceMs = F_BTN_DEBOUNCE_MS,
                uint16_t holdMs = F_BTN_HOLD_MS,
                uint16_t stepMs = F_BTN_STEP_MS,
                uint16_t stepPeriodMs = F_BTN_STEP_PERIOD_MS,
                uint16_t clickWaitMs = F_BTN_CLICK_WAIT_MS)
        : ButtonCore(debounceMs, holdMs, stepMs, stepPeriodMs, clickWaitMs) {}

    // Вызывать в loop() вместо tick() обеих кнопок.
    bool tick(ButtonCore& b0, ButtonCore& b1) {
        if (_active) {
            if (!b0.pressing() && !b1.pressing()) _active = false;
            if (!b0.pressing()) b0.reset();
            if (!b1.pressing()) b1.reset();
            return pollRaw(true);
        }
        if (b0.pressing() && b1.pressing()) {
            b0.skipEvents();
            b1.skipEvents();
            _active = true;
        }
        return pollRaw(false);
    }

    void reset() {
        ButtonCore::reset();
        _active = false;
    }

   private:
    // Прямой poll() обходил бы логику мультикнопки — скрываем.
    using ButtonCore::poll;
    using ButtonCore::pollRaw;

    bool _active = false;
};

// Кнопка на compile-time пине: не хранит PinRef в объекте.
template <uint8_t P, bool ActiveLow = true>
class ButtonPin : public ButtonCore {
   public:
    ButtonPin(uint16_t debounceMs = F_BTN_DEBOUNCE_MS,
              uint16_t holdMs = F_BTN_HOLD_MS,
              uint16_t stepMs = F_BTN_STEP_MS,
              uint16_t stepPeriodMs = F_BTN_STEP_PERIOD_MS,
              uint16_t clickWaitMs = F_BTN_CLICK_WAIT_MS)
        : ButtonCore(debounceMs, holdMs, stepMs, stepPeriodMs, clickWaitMs) {
        if (ActiveLow) Pin<P>::inputPullup();
        else Pin<P>::input();
    }

    bool tick() { return poll(readRaw()); }
    bool tickRaw() { return pollRaw(readRaw()); }
    bool readButton() const { return readRaw(); }

   private:
    static bool readRaw() {
        bool v = Pin<P>::read();
        return ActiveLow ? !v : v;
    }
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC

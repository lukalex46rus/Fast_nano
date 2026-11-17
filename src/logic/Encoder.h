#pragma once
// Энкодер: направление, быстрый поворот, поворот при нажатой кнопке,
// абсолютная позиция и накопленная дельта. Плюс ISR-вариант с буфером
// на 4 события и комбинированный энкодер+кнопка.
//
// Пример:
//   fn::logic::Encoder enc(3, 4);       // A/B, INPUT_PULLUP
//   void loop() {
//       if (enc.tick()) {
//           if (enc.left())  Serial.println("left");
//           if (enc.right()) Serial.println("right");
//       }
//   }

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

#include "../core/Atomic.h"
#include "../pin/PinRef.h"
#include "../pin/Pin.h"
#include "../timer/Tick.h"
#include "Button.h"

#ifndef F_ENC_FAST_MS
#define F_ENC_FAST_MS 32
#endif

namespace fn {
namespace logic {

// Чистая логика энкодера без привязки к пинам.
class EncoderCore {
   public:
    enum class EncoderMode : uint8_t {
        Step4Low,   // активный низкий, полный период (4 фазы) за щелчок
        Step4High,  // активный высокий, полный период (4 фазы) за щелчок
        Step2,      // половина периода (2 фазы) за щелчок
        Step1,      // четверть периода (1 фаза) за щелчок
    };

    enum class State : uint8_t {
        Idle = 0,
        Left,
        LeftFast,
        LeftHold,
        LeftHoldFast,
        Right,
        RightFast,
        RightHold,
        RightHoldFast,
    };

    EncoderCore() = default;

    // ================= настройка =================

    void setType(EncoderMode type) { _type = type; }
    void setReverse(bool rev) { _rev = rev; }
    void setFastTime(uint16_t ms) { _fastMs = ms; }

    // Начальное состояние линий.
    void initEnc(bool e0, bool e1) {
        _e0 = e0;
        _e1 = e1;
        _phase = 0;
        _state = State::Idle;
        _lastTurn = uint16_t(uint16_t(millis()) - _fastMs);
    }

    void reset(int32_t position = 0) {
        _position = position;
        _delta = 0;
        _phase = 0;
        _state = State::Idle;
    }

    // ================= состояние =================

    State state() const { return _state; }
    State getState() const { return state(); }

    // Был поворот в этом опросе [событие].
    bool turn() const { return _state != State::Idle; }

    // Направление: +1 / -1 [состояние].
    int8_t dir() const { return dirBool() ? 1 : -1; }
    bool dirBool() const {
        switch (_state) {
            case State::Right:
            case State::RightFast:
            case State::RightHold:
            case State::RightHoldFast:
                return true;
            default:
                return false;
        }
    }

    // Нажатый поворот [событие].
    bool turnH() const {
        switch (_state) {
            case State::RightHold:
            case State::RightHoldFast:
            case State::LeftHold:
            case State::LeftHoldFast:
                return true;
            default:
                return false;
        }
    }

    // Быстрый поворот [состояние].
    bool fast() const {
        switch (_state) {
            case State::RightFast:
            case State::RightHoldFast:
            case State::LeftFast:
            case State::LeftHoldFast:
                return true;
            default:
                return false;
        }
    }

    bool right() const {
        return _state == State::Right || _state == State::RightFast;
    }
    bool left() const {
        return _state == State::Left || _state == State::LeftFast;
    }
    bool rightH() const {
        return _state == State::RightHold || _state == State::RightHoldFast;
    }
    bool leftH() const {
        return _state == State::LeftHold || _state == State::LeftHoldFast;
    }

    // Кнопка энкодера нажата во время поворота [состояние].
    bool encHolding() const { return turnH(); }

    // ================= позиция =================

    int32_t position() const { return _position; }

    // Накопленная дельта с прошлого вызова. Сбрасывается.
    int8_t delta() {
        int8_t d = _delta;
        _delta = 0;
        return d;
    }

    // ================= опрос =================

    bool poll(bool e0, bool e1, bool pressed = false) {
        _state = pollRaw(e0, e1, pressed);
        return _state != State::Idle;
    }

    State pollRaw(bool e0, bool e1, bool pressed = false) {
        if (!(_e0 ^ _e1 ^ e0 ^ e1)) return State::Idle;

        (_e1 ^ e0) ? ++_phase : --_phase;
        _e0 = e0;
        _e1 = e1;
        if (!_phase) return State::Idle;

        switch (_type) {
            case EncoderMode::Step4Low:
                if (!(e0 & e1)) return State::Idle;
                break;
            case EncoderMode::Step4High:
                if (e0 | e1) return State::Idle;
                break;
            case EncoderMode::Step2:
                if (e0 ^ e1) return State::Idle;
                break;
            case EncoderMode::Step1:
                break;
        }

        bool neg = (_phase < 0) ^ _rev;
        _phase = 0;

        uint16_t now = uint16_t(millis());
        bool isFast = uint16_t(now - _lastTurn) < _fastMs;
        _lastTurn = now;

        int8_t step = neg ? -1 : 1;
        _position += step;

        int16_t d = int16_t(_delta) + step;
        if (d > 127) d = 127;
        else if (d < -127) d = -127;
        _delta = int8_t(d);

        if (neg) {
            return pressed ? (isFast ? State::RightHoldFast : State::RightHold)
                           : (isFast ? State::RightFast : State::Right);
        }
        return pressed ? (isFast ? State::LeftHoldFast : State::LeftHold)
                       : (isFast ? State::LeftFast : State::Left);
    }

   protected:
    int8_t _phase = 0;
    int8_t _rev = 0;
    EncoderMode _type = EncoderMode::Step4Low;
    State _state = State::Idle;
    uint16_t _lastTurn = 0;
    uint16_t _fastMs = F_ENC_FAST_MS;
    int32_t _position = 0;
    int8_t _delta = 0;
    bool _e0 = false;
    bool _e1 = false;
};

// Энкодер на runtime-пинах.
class Encoder : public EncoderCore {
   public:
    Encoder(uint8_t pinA, uint8_t pinB, uint8_t mode = INPUT_PULLUP)
        : _a(pinA, mode), _b(pinB, mode) {
        initEnc(_a.read(), _b.read());
    }

    // Вызывать в loop. pressed — кнопка энкодера нажата.
    bool tick(bool pressed = false) {
        return EncoderCore::poll(_a.read(), _b.read(), pressed);
    }

   private:
    PinRef _a, _b;
};

// Энкодер на compile-time пинах.
template <uint8_t A, uint8_t B>
class EncoderPin : public EncoderCore {
   public:
    EncoderPin(uint8_t mode = INPUT_PULLUP) {
        if (mode == INPUT_PULLUP) {
            Pin<A>::inputPullup();
            Pin<B>::inputPullup();
        } else {
            Pin<A>::input();
            Pin<B>::input();
        }
        initEnc(Pin<A>::read(), Pin<B>::read());
    }

    bool tick(bool pressed = false) {
        return EncoderCore::poll(Pin<A>::read(), Pin<B>::read(), pressed);
    }
};

// Энкодер с опросом в прерывании. Буфер — 4 события.
class InterruptEncoder : public EncoderCore {
   public:
    InterruptEncoder(uint8_t pinA, uint8_t pinB, uint8_t mode = INPUT_PULLUP)
        : _a(pinA, mode), _b(pinB, mode) {
        initEnc(_a.read(), _b.read());
    }

    // Вызывать в loop: забирает одно событие из буфера.
    bool tick() {
        uint8_t st;
        {
            AtomicBlock lock;
            st = uint8_t(_buf & 0x0F);
            _buf >>= 4;
        }
        _state = st ? State(st) : State::Idle;
        return st != 0;
    }

    // Вызывать из ISR по CHANGE на любом из пинов A/B.
    void tickISR(bool pressed = false) {
        State st = pollRaw(_a.read(), _b.read(), pressed);
        if (st == State::Idle) return;

        uint16_t v = uint16_t(st);
        for (uint8_t i = 0; i < 4; i++) {
            uint8_t shift = uint8_t(i << 2);
            if (!(_buf & (0x0Fu << shift))) {
                _buf |= uint16_t(v << shift);
                break;
            }
        }
    }

    // Атомарное чтение позиции/дельты: их меняет ISR.
    int32_t position() {
        AtomicBlock lock;
        return _position;
    }

    int8_t delta() {
        AtomicBlock lock;
        int8_t d = _delta;
        _delta = 0;
        return d;
    }

   private:
    PinRef _a, _b;
    volatile uint16_t _buf = 0;
};

// Виртуальный энкодер+кнопка для внешнего источника сигналов.
class EncoderButtonCore : public ButtonCore, public EncoderCore {
   public:
    bool poll(bool e0, bool e1, bool btn) {
        if (EncoderCore::poll(e0, e1, ButtonCore::pressing())) {
            if (ButtonCore::pressing()) ButtonCore::skipToRelease();
            else ButtonCore::skipToTimeout();
            return true;
        }
        return ButtonCore::poll(btn);
    }

    void reset() {
        ButtonCore::reset();
        EncoderCore::reset(0);
    }

    ButtonCore::State buttonState() const { return ButtonCore::state(); }
    EncoderCore::State encoderState() const { return EncoderCore::state(); }
    ButtonCore::State getState() const { return ButtonCore::getState(); }
};

// Энкодер и кнопка на общих пинах.
class EncoderButton : public Button, public Encoder {
   public:
    EncoderButton(uint8_t encA, uint8_t encB, uint8_t btn,
              uint8_t modeEnc = INPUT_PULLUP, uint8_t modeBtn = INPUT_PULLUP)
        : Button(btn, modeBtn == INPUT_PULLUP),
          Encoder(encA, encB, modeEnc) {}

    // Вызывать в loop. Вернёт true при событии энкодера или кнопки.
    bool tick() {
        if (Encoder::tick(Button::pressing())) {
            if (Button::pressing()) Button::skipToRelease();
            else Button::skipToTimeout();
            return true;
        }
        return Button::tick();
    }

    void reset() {
        Button::reset();
        Encoder::reset(0);
    }

    ButtonCore::State buttonState() const { return Button::state(); }
    EncoderCore::State encoderState() const { return Encoder::state(); }

    ButtonCore::State getState() const { return Button::getState(); }

    using Button::poll;
    using Button::pollRaw;
    using Encoder::poll;
    using Encoder::pollRaw;
};

// То же, но энкодер опрашивается в прерывании.
class EncoderButtonInterrupt : public Button, public InterruptEncoder {
   public:
    EncoderButtonInterrupt(uint8_t encA, uint8_t encB, uint8_t btn,
                 uint8_t modeEnc = INPUT_PULLUP, uint8_t modeBtn = INPUT_PULLUP)
        : Button(btn, modeBtn == INPUT_PULLUP),
          InterruptEncoder(encA, encB, modeEnc) {}

    bool tick() {
        if (InterruptEncoder::tick()) {
            if (Button::pressing()) Button::skipToRelease();
            else Button::skipToTimeout();
            return true;
        }
        return Button::tick();
    }

    void tickISR() { InterruptEncoder::tickISR(Button::pressing()); }

    void reset() {
        Button::reset();
        InterruptEncoder::reset(0);
    }

    ButtonCore::State buttonState() const { return Button::state(); }
    EncoderCore::State encoderState() const { return InterruptEncoder::state(); }

    ButtonCore::State getState() const { return Button::getState(); }

    using Button::poll;
    using Button::pollRaw;
    using InterruptEncoder::poll;
    using InterruptEncoder::pollRaw;
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC

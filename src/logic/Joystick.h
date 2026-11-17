#pragma once
// Аналоговый джойстик: мёртвая зона, калибровка нуля, инверсия и
// «экспонента» (линейная / квадратичная / кубическая).
// Вход 0..1023, выход -255..255.
//
// Пример:
//   fn::logic::Joystick joy(A0);
//   joy.calibrate();
//   joy.deadzone(30);
//   joy.curve(fn::logic::JoyCurve::Cubic);
//   void loop() {
//       if (joy.tick()) Serial.println(joy.value());
//   }

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

#include <Arduino.h>

#include "../timer/Tick.h"

#ifndef F_JOY_PERIOD_MS
#define F_JOY_PERIOD_MS 10
#endif

namespace fn {
namespace logic {

enum class JoyCurve : uint8_t {
    Linear,
    Square,
    Cubic,
};

// Чистая логика без привязки к пину: compute(adc).
class JoystickCore {
   public:
    // Инверсия направления.
    void invert(bool inv) { _inv = inv; }

    // Период внутреннего опроса, мс (0 — каждый вызов).
    void setPeriod(uint8_t ms) { _period = ms; }

    // Калибровка нуля внешним значением.
    void calibrate(uint16_t value) {
        _zero = value > 1023 ? 1023 : value;
    }

    // Мёртвая зона: снаружи значение пересчитывается линейно от её краёв.
    void deadzone(uint8_t width) { _dead = uint8_t(width / 2); }

    void curve(JoyCurve c) { _mode = c; }

    uint16_t zero() const { return _zero; }
    int16_t value() const { return _val; }

    // Тикер по внутреннему таймеру. true — пора опрашивать.
    bool tick() {
        uint16_t now = uint16_t(millis());
        if (uint16_t(now - _tmr) >= _period) {
            _tmr = now;
            return true;
        }
        return false;
    }

    // Ручной расчёт по значению АЦП.
    int16_t compute(uint16_t adc) {
        int32_t v;
        int32_t zero = _zero;
        int32_t dead = _dead;

        if (int32_t(adc) + dead < zero) {
            int32_t denom = zero - dead;
            if (denom <= 0) denom = 1;
            v = (int32_t(adc) - zero + dead) * 255 / denom;
            if (v < -255) v = -255;
        } else if (int32_t(adc) > zero + dead) {
            int32_t denom = 1023 - zero - dead;
            if (denom <= 0) denom = 1;
            v = (int32_t(adc) - zero - dead) * 255 / denom;
            if (v > 255) v = 255;
        } else {
            v = 0;
        }

        if (_inv) v = -v;

        if (_mode != JoyCurve::Linear) {
            int32_t n = v < 0 ? -v : v;
            if (_mode == JoyCurve::Square) {
                n = ((n + 1) * n) >> 8;
            } else {
                n = ((n + 1) * (n + 1) * n) >> 16;
            }
            v = v < 0 ? -n : n;
        }

        _val = int16_t(v);
        return _val;
    }

   private:
    uint16_t _zero = 512;
    int16_t _val = 0;
    uint16_t _tmr = 0;
    JoyCurve _mode = JoyCurve::Linear;
    uint8_t _dead = 0;
    uint8_t _period = F_JOY_PERIOD_MS;
    bool _inv = false;
};

// Джойстик на пине АЦП.
class Joystick : public JoystickCore {
   public:
    explicit Joystick(uint8_t pin) : _pin(pin) {}

    void setPin(uint8_t pin) { _pin = pin; }
    uint8_t pin() const { return _pin; }

    // Прочитать сырое значение с пина.
    uint16_t read() const { return uint16_t(analogRead(_pin)); }

    // Калибровать ноль по текущему значению.
    void calibrate() { JoystickCore::calibrate(read()); }

    // Прочитать пин и пересчитать значение.
    int16_t compute() { return JoystickCore::compute(read()); }

    // Пересчитать по внешнему значению АЦП.
    int16_t compute(uint16_t adc) { return JoystickCore::compute(adc); }

    // Тикер: опрашивает пин по своему периоду.
    bool tick() {
        if (JoystickCore::tick()) {
            compute();
            return true;
        }
        return false;
    }

   private:
    uint8_t _pin;
};

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC

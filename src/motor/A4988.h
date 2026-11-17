#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_STEPPER

#include "StepperDriver.h"
#include "../pin/Pin.h"
#include "../pin/PinRef.h"
#include "../timer/Tick.h"

namespace fn {

// A4988: STEP/DIR драйвер с микрошагом до 1/16.
// Пины задаются в begin(). Если пины compile-time — используйте шаблон A4988Pins<P...>,
// если runtime — обычный A4988.
class A4988 : public StepperDriver {
   public:
    // Шаг микрошага. Значения соответствуют MS1/MS2/MS3.
    enum class Microstep : uint8_t {
        Full = 0,   // MS1=0 MS2=0 MS3=0
        Half,       // 1/2: MS1=1 MS2=0 MS3=0
        Quarter,    // 1/4: MS1=0 MS2=1 MS3=0
        Eighth,     // 1/8: MS1=1 MS2=1 MS3=0
        Sixteenth,  // 1/16: MS1=1 MS2=1 MS3=1
    };

    A4988() = default;

    // Базовая версия без параметров не годится для A4988 (нужны пины).
    // Используйте begin(stepPin, dirPin, ...).
    bool begin() override { return false; }

    // stepPin, dirPin — обязательны. enablePin, ms1..ms3 — опционально (-1 = не подключён).
    // Если MS-пины не подключены, микрошаг задаётся перемычками на модуле.
    bool begin(uint8_t stepPin, uint8_t dirPin,
               int8_t enablePin = -1,
               int8_t ms1 = -1, int8_t ms2 = -1, int8_t ms3 = -1);

    bool step(bool forward) override;
    void enable(bool on) override;

    // Установить микрошаг (если MS-пины подключены в begin()).
    void setMicrostep(Microstep m);

    // Минимальная длительность импульса STEP (мкс). По умолчанию 2 мкс
    // (даташит требует >= 1 мкс, берём с запасом).
    void setPulseWidthUs(uint8_t us) { _pulseUs = us ? us : 1; }

    // Задержка после enable(true) для стабилизации charge pump (A4988 требует ~1 мс).
    void setEnableDelayUs(uint16_t us) { _enableDelayUs = us; }

   private:
    void _stepPin(bool high) { _step.write(high); }

    PinRef _step;
    PinRef _dirPin;
    PinRef _en;
    PinRef _ms1, _ms2, _ms3;

    bool _hasEn = false;
    bool _hasMs[3] = { false, false, false };
    uint8_t _pulseUs = 2;
    uint16_t _enableDelayUs = 1000;
    Microstep _microstep = Microstep::Full;
};

// Compile-time версия: все пины — параметры шаблона.
// Быстрее, потому что Pin<P> генерирует SBI/CBI без поиска по регистрам.
template <uint8_t STEP, uint8_t DIR, int8_t EN = -1,
          int8_t MS1 = -1, int8_t MS2 = -1, int8_t MS3 = -1>
class A4988Pins : public StepperDriver {
   public:
    bool begin() override {
        Pin<STEP>::output(); Pin<STEP>::low();
        Pin<DIR>::output();  Pin<DIR>::low();
        detail::OptionalPin<EN>::output();
        detail::OptionalPin<EN>::high();   // disabled
        detail::OptionalPin<MS1>::output();
        detail::OptionalPin<MS2>::output();
        detail::OptionalPin<MS3>::output();
        _enabled = false;
        return true;
    }

    bool step(bool forward) override {
        if (!_enabled) return false;
        _dir = forward;
        Pin<DIR>::write(forward);
        Pin<STEP>::high();
        if (_pulseUs) delayUs(_pulseUs);
        Pin<STEP>::low();
        if (_pulseUs) delayUs(_pulseUs);
        _pos += forward ? 1 : -1;
        return true;
    }

    void enable(bool on) override {
        detail::OptionalPin<EN>::write(!on);   // active-low
        if (EN >= 0 && on) delayUs(_enableDelayUs);
        _enabled = on;
    }

    void setMicrostep(A4988::Microstep m) {
        detail::OptionalPin<MS1>::write(m == A4988::Microstep::Half || m == A4988::Microstep::Eighth || m == A4988::Microstep::Sixteenth);
        detail::OptionalPin<MS2>::write(m == A4988::Microstep::Quarter || m == A4988::Microstep::Eighth || m == A4988::Microstep::Sixteenth);
        detail::OptionalPin<MS3>::write(m == A4988::Microstep::Sixteenth);
    }

    void setPulseWidthUs(uint8_t us) { _pulseUs = us ? us : 1; }
    void setEnableDelayUs(uint16_t us) { _enableDelayUs = us; }

   private:
    uint8_t _pulseUs = 2;
    uint16_t _enableDelayUs = 1000;
};

}  // namespace fn

#endif  // F_USE_STEPPER

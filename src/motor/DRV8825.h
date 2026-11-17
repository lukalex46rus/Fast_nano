#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_STEPPER && F_USE_DRV8825

#include "StepperDriver.h"
#include "../pin/Pin.h"
#include "../pin/PinRef.h"
#include "../timer/Tick.h"

namespace fn {

// DRV8825: STEP/DIR-драйвер с микрошагом до 1/32.
// Пины задаются в begin(). Для известных на этапе компиляции пинов
// используйте DRV8825Pins<...>, для runtime-пинов — DRV8825.
class DRV8825 : public StepperDriver {
   public:
    // Режим микрошага. Значения соответствуют MODE0 / MODE1 / MODE2.
    enum class Microstep : uint8_t {
        Full = 0,       // 1/1
        Half,           // 1/2
        Quarter,        // 1/4
        Eighth,         // 1/8
        Sixteenth,      // 1/16
        ThirtySecond,   // 1/32
    };

    // Таблица DRV8825:
    // MODE0 MODE1 MODE2
    // 0     0     0     Full
    // 0     0     1     Half
    // 0     1     0     Quarter
    // 0     1     1     Eighth
    // 1     0     0     Sixteenth
    // 1     0     1     ThirtySecond
    static void microstepToPins(Microstep m, bool& m0, bool& m1, bool& m2) {
        m0 = false;
        m1 = false;
        m2 = false;

        switch (m) {
            case Microstep::Half:
                m2 = true;
                break;

            case Microstep::Quarter:
                m1 = true;
                break;

            case Microstep::Eighth:
                m1 = true;
                m2 = true;
                break;

            case Microstep::Sixteenth:
                m0 = true;
                break;

            case Microstep::ThirtySecond:
                m0 = true;
                m2 = true;
                break;

            case Microstep::Full:
            default:
                break;
        }
    }

    DRV8825() = default;

    // Базовая версия без пинов не подходит для DRV8825.
    bool begin() override { return false; }

    // stepPin, dirPin — обязательны.
    // enablePin, mode0..mode2 — опционально (-1 = не подключён).
    bool begin(uint8_t stepPin, uint8_t dirPin,
               int8_t enablePin = -1,
               int8_t mode0 = -1,
               int8_t mode1 = -1,
               int8_t mode2 = -1);

    bool step(bool forward) override;
    void enable(bool on) override;

    // Установить микрошаг (если MODE-пины подключены в begin()).
    void setMicrostep(Microstep m);

    // Длительность импульса STEP, мкс. Даташит требует >= 1.9 мкс.
    void setPulseWidthUs(uint8_t us) { _pulseUs = us ? us : 2; }

    // Задержка после enable(true), мкс.
    void setEnableDelayUs(uint16_t us) { _enableDelayUs = us; }

    Microstep microstep() const { return _microstep; }

   private:
    PinRef _step;
    PinRef _dirPin;
    PinRef _en;
    PinRef _mode0;
    PinRef _mode1;
    PinRef _mode2;

    bool _hasEn = false;
    bool _hasMode[3] = {false, false, false};

    uint8_t _pulseUs = 2;
    uint16_t _enableDelayUs = 1000;

    Microstep _microstep = Microstep::Full;
};

// Compile-time версия: все пины — параметры шаблона.
template <uint8_t STEP, uint8_t DIR, int8_t EN = -1,
          int8_t MODE0 = -1, int8_t MODE1 = -1, int8_t MODE2 = -1>
class DRV8825Pins : public StepperDriver {
   public:
    bool begin() override {
        Pin<STEP>::output();
        Pin<STEP>::low();

        Pin<DIR>::output();
        Pin<DIR>::low();

        // ENABLE активный низкий: до enable(true) драйвер выключен.
        detail::OptionalPin<EN>::output();
        detail::OptionalPin<EN>::high();

        detail::OptionalPin<MODE0>::output();
        detail::OptionalPin<MODE1>::output();
        detail::OptionalPin<MODE2>::output();

        detail::OptionalPin<MODE0>::low();
        detail::OptionalPin<MODE1>::low();
        detail::OptionalPin<MODE2>::low();

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
        detail::OptionalPin<EN>::write(!on);
        if (EN >= 0 && on) delayUs(_enableDelayUs);
        _enabled = on;
    }

    void setMicrostep(DRV8825::Microstep m) {
        bool m0 = false;
        bool m1 = false;
        bool m2 = false;
        DRV8825::microstepToPins(m, m0, m1, m2);

        detail::OptionalPin<MODE0>::write(m0);
        detail::OptionalPin<MODE1>::write(m1);
        detail::OptionalPin<MODE2>::write(m2);

        _microstep = m;
    }

    void setPulseWidthUs(uint8_t us) { _pulseUs = us ? us : 2; }
    void setEnableDelayUs(uint16_t us) { _enableDelayUs = us; }

    DRV8825::Microstep microstep() const { return _microstep; }

   private:
    uint8_t _pulseUs = 2;
    uint16_t _enableDelayUs = 1000;
    DRV8825::Microstep _microstep = DRV8825::Microstep::Full;
};

}  // namespace fn

#endif  // F_USE_STEPPER && F_USE_DRV8825

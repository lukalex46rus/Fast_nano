#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_STEPPER

#include "../pin/Pin.h"

namespace fn {

// Абстрактный драйвер шагового двигателя с интерфейсом STEP/DIR.
// Позиция ведётся программно (open-loop), без энкодера.
class StepperDriver {
   public:
    virtual ~StepperDriver() = default;

    // Инициализация пинов. Возвращает false при ошибке.
    virtual bool begin() = 0;

    // Один шаг в заданном направлении (true = вперёд).
    // Возвращает false, если драйвер выключен или ошибка.
    virtual bool step(bool forward) = 0;

    // Включить/выключить драйвер (мотор обесточен при false).
    virtual void enable(bool on) = 0;

    // Текущая позиция (в шагах, с учётом микрошага).
    int32_t position() const { return _pos; }

    // Сбросить счётчик позиции (не двигая мотор).
    void resetPosition(int32_t pos = 0) { _pos = pos; }

    // Направление: true = вперёд (увеличивает позицию).
    bool direction() const { return _dir; }

   protected:
    int32_t _pos = 0;
    bool    _dir = true;
    bool    _enabled = false;
};

namespace detail {

// Опциональный compile-time пин: P = -1 — пин не подключён.
// Специализация нужна вместо if constexpr (C++11).
template <int8_t P>
struct OptionalPin {
    static void output() { Pin<uint8_t(P)>::output(); }
    static void write(bool v) { Pin<uint8_t(P)>::write(v); }
    static void high() { Pin<uint8_t(P)>::high(); }
    static void low() { Pin<uint8_t(P)>::low(); }
};

template <>
struct OptionalPin<-1> {
    static void output() {}
    static void write(bool) {}
    static void high() {}
    static void low() {}
};

}  // namespace detail

}  // namespace fn

#endif  // F_USE_STEPPER

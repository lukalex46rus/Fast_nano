#pragma once
// Альфа-бета фильтр: сглаживает значение и оценивает скорость его изменения.
// Подходит для плавно меняющихся величин: датчик, позиция, скорость.

#include <math.h>
#include <stdint.h>

#include "../../core/Features.h"

#if F_USE_LOGIC

namespace fn {
namespace logic {

class AlphaBetaFilter {
   public:
    AlphaBetaFilter(float dt = 0.0f, float processNoise = 0.0f, float measurementNoise = 1.0f) {
        setParameters(dt, processNoise, measurementNoise);
    }

    // dt — период измерений, processNoise — разброс процесса,
    // measurementNoise — разброс измерения.
    bool setParameters(float dt, float processNoise, float measurementNoise) {
        if (dt <= 0.0f || measurementNoise <= 0.0f) return false;

        _dt = dt;
        float lambda = processNoise * dt * dt / measurementNoise;
        float r = (4.0f + lambda - sqrt(8.0f * lambda + lambda * lambda)) / 4.0f;
        _alpha = 1.0f - r * r;
        _beta = 2.0f * (2.0f - _alpha) - 4.0f * sqrt(1.0f - _alpha);
        return true;
    }

    // Измерение; возвращает сглаженную оценку.
    float update(float measurement) {
        float predicted = _position + _velocity * _dt;
        float residual = measurement - predicted;
        _position = predicted + _alpha * residual;
        _velocity += _beta * residual / _dt;
        return _position;
    }

    float position() const { return _position; }
    float velocity() const { return _velocity; }

    void reset(float position = 0.0f, float velocity = 0.0f) {
        _position = position;
        _velocity = velocity;
    }

   private:
    float _dt = 0.0f;
    float _alpha = 0.0f;
    float _beta = 0.0f;
    float _position = 0.0f;
    float _velocity = 0.0f;
};

}  // namespace logic
}  // namespace fn

#endif

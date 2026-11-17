#pragma once
// Одномерные фильтры Калмана.
// KalmanFilter — модель «постоянная величина» с явными predict/update.
// SimpleKalmanFilter — упрощённый вариант с подстройкой дисперсии по невязке.

#include <math.h>
#include <stdint.h>

#include "../../core/Features.h"

#if F_USE_LOGIC && F_USE_KALMAN

namespace fn {
namespace logic {

class KalmanFilter {
   public:
    KalmanFilter() = default;

    explicit KalmanFilter(float initial, float estimateError = 1.0f,
                          float processNoise = 0.01f, float measurementNoise = 1.0f) {
        begin(initial, estimateError, processNoise, measurementNoise);
    }

    void begin(float initial = 0.0f, float estimateError = 1.0f,
               float processNoise = 0.01f, float measurementNoise = 1.0f) {
        _value = initial;
        _estimateError = (estimateError > 0.0f) ? estimateError : 1.0f;
        _processNoise = (processNoise >= 0.0f) ? processNoise : 0.0f;
        _measurementNoise = (measurementNoise > 0.0f) ? measurementNoise : 1.0f;
        _gain = 0.0f;
    }

    void reset(float value = 0.0f) {
        _value = value;
        _estimateError = 1.0f;
        _gain = 0.0f;
    }

    void setProcessNoise(float processNoise) {
        _processNoise = (processNoise >= 0.0f) ? processNoise : 0.0f;
    }

    void setMeasurementNoise(float measurementNoise) {
        _measurementNoise = (measurementNoise > 0.0f) ? measurementNoise : 1.0f;
    }

    void setEstimateError(float estimateError) {
        _estimateError = (estimateError > 0.0f) ? estimateError : 1.0f;
    }

    // Предсказание на dt секунд. Возвращает оценку.
    float predict(float dt = 1.0f) {
        if (dt <= 0.0f) dt = 1.0f;
        _estimateError += _processNoise * dt;
        return _value;
    }

    // Измерение measurement; dt — время с предыдущего вызова в секундах.
    float update(float measurement, float dt = 1.0f) {
        predict(dt);
        float denominator = _estimateError + _measurementNoise;
        _gain = (denominator > 0.0f) ? (_estimateError / denominator) : 0.0f;
        _value += _gain * (measurement - _value);
        _estimateError *= (1.0f - _gain);
        if (_estimateError < 0.0f) _estimateError = 0.0f;
        return _value;
    }

    float value() const { return _value; }
    float gain() const { return _gain; }
    float variance() const { return _estimateError; }
    float processNoise() const { return _processNoise; }
    float measurementNoise() const { return _measurementNoise; }
    float estimateError() const { return _estimateError; }

   private:
    float _value = 0.0f;
    float _estimateError = 1.0f;
    float _processNoise = 0.01f;
    float _measurementNoise = 1.0f;
    float _gain = 0.0f;
};

// Упрощённый фильтр Калмана: оценка и подстройка дисперсии по невязке,
// без отдельного шага предсказания.
class SimpleKalmanFilter {
   public:
    SimpleKalmanFilter(float measurementError, float estimateError, float processNoise) {
        setParameters(measurementError, estimateError, processNoise);
    }

    SimpleKalmanFilter(float measurementError, float processNoise) {
        setParameters(measurementError, measurementError, processNoise);
    }

    void setParameters(float measurementError, float estimateError, float processNoise) {
        _measurementError = measurementError;
        _estimateError = estimateError;
        _processNoise = processNoise;
    }

    void setParameters(float measurementError, float processNoise) {
        setParameters(measurementError, measurementError, processNoise);
    }

    float update(float measurement) {
        float gain = _estimateError / (_estimateError + _measurementError);
        float estimate = _value + gain * (measurement - _value);
        _estimateError = (1.0f - gain) * _estimateError +
                         fabs(_value - estimate) * _processNoise;
        _value = estimate;
        return _value;
    }

    float value() const { return _value; }
    void reset(float value = 0.0f) { _value = value; }

   private:
    float _measurementError = 0.0f;
    float _estimateError = 0.0f;
    float _processNoise = 0.0f;
    float _value = 0.0f;
};

}  // namespace logic
}  // namespace fn

#endif

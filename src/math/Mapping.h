#pragma once
// ============================================================================
//  Mapping.h — отображение диапазонов и ограничение значений
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Замена Arduino map()/constrain(): без макросов, с 64-битной промежуточной
//    арифметикой, явным выбором округления и поддержкой reversed-диапазонов.
//    Плюс быстрый целочисленный путь для горячих циклов (АЦП -> ШИМ).
//
//  API (namespace fn::math)
//    Целочисленные (int64 внутри, без переполнения на полном int32):
//      map(x, inMin, inMax, outMin, outMax)          усечение к нулю
//      mapClamped(...)                               x зажат во входном диапазоне
//      mapRound(...)                                 к ближайшему, половина от нуля
//      mapFast(x, inMax, outMax)                     из [0..inMax], без int64
//      mapAdc8(v) / mapAdc16(v, top)                 специализации для АЦП
//    Float:
//      mapFloat / mapFloatClamped
//    Ограничение и проверка:
//      clamp(v, lo, hi)                              любой сравнимый тип
//      inRange(x, a, b)                              границы в любом порядке
//    Класс Mapper: хранит 4 границы + режим clamp, apply()/applyFloat()
//
//  ПОЛИТИКА ОКРУГЛЕНИЯ
//    map/mapClamped/mapFast — усечение к нулю (как деление C++).
//    mapRound               — к ближайшему, половина от нуля.
//    mapFloat               — обычная float-арифметика.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - inMin == inMax -> outMin во всех вариантах: деления на ноль нет.
//    - reversed-диапазоны поддерживаются (0..1023 -> 255..0): знак
//      знаменателя нормализуется в mapRound, в map получается естественно.
//    - mapFast работает только при inMin == 0 и outMin == 0 и требует
//      inMax <= 65535, outMax <= 65535: произведение помещается в uint32.
//      Вне этих условий результат неверен — используйте map().
//    - mapAdc8(v) = (v * 255 + 511) / 1023 — точное округление 10->8 бит,
//      ~10 инструкций против ~44 мкс у map() с int64-делением.
//    - Переполнение int64 при крайних int32-диапазонах не контролируется:
//      (x - inMin) * (outMax - outMin) может выйти за int64 при обоих
//      множителях около 2^32. На практике недостижимо.
//    - clamp требует lo <= hi: иначе поведение не определено (как constrain).
//    - Имена map/clamp конфликтуют с глобальными Arduino map()/constrain()
//      при `using namespace fn::math`. Вызывайте квалифицированно.
//    - sizeof(Mapper) == 17 байт (4 * int32 + bool).
//
//  СТОИМОСТЬ (AVR)
//    map с int64-делением ~44 мкс при 16 МГц. mapFast/mapAdc8 ~2 мкс.
//    mapFloat — soft-float, десятки мкс. В ISR и hot path > 1 кГц — только Fast.
//
//  ПРИМЕР
//    // АЦП 10 бит -> ШИМ 8 бит, горячий путь
//    uint8_t duty = fn::math::mapAdc8(adcValue);
//
//    // Общий случай с округлением
//    int32_t rpm = fn::math::mapRound(pulses, 0, 1000, 0, 6000);
//
//    // Калибровка датчика с ограничением
//    fn::math::Mapper cal(120, 890, -40, 125);
//    int32_t tempC = cal.apply(raw);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, math/MathUtils.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "MathUtils.h"

#include <stdint.h>

#if F_USE_MATH

namespace fn {
namespace math {

namespace detail {

// Границы входа в правильном порядке (для clamp перед отображением).
inline constexpr int32_t loOf(int32_t a, int32_t b) { return a < b ? a : b; }
inline constexpr int32_t hiOf(int32_t a, int32_t b) { return a < b ? b : a; }

}  // namespace detail

// ---------------------------- целочисленные --------------------------------
inline constexpr int32_t map(int32_t x, int32_t inMin, int32_t inMax,
                             int32_t outMin, int32_t outMax) {
    return (int64_t(inMax) - inMin) == 0
               ? outMin
               : int32_t((int64_t(x) - inMin) * (int64_t(outMax) - outMin) /
                             (int64_t(inMax) - inMin) +
                         outMin);
}

inline constexpr int32_t mapClamped(int32_t x, int32_t inMin, int32_t inMax,
                                    int32_t outMin, int32_t outMax) {
    return map(clamp(x, detail::loOf(inMin, inMax), detail::hiOf(inMin, inMax)),
               inMin, inMax, outMin, outMax);
}

// Округление к ближайшему: числитель и знаменатель нормализуются по знаку,
// затем добавляется половина знаменателя в сторону от нуля.
inline constexpr int32_t mapRound(int32_t x, int32_t inMin, int32_t inMax,
                                  int32_t outMin, int32_t outMax) {
    return (int64_t(inMax) - inMin) == 0
               ? outMin
               : int32_t(
                     ((int64_t(x) - inMin) * (int64_t(outMax) - outMin) * 2 +
                      ((int64_t(inMax) - inMin) > 0
                           ? (int64_t(inMax) - inMin)
                           : -(int64_t(inMax) - inMin)) *
                          (((int64_t(x) - inMin) * (int64_t(outMax) - outMin) >= 0)
                               ? 1 : -1)) /
                         ((int64_t(inMax) - inMin) * 2) +
                     outMin);
}

// Быстрый путь: [0..inMax] -> [0..outMax], uint32-арифметика без int64.
// Требования: inMax <= 65535, outMax <= 65535, x <= inMax.
inline uint16_t mapFast(uint16_t x, uint16_t inMax, uint16_t outMax) {
    if (inMax == 0) return 0;
    return uint16_t((uint32_t(x) * uint32_t(outMax)) / uint32_t(inMax));
}

// АЦП 10 бит -> 8 бит с округлением: ~10 инструкций.
inline constexpr uint8_t mapAdc8(uint16_t v) {
    return uint8_t((uint32_t(v) * 255u + 511u) / 1023u);
}

// АЦП 10 бит -> произвольный верхний предел с округлением.
inline uint16_t mapAdc16(uint16_t v, uint16_t top) {
    return uint16_t((uint32_t(v) * uint32_t(top) + 511u) / 1023u);
}

// ---------------------------- float ----------------------------------------
inline constexpr float mapFloat(float x, float inMin, float inMax,
                                float outMin, float outMax) {
    return inMin == inMax ? outMin
                          : outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin);
}

inline constexpr float mapFloatClamped(float x, float inMin, float inMax,
                                       float outMin, float outMax) {
    const float lo = inMin < inMax ? inMin : inMax;
    const float hi = inMin < inMax ? inMax : inMin;
    return mapFloat(x < lo ? lo : (x > hi ? hi : x), inMin, inMax, outMin, outMax);
}

// ---------------------------- ограничение ----------------------------------
template <typename T>
inline constexpr T clamp(T v, T lo, T hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline constexpr bool inRange(int32_t x, int32_t a, int32_t b) {
    return a <= b ? (x >= a && x <= b) : (x >= b && x <= a);
}

// ---------------------------- отображатель ---------------------------------
class Mapper {
public:
    Mapper() = default;

    constexpr Mapper(int32_t inMin, int32_t inMax, int32_t outMin, int32_t outMax,
                     bool clampOn = true)
        : _inMin(inMin), _inMax(inMax), _outMin(outMin), _outMax(outMax),
          _clampOn(clampOn) {}

    void setInput(int32_t lo, int32_t hi) { _inMin = lo; _inMax = hi; }
    void setOutput(int32_t lo, int32_t hi) { _outMin = lo; _outMax = hi; }
    void setClamp(bool on) { _clampOn = on; }

    constexpr int32_t inputMin() const { return _inMin; }
    constexpr int32_t inputMax() const { return _inMax; }
    constexpr int32_t outputMin() const { return _outMin; }
    constexpr int32_t outputMax() const { return _outMax; }
    constexpr bool clampEnabled() const { return _clampOn; }
    constexpr bool valid() const { return _inMin != _inMax; }

    constexpr int32_t apply(int32_t x) const {
        return _clampOn ? mapClamped(x, _inMin, _inMax, _outMin, _outMax)
                        : map(x, _inMin, _inMax, _outMin, _outMax);
    }

    int32_t applyRound(int32_t x) const {
        const int32_t v = _clampOn
                              ? clamp(x, detail::loOf(_inMin, _inMax),
                                      detail::hiOf(_inMin, _inMax))
                              : x;
        return mapRound(v, _inMin, _inMax, _outMin, _outMax);
    }

    float applyFloat(float x) const {
        return _clampOn
                   ? mapFloatClamped(x, float(_inMin), float(_inMax),
                                     float(_outMin), float(_outMax))
                   : mapFloat(x, float(_inMin), float(_inMax),
                              float(_outMin), float(_outMax));
    }

private:
    int32_t _inMin = 0;
    int32_t _inMax = 0;
    int32_t _outMin = 0;
    int32_t _outMax = 0;
    bool _clampOn = true;
};

}  // namespace math
}  // namespace fn

#endif  // F_USE_MATH
#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

#include "../timer/Tick.h"

namespace fn {
namespace logic {

// Периодический таймер: возвращает true раз в intervalMs.
class PeriodicTimer {
   public:
    explicit PeriodicTimer(uint16_t intervalMs, bool fireImmediately = false)
        : _interval(intervalMs) {
        _last = fireImmediately ? uint32_t(millis() - intervalMs) : millis();
    }

    bool tick() {
        uint32_t now = millis();
        if (uint32_t(now - _last) >= _interval) {
            _last = now;
            return true;
        }
        return false;
    }
    void reset() { _last = millis(); }
    void setInterval(uint16_t ms) { _interval = ms; }

   private:
    uint32_t _last = 0;
    uint16_t _interval;
};

}  // namespace logic
}  // namespace fn

#endif

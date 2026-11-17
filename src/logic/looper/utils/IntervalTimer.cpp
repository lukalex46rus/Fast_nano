#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "IntervalTimer.h"

#include "../platform.h"

namespace fn {

IntervalTimer::IntervalTimer(uint32_t ms, bool start) {
    _period = ms;
    if (start && ms) restart();
}

void IntervalTimer::restart() {
    _startedAt = looper::millis();
    if (!_startedAt) --_startedAt;
}

void IntervalTimer::start(uint32_t ms, uint32_t seconds, uint16_t minutes,
                          uint16_t hours, uint16_t days) {
    _period = ms;
    if (seconds) _period += seconds * 1000ul;
    if (minutes) _period += uint32_t(minutes) * 60ul * 1000ul;
    if (hours) _period += uint32_t(hours) * 60ul * 60ul * 1000ul;
    if (days) _period += uint32_t(days) * 24ul * 60ul * 60ul * 1000ul;
    restart();
}

void IntervalTimer::stop() {
    _startedAt = 0;
}

bool IntervalTimer::isRunning() {
    return _startedAt != 0;
}

void IntervalTimer::forceExpire() {
    _startedAt = looper::millis() - _period;
    if (!_startedAt) --_startedAt;
}

bool IntervalTimer::isReady() {
    return _startedAt && (looper::millis() - _startedAt >= _period);
}

uint32_t IntervalTimer::period() {
    return _period;
}

uint32_t IntervalTimer::remaining() {
    if (!_startedAt) return 0;
    uint32_t elapsed = looper::millis() - _startedAt;
    return (elapsed >= _period) ? 0 : (_period - elapsed);
}

bool IntervalTimer::expireAndRestart() {
    if (!isReady()) return false;
    restart();
    return true;
}

bool IntervalTimer::expire() {
    if (!isReady()) return false;
    stop();
    return true;
}

}  // namespace fn

#endif

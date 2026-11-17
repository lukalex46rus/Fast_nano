#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC

#include "../timer/Tick.h"

namespace fn {
namespace logic {

// Неблокирующая задержка.
class DelayTimer {
   public:
    void start(uint16_t ms) { _end = uint32_t(millis() + ms); _active = true; }
    bool done() {
        if (!_active) return false;
        if (int32_t(uint32_t(millis()) - _end) >= 0) {
            _active = false;
            return true;
        }
        return false;
    }
    bool active() const { return _active; }
    void cancel() { _active = false; }

   private:
    uint32_t _end = 0;
    bool _active = false;
};

}  // namespace logic
}  // namespace fn

#endif

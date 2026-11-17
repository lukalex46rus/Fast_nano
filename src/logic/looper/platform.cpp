#include "../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

// заменить для работы на других платформах

#include "platform.h"

#include <Arduino.h>

namespace fn {
namespace looper {

uint32_t millis() {
    return ::millis();
}

void yield() {
    delay(0);
}

}  // namespace looper
}  // namespace fn

#endif

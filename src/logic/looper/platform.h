#pragma once
#include <inttypes.h>

#include "../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

// размер стека отложенных событий
#ifndef LOOPER_QUEUE_SIZE
#define LOOPER_QUEUE_SIZE 3
#endif

// использовать ID задач для событий
#ifndef LOOPER_USE_ID
#define LOOPER_USE_ID true
#endif

// использовать события
#ifndef LOOPER_USE_EVENTS
#define LOOPER_USE_EVENTS true
#endif

namespace fn {
namespace looper {

// миллисекунды со старта МК
uint32_t millis();

// вызывается в цикле между задачами
void yield();

}  // namespace looper
}  // namespace fn

#endif

#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

namespace fn {

// Идентификатор задачи. На AVR совпадает с size_t (16 бит).
using TaskId = size_t;

// Хэш строки на этапе компиляции.
static constexpr TaskId looperTaskId(const char* str, const TaskId hash = 0) {
    return (*str ? looperTaskId(str + 1, hash + (hash << 5) + *str) : hash);
}
static constexpr TaskId looperTaskId(TaskId id) {
    return id;
}

// Хэш строки во время выполнения.
inline TaskId looperTaskIdRuntime(const char* str) {
    TaskId hash = 0;
    while (*str) hash = hash + (hash << 5) + *str++;
    return hash;
}
inline TaskId looperTaskIdRuntime(TaskId id) {
    return id;
}

}  // namespace fn

#endif

#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../LooperEngine.h"
#include "../utils/IntervalTimer.h"
#include "CallbackData.h"
#include "LoopTask.h"

namespace fn {

// Задача-таймер: вызывает обработчик с заданным периодом.
class LoopTimer : public LoopTask, public IntervalTimer {
   public:
    LoopTimer(uint32_t ms, TaskCallback callback, bool start = true, bool states = false)
        : LoopTimer(TaskId(0), ms, callback, start, states) {}
    LoopTimer(const char* id, uint32_t ms, TaskCallback callback, bool start = true, bool states = false)
        : LoopTimer(looperTaskIdRuntime(id), ms, callback, start, states) {}
    LoopTimer(TaskId id, uint32_t ms, TaskCallback callback, bool start = true, bool states = false)
        : LoopTask(id, callback, LOOPER_TASK_TYPE_TIMER, states, false), IntervalTimer(ms, start) {}

    // Проверить период и выполнить задачу.
    void poll() {
        if (expireAndRestart()) execute();
    }
};

// Таймер для собственных классов.
class LoopTimerBase : public LoopTimer {
   public:
    LoopTimerBase(uint32_t ms, bool start = true, bool states = false)
        : LoopTimerBase(TaskId(0), ms, start, states) {}
    LoopTimerBase(const char* id, uint32_t ms, bool start = true, bool states = false)
        : LoopTimerBase(looperTaskIdRuntime(id), ms, start, states) {}
    LoopTimerBase(TaskId id, uint32_t ms, bool start = true, bool states = false)
        : LoopTimer(id, ms, _onTimer, start, states) {}

    // Вызывается при срабатывании таймера.
    virtual void onTimer() = 0;

   private:
    using LoopTask::clearCallback;
    using LoopTask::execute;
    using LoopTask::setCallback;

    static void _onTimer() {
        Looper.currentTaskAs<LoopTimerBase>()->onTimer();
    }
};

// Таймер с данными.
template <typename T>
class LoopTimerData : public LoopTimer, public TaskData<T> {
    LOOPER_MAKE_CALLBACK(DataCallback, void, T*);

   public:
    LoopTimerData(uint32_t ms, T* data, DataCallback callback, bool start = true, bool states = false)
        : LoopTimerData(TaskId(0), ms, data, callback, start, states) {}
    LoopTimerData(const char* id, uint32_t ms, T* data, DataCallback callback, bool start = true, bool states = false)
        : LoopTimerData(looperTaskIdRuntime(id), ms, data, callback, start, states) {}
    LoopTimerData(TaskId id, uint32_t ms, T* data, DataCallback callback, bool start = true, bool states = false)
        : LoopTimer(id, ms, _onTimer, start, states), TaskData<T>(data, callback) {}

   private:
    using LoopTask::clearCallback;
    using LoopTask::setCallback;

    static void _onTimer() {
        Looper.currentTaskAs<LoopTimerData<T>>()->TaskData<T>::invoke();
    }
};

}  // namespace fn

#endif

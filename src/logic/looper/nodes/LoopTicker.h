#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../LooperEngine.h"
#include "CallbackData.h"
#include "LoopTask.h"

namespace fn {

// Задача-тикер: вызывается на каждом проходе планировщика.
class LoopTicker : public LoopTask {
   public:
    LoopTicker(TaskCallback callback) : LoopTicker(TaskId(0), callback) {}
    LoopTicker(const char* id, TaskCallback callback) : LoopTicker(looperTaskIdRuntime(id), callback) {}
    LoopTicker(TaskId id, TaskCallback callback)
        : LoopTask(id, callback, LOOPER_TASK_TYPE_TICKER, true, true) {}
};

// Тикер для собственных классов.
class LoopTickerBase : public LoopTicker {
   public:
    LoopTickerBase() : LoopTickerBase(TaskId(0)) {}
    LoopTickerBase(const char* id) : LoopTickerBase(looperTaskIdRuntime(id)) {}
    LoopTickerBase(TaskId id) : LoopTicker(id, _onTick) {}

    // Вызывается на каждом проходе и при событиях.
    virtual void onTick() = 0;

   private:
    using LoopTask::clearCallback;
    using LoopTask::execute;
    using LoopTask::setCallback;

    static void _onTick() {
        Looper.currentTaskAs<LoopTickerBase>()->onTick();
    }
};

// Тикер с данными.
template <typename T>
class LoopTickerData : public LoopTicker, public TaskData<T> {
    LOOPER_MAKE_CALLBACK(DataCallback, void, T*);

   public:
    LoopTickerData(T* data, DataCallback callback)
        : LoopTickerData(TaskId(0), data, callback) {}
    LoopTickerData(const char* id, T* data, DataCallback callback)
        : LoopTickerData(looperTaskIdRuntime(id), data, callback) {}
    LoopTickerData(TaskId id, T* data, DataCallback callback)
        : LoopTicker(id, _onTick), TaskData<T>(data, callback) {}

   private:
    using LoopTask::clearCallback;
    using LoopTask::setCallback;

    static void _onTick() {
        Looper.currentTaskAs<LoopTickerData<T>>()->TaskData<T>::invoke();
    }
};

}  // namespace fn

#endif

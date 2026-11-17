#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../LooperEngine.h"
#include "CallbackData.h"
#include "LoopTask.h"

namespace fn {

// Задача-обработчик событий: вызывается при событии.
class LoopListener : public LoopTask {
   public:
    LoopListener(const char* id, TaskCallback callback, bool states = false)
        : LoopListener(looperTaskIdRuntime(id), callback, states) {}
    LoopListener(TaskId id, TaskCallback callback, bool states = false)
        : LoopTask(id, callback, LOOPER_TASK_TYPE_LISTENER, states, true) {}
};

// Обработчик событий для собственных классов.
class LoopListenerBase : public LoopListener {
   public:
    LoopListenerBase(const char* id, bool states = false)
        : LoopListenerBase(looperTaskIdRuntime(id), states) {}
    LoopListenerBase(TaskId id, bool states = false)
        : LoopListener(id, _onEvent, states) {}

    // Вызывается при получении события.
    virtual void onEvent() = 0;

   private:
    using LoopTask::clearCallback;
    using LoopTask::execute;
    using LoopTask::setCallback;

    static void _onEvent() {
        Looper.currentTaskAs<LoopListenerBase>()->onEvent();
    }
};

// Обработчик событий с данными.
template <typename T>
class LoopListenerData : public LoopListener, public TaskData<T> {
    LOOPER_MAKE_CALLBACK(DataCallback, void, T*);

   public:
    LoopListenerData(const char* id, T* data, DataCallback callback, bool states = false)
        : LoopListenerData(looperTaskIdRuntime(id), data, callback, states) {}
    LoopListenerData(TaskId id, T* data, DataCallback callback, bool states = false)
        : LoopListener(id, _onEvent, states), TaskData<T>(data, callback) {}

   private:
    using LoopTask::clearCallback;
    using LoopTask::setCallback;

    static void _onEvent() {
        Looper.currentTaskAs<LoopListenerData<T>>()->TaskData<T>::invoke();
    }
};

}  // namespace fn

#endif

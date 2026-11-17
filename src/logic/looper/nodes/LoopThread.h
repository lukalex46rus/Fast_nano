#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../LooperEngine.h"
#include "../utils/IntervalTimer.h"
#include "CallbackData.h"
#include "LoopTask.h"

namespace fn {

// Задача-поток: линейный сценарий, который можно приостанавливать
// макросами LOOPER_WAIT / LOOPER_DELAY.
class LoopThread : public LoopTask {
   public:
    LoopThread(TaskCallback callback) : LoopThread(TaskId(0), callback) {}
    LoopThread(const char* id, TaskCallback callback) : LoopThread(looperTaskIdRuntime(id), callback) {}
    LoopThread(TaskId id, TaskCallback callback)
        : LoopTask(id, callback, LOOPER_TASK_TYPE_THREAD, false, true) {}

    // Начать сценарий с первой строки.
    void rewind() {
        resumePoint = 0;
    }

    // Точка продолжения сценария (используется макросами LOOPER_*).
    uint16_t resumePoint = 0;

    // Таймер для LOOPER_DELAY.
    IntervalTimer timer;
};

// Поток с данными.
template <typename T>
class LoopThreadData : public LoopThread, public TaskData<T> {
    LOOPER_MAKE_CALLBACK(DataCallback, void, T*);

   public:
    LoopThreadData(T* data, DataCallback callback)
        : LoopThreadData(TaskId(0), data, callback) {}
    LoopThreadData(const char* id, T* data, DataCallback callback)
        : LoopThreadData(looperTaskIdRuntime(id), data, callback) {}
    LoopThreadData(TaskId id, T* data, DataCallback callback)
        : LoopThread(id, _onStep), TaskData<T>(data, callback) {}

   private:
    using LoopTask::clearCallback;
    using LoopTask::setCallback;

    static void _onStep() {
        Looper.currentTaskAs<LoopThreadData<T>>()->TaskData<T>::invoke();
    }
};

}  // namespace fn

#endif

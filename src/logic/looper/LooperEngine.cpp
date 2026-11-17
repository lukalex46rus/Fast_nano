#include "../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "LooperEngine.h"

#include <limits.h>

#include "nodes/LoopThread.h"
#include "nodes/LoopTimer.h"
#include "platform.h"

namespace fn {

LooperEngine Looper;

void LooperEngine::run(bool main) {
    // _currentTask и _currentState при входе не сохраняются: run() — основной цикл.
    _currentTask = _tasks.first();

    while (_currentTask) {
        switch (_currentTask->_readFlags(LOOPER_TASK_MASK_STATE | LOOPER_TASK_DISABLED | LOOPER_TASK_SKIPPED)) {
            case LOOPER_TASK_STATE_SETUP:
            case LOOPER_TASK_STATE_SETUP | LOOPER_TASK_DISABLED:
                if (main) {
                    _currentTask->_setStateBits(LOOPER_TASK_STATE_LOOP);
                    _executeState(TaskState::Setup);
                }
                break;

            case LOOPER_TASK_STATE_EXIT:
            case LOOPER_TASK_STATE_EXIT | LOOPER_TASK_DISABLED:
                if (main) {
                    _currentTask->_setStateBits(LOOPER_TASK_STATE_NONE);
                    LoopTask* previous = _tasks.remove(_currentTask);
                    _executeState(TaskState::Exit);
                    _currentTask = previous;
                }
                break;

            case LOOPER_TASK_STATE_LOOP:
                switch (_currentTask->_readFlags(LOOPER_TASK_MASK_TYPE)) {
                    case LOOPER_TASK_TYPE_TICKER:
                    case LOOPER_TASK_TYPE_THREAD:
                        _currentTask->execute();
                        break;

                    case LOOPER_TASK_TYPE_TIMER:
                        currentTimer()->poll();
                        break;
                }
                break;
        }
        _currentTask = _currentTask ? _currentTask->next() : _tasks.first();
        looper::yield();
    }

#if LOOPER_USE_EVENTS && LOOPER_QUEUE_SIZE
    while (main && _eventQueue.size()) _dispatchEvent(_eventQueue.pop());
#endif
}

void LooperEngine::_executeState(TaskState state) {
    if (_currentTask->hasStates()) {
        _currentState = state;
        _currentTask->execute();
        _currentState = TaskState::Loop;
    }
}

void LooperEngine::onEvent(EventCallback callback) {
#if LOOPER_USE_EVENTS
    _eventCallback = callback;
#else
    (void)callback;
#endif
}

void LooperEngine::restartAll() {
    LoopTask* task = _tasks.first();
    while (task) {
        task->reset();
        if (task->isThread()) static_cast<LoopThread*>(task)->rewind();
        else if (task->isTimer()) static_cast<LoopTimer*>(task)->restart();
        task = task->next();
    }
}

uint32_t LooperEngine::timeUntilNextTimer() {
    uint32_t shortest = UINT32_MAX;
    LoopTask* task = _tasks.first();
    while (task) {
        if (task->_stateBits() != LOOPER_TASK_STATE_LOOP) return 0;

        uint32_t left = 0;
        switch (task->_readFlags(LOOPER_TASK_MASK_TYPE | LOOPER_TASK_DISABLED)) {
            case LOOPER_TASK_TYPE_TICKER:
                return 0;

            case LOOPER_TASK_TYPE_TIMER:
                if (!static_cast<LoopTimer*>(task)->isRunning()) goto next;
                left = static_cast<LoopTimer*>(task)->remaining();
                break;

            case LOOPER_TASK_TYPE_THREAD:
                left = static_cast<LoopThread*>(task)->timer.remaining();
                break;

            default:
                goto next;
        }
        if (!left) return 0;
        if (shortest > left) shortest = left;

    next:
        task = task->next();
    }
    return (shortest == UINT32_MAX) ? 0 : shortest;
}

uint16_t LooperEngine::taskCount() {
    return uint16_t(_tasks.size());
}

void LooperEngine::runFor(uint32_t ms) {
    uint32_t startedAt = looper::millis();
    LoopTask* savedTask = _currentTask;
    TaskState savedState = _currentState;

    _currentState = TaskState::Loop;
    if (savedTask) savedTask->_skip();

    while (looper::millis() - startedAt < ms) {
        run(false);
        looper::yield();
    }

    if (savedTask) savedTask->_unskip();
    _currentState = savedState;
    _currentTask = savedTask;
}

void LooperEngine::addTask(LoopTask* task) {
    if (task && !task->isAdded()) {
        task->_setStateBits(task->hasStates() ? LOOPER_TASK_STATE_SETUP : LOOPER_TASK_STATE_LOOP);
        _tasks.add(task);
    }
}

void LooperEngine::removeTask(LoopTask* task) {
    if (task && task->isAdded()) task->_setStateBits(LOOPER_TASK_STATE_EXIT);
}

void LooperEngine::removeCurrentTask() {
    removeTask(_currentTask);
}

void LooperEngine::removeAllTasks() {
    LoopTask* task = _tasks.first();
    while (task) {
        removeTask(task);
        task = task->next();
    }
}

void LooperEngine::_removeImmediately(LoopTask* task) {
    if (task && task->isAdded()) {
        task->_setStateBits(LOOPER_TASK_STATE_NONE);
        LoopTask* previous = _tasks.remove(task);
        if (_currentTask == task) _currentTask = previous;
    }
}

TaskState LooperEngine::currentState() { return _currentState; }
LoopTask* LooperEngine::currentTask() { return _currentTask; }
LoopTimer* LooperEngine::currentTimer() { return currentTaskAs<LoopTimer>(); }
LoopThread* LooperEngine::currentThread() { return currentTaskAs<LoopThread>(); }
bool LooperEngine::isInEvent() { return _currentState == TaskState::Event; }
bool LooperEngine::isInSetup() { return _currentState == TaskState::Setup; }
bool LooperEngine::isInExit() { return _currentState == TaskState::Exit; }
bool LooperEngine::isInLoop() { return _currentState == TaskState::Loop; }

bool LooperEngine::isBroadcastEvent() {
#if LOOPER_USE_EVENTS
    return _eventBroadcast;
#else
    return false;
#endif
}

LoopTask* LooperEngine::currentEventSource() {
#if LOOPER_USE_EVENTS
    return _eventSource;
#else
    return nullptr;
#endif
}

void* LooperEngine::currentEventData() {
#if LOOPER_USE_EVENTS
    return _eventData;
#else
    return nullptr;
#endif
}

void LooperEngine::_dispatchEvent(EventData& event) {
    sendEvent(event.id, event.data);
}

void LooperEngine::sendEvent(TaskId id, void* data) {
#if LOOPER_USE_EVENTS
    LoopTask* savedSource = _eventSource;
    LoopTask* savedTask = _currentTask;
    TaskState savedState = _currentState;
    bool savedBroadcast = _eventBroadcast;
    void* savedData = _eventData;

    _currentState = TaskState::Event;
    _eventSource = _currentTask;
    _eventBroadcast = (id == 0);
    _eventData = data;

    if (_eventCallback) _eventCallback(id);

#if LOOPER_USE_ID
    _currentTask = _tasks.first();
    while (_currentTask) {
        if (_currentTask->id() &&
            _currentTask != _eventSource &&
            (_eventBroadcast || _currentTask->id() == id) &&
            _currentTask->canListen()) {
            _currentTask->execute();
            looper::yield();
        }
        _currentTask = _currentTask ? _currentTask->next() : _tasks.first();
    }
#endif

    _eventSource = savedSource;
    _eventBroadcast = savedBroadcast;
    _currentState = savedState;
    _eventData = savedData;
    _currentTask = savedTask;
#else
    (void)id;
    (void)data;
#endif
}

void LooperEngine::sendEvent(const char* id, void* data) {
    sendEvent(looperTaskIdRuntime(id), data);
}

bool LooperEngine::pushEvent(TaskId id, void* data) {
#if LOOPER_USE_EVENTS && LOOPER_QUEUE_SIZE
    return _eventQueue.push(EventData{id, data});
#else
    (void)id;
    (void)data;
    return false;
#endif
}

bool LooperEngine::pushEvent(const char* id, void* data) {
    return pushEvent(looperTaskIdRuntime(id), data);
}

LoopTask* LooperEngine::task(TaskId id) {
#if LOOPER_USE_ID
    if (!id) return nullptr;
    LoopTask* current = _tasks.first();
    while (current) {
        if (current->id() == id) return current;
        current = current->next();
    }
#else
    (void)id;
#endif
    return nullptr;
}

LoopTask* LooperEngine::task(const char* id) {
    return task(looperTaskIdRuntime(id));
}

LoopTimer* LooperEngine::timer(TaskId id) {
    return static_cast<LoopTimer*>(task(id));
}

LoopTimer* LooperEngine::timer(const char* id) {
    return timer(looperTaskIdRuntime(id));
}

LoopThread* LooperEngine::thread(TaskId id) {
    return static_cast<LoopThread*>(task(id));
}

LoopThread* LooperEngine::thread(const char* id) {
    return thread(looperTaskIdRuntime(id));
}

}  // namespace fn

#endif

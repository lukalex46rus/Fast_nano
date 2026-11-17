#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "LoopTask.h"

#include "../LooperEngine.h"

namespace fn {

LoopTask::LoopTask(TaskId id, TaskCallback callback, uint8_t type, bool states, bool events)
    : _callback(callback) {
#if LOOPER_USE_ID
    _id = id;
#else
    (void)id;
#endif
    _flags.flags = (type & LOOPER_TASK_MASK_TYPE) |
                   (states ? LOOPER_TASK_HAS_STATES : 0) |
                   (events ? LOOPER_TASK_HAS_EVENTS : 0);
    addToLooper();
}

LoopTask::~LoopTask() {
    Looper._removeImmediately(this);
}

void LoopTask::addToLooper() {
    Looper.addTask(this);
}

void LoopTask::removeFromLooper() {
    Looper.removeTask(this);
}

bool LoopTask::isAdded() {
    return _stateBits() != LOOPER_TASK_STATE_NONE;
}

void LoopTask::reset() {
    if (hasStates() && _stateBits() == LOOPER_TASK_STATE_LOOP) {
        _setStateBits(LOOPER_TASK_STATE_SETUP);
    }
}

void LoopTask::execute() {
    if (_callback) _callback();
}

void LoopTask::setCallback(TaskCallback callback) {
    _callback = callback;
}

void LoopTask::clearCallback() {
    _callback = nullptr;
}

void LoopTask::enableEvents() {
    _flags.set(LOOPER_TASK_HAS_EVENTS);
}

void LoopTask::disableEvents() {
    _flags.clear(LOOPER_TASK_HAS_EVENTS);
}

void LoopTask::enableStates() {
    _flags.set(LOOPER_TASK_HAS_STATES);
}

void LoopTask::disableStates() {
    _flags.clear(LOOPER_TASK_HAS_STATES);
}

TaskId LoopTask::id() const {
#if LOOPER_USE_ID
    return _id;
#else
    return 0;
#endif
}

void LoopTask::enable() {
    _flags.clear(LOOPER_TASK_DISABLED);
}

void LoopTask::disable() {
    _flags.set(LOOPER_TASK_DISABLED);
}

void LoopTask::toggle() {
    isEnabled() ? disable() : enable();
}

bool LoopTask::isEnabled() const {
    return !_flags.read(LOOPER_TASK_DISABLED);
}

bool LoopTask::isTimer() const {
    return type() == LOOPER_TASK_TYPE_TIMER;
}

bool LoopTask::isTicker() const {
    return type() == LOOPER_TASK_TYPE_TICKER;
}

bool LoopTask::isListener() const {
    return type() == LOOPER_TASK_TYPE_LISTENER;
}

bool LoopTask::isThread() const {
    return type() == LOOPER_TASK_TYPE_THREAD;
}

uint8_t LoopTask::type() const {
    return _flags.read(LOOPER_TASK_MASK_TYPE);
}

bool LoopTask::hasStates() const {
    return _flags.read(LOOPER_TASK_HAS_STATES);
}

bool LoopTask::canListen() const {
    return _flags.compare(LOOPER_TASK_MASK_STATE | LOOPER_TASK_HAS_EVENTS | LOOPER_TASK_SKIPPED | LOOPER_TASK_DISABLED,
                          LOOPER_TASK_STATE_LOOP | LOOPER_TASK_HAS_EVENTS);
}

void LoopTask::_setStateBits(uint8_t state) {
    _flags.writeBits(LOOPER_TASK_MASK_STATE, state);
}

uint8_t LoopTask::_stateBits() const {
    return _flags.read(LOOPER_TASK_MASK_STATE);
}

uint8_t LoopTask::_readFlags(uint8_t mask) const {
    return _flags.read(mask);
}

void LoopTask::_skip() {
    _flags.set(LOOPER_TASK_SKIPPED);
}

void LoopTask::_unskip() {
    _flags.clear(LOOPER_TASK_SKIPPED);
}

}  // namespace fn

#endif

#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../platform.h"
#include "../utils/flags.h"
#include "../utils/hash.h"
#include "../utils/list.h"
#include "../utils/macro.h"

#define LOOPER_TASK_SHIFT_TYPE 0
#define LOOPER_TASK_TYPE_LISTENER (0 << LOOPER_TASK_SHIFT_TYPE)
#define LOOPER_TASK_TYPE_TICKER (1 << LOOPER_TASK_SHIFT_TYPE)
#define LOOPER_TASK_TYPE_THREAD (2 << LOOPER_TASK_SHIFT_TYPE)
#define LOOPER_TASK_TYPE_TIMER (3 << LOOPER_TASK_SHIFT_TYPE)
#define LOOPER_TASK_MASK_TYPE (LOOPER_TASK_TYPE_LISTENER | LOOPER_TASK_TYPE_TICKER | LOOPER_TASK_TYPE_THREAD | LOOPER_TASK_TYPE_TIMER)

#define LOOPER_TASK_SHIFT_STATE 2
#define LOOPER_TASK_STATE_NONE (0 << LOOPER_TASK_SHIFT_STATE)
#define LOOPER_TASK_STATE_SETUP (1 << LOOPER_TASK_SHIFT_STATE)
#define LOOPER_TASK_STATE_LOOP (2 << LOOPER_TASK_SHIFT_STATE)
#define LOOPER_TASK_STATE_EXIT (3 << LOOPER_TASK_SHIFT_STATE)
#define LOOPER_TASK_MASK_STATE (LOOPER_TASK_STATE_NONE | LOOPER_TASK_STATE_SETUP | LOOPER_TASK_STATE_LOOP | LOOPER_TASK_STATE_EXIT)

#define LOOPER_TASK_HAS_STATES (1 << 4)
#define LOOPER_TASK_HAS_EVENTS (1 << 5)
#define LOOPER_TASK_DISABLED (1 << 6)
#define LOOPER_TASK_SKIPPED (1 << 7)

namespace fn {

enum class TaskState : uint8_t {
    Loop,
    Setup,
    Exit,
    Event,
};

LOOPER_MAKE_CALLBACK(TaskCallback, void);

// Базовая задача планировщика. Наследники: LoopTimer, LoopTicker,
// LoopThread и LoopListener.
class LoopTask : public looper::List<LoopTask>::Node {
   public:
    LoopTask(TaskId id, TaskCallback callback, uint8_t type, bool states, bool events);
    ~LoopTask();

    // Добавить задачу в планировщик.
    void addToLooper();

    // Пометить задачу на удаление.
    void removeFromLooper();

    // Задача зарегистрирована в планировщике.
    bool isAdded();

    // Вернуть задачу на стадию Setup (только для добавленных задач).
    void reset();

    // Вызвать обработчик задачи.
    void execute();

    // Назначить обработчик.
    void setCallback(TaskCallback callback);

    // Снять обработчик.
    void clearCallback();

    // Идентификатор задачи.
    TaskId id() const;

    // Приём событий.
    void enableEvents();
    void disableEvents();

    // Состояния Setup и Exit.
    void enableStates();
    void disableStates();

    // Включение задачи (состояния Loop и Event).
    void enable();
    void disable();
    void toggle();
    bool isEnabled() const;

    // Тип задачи.
    bool isTimer() const;
    bool isTicker() const;
    bool isListener() const;
    bool isThread() const;
    uint8_t type() const;

    // Задача поддерживает состояния Setup/Exit.
    bool hasStates() const;

    // Задача может принимать события.
    bool canListen() const;

    // ============ INTERNAL ============
    void _setStateBits(uint8_t state);
    uint8_t _stateBits() const;
    uint8_t _readFlags(uint8_t mask) const;
    void _skip();
    void _unskip();

   private:
#if LOOPER_USE_ID
    TaskId _id;
#endif
    TaskCallback _callback;
    looper::Flags _flags;
};

}  // namespace fn

#endif

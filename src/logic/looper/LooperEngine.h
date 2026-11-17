#pragma once
#include <inttypes.h>

#include "../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "nodes/LoopTask.h"
#include "platform.h"
#include "utils/hash.h"
#include "utils/list.h"
#include "utils/macro.h"
#include "utils/stack.h"

namespace fn {

class LoopTimer;
class LoopThread;

// ========= LooperEngine =========
// Кооперативный планировщик задач. Пользовательский экземпляр — fn::Looper.
class LooperEngine {
    LOOPER_MAKE_CALLBACK(EventCallback, void, TaskId);

   public:
    // ========= SYSTEM =========
    // Выполнить один проход планировщика. main=false — только задачи,
    // без состояний Setup/Exit и без очереди отложенных событий.
    void run(bool main = true);

    // Перезапустить все задачи: Setup, таймеры и потоки с начала.
    void restartAll();

    // Количество зарегистрированных задач.
    uint16_t taskCount();

    // Время до срабатывания ближайшего таймера, мс.
    uint32_t timeUntilNextTimer();

    // Блокирующее ожидание: крутит вложенный цикл указанное время.
    void runFor(uint32_t ms);

    // Добавить задачу в планировщик.
    void addTask(LoopTask* task);

    // Пометить задачу на удаление после текущего прохода.
    void removeTask(LoopTask* task);

    // Пометить текущую задачу на удаление.
    void removeCurrentTask();

    // Пометить все задачи на удаление.
    void removeAllTasks();

    // Найти задачу по идентификатору.
    LoopTask* task(TaskId id);
    LoopTask* task(const char* id);

    LoopTimer* timer(TaskId id);
    LoopTimer* timer(const char* id);

    LoopThread* thread(TaskId id);
    LoopThread* thread(const char* id);

    // ========= CURRENT TASK =========

    // Текущая выполняемая задача.
    LoopTask* currentTask();

    // Текущая задача как таймер.
    LoopTimer* currentTimer();

    // Текущая задача как поток.
    LoopThread* currentThread();

    // Текущая задача, приведённая к указанному типу.
    template <typename T>
    T* currentTaskAs() {
        return static_cast<T*>(_currentTask);
    }

    // Состояние текущей задачи.
    TaskState currentState();

    bool isInSetup();
    bool isInExit();
    bool isInLoop();
    bool isInEvent();

    // ========= EVENT =========

    // Данные текущего события.
    void* currentEventData();

    // Задача, отправившая текущее событие.
    LoopTask* currentEventSource();

    // Текущее событие отправлено всем задачам.
    bool isBroadcastEvent();

    // Отправить событие немедленно.
    void sendEvent(TaskId id, void* data = nullptr);
    void sendEvent(const char* id, void* data = nullptr);

    // Поставить событие в очередь (будет отправлено из run()).
    bool pushEvent(TaskId id, void* data = nullptr);
    bool pushEvent(const char* id, void* data = nullptr);

    // Перехватчик событий вида void f(TaskId id).
    void onEvent(EventCallback callback);

    // ============ INTERNAL ============
    void _removeImmediately(LoopTask* task);

   private:
    // Все члены без инициализаторов: Looper живёт в статической памяти и должен
    // быть constant-initialized (bss, нули). Иначе динамический конструктор Looper
    // может обнулить список задач, куда уже добавились глобальные LOOPER_TIMER и
    // LOOPER_TICKER. Нули дают нужные значения: nullptr, TaskState::Loop == 0,
    // Stack::_size == 0.
    struct EventData {
        TaskId id;
        void* data;
    };

#if LOOPER_USE_EVENTS
#if LOOPER_QUEUE_SIZE
    looper::Stack<EventData, LOOPER_QUEUE_SIZE> _eventQueue;
#endif
    EventCallback _eventCallback;
    LoopTask* _eventSource;
    void* _eventData;
    bool _eventBroadcast;
#endif

    looper::List<LoopTask> _tasks;
    LoopTask* _currentTask;
    TaskState _currentState;

    void _dispatchEvent(EventData& event);
    void _executeState(TaskState state);
};

extern LooperEngine Looper;

}  // namespace fn

#endif

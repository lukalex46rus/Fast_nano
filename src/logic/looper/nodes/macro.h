#pragma once
#include <inttypes.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

// ========= пользовательские макросы =========
// Широковещательное событие.
#define LOOPER_BROADCAST (::fn::TaskId)0

// Отправить событие немедленно.
#define LOOPER_SEND_EVENT(id, data) ::fn::Looper.sendEvent(::fn::looperTaskId(id), data)

// Поставить событие в очередь.
#define LOOPER_PUSH_EVENT(id, data) ::fn::Looper.pushEvent(::fn::looperTaskId(id), data)

// Создать статическую задачу.
#define LOOPER_MAKE_TASK(type, ...) \
    static type LOOPER_CONCAT(__looper_task_, __COUNTER__)(__VA_ARGS__);
#define LOOPER_MAKE_TASK_WITH_ID(id, type, ...) \
    static type LOOPER_CONCAT(__looper_task_, __COUNTER__)(::fn::looperTaskId(id), __VA_ARGS__);

// Создать задачу в динамической памяти.
#define LOOPER_NEW_TASK(type, ...) new type(__VA_ARGS__)
#define LOOPER_NEW_TASK_WITH_ID(id, type, ...) new type(::fn::looperTaskId(id), __VA_ARGS__)

// LISTENER
#define LOOPER_LISTENER_WITH_ID(id, ...) LOOPER_MAKE_TASK_WITH_ID(id, ::fn::LoopListener, __VA_ARGS__)

// TICKER
#define LOOPER_TICKER(...) LOOPER_MAKE_TASK(::fn::LoopTicker, __VA_ARGS__)
#define LOOPER_TICKER_WITH_ID(id, ...) LOOPER_MAKE_TASK_WITH_ID(id, ::fn::LoopTicker, __VA_ARGS__)

// TIMER
#define LOOPER_TIMER(ms, ...) LOOPER_MAKE_TASK(::fn::LoopTimer, ms, __VA_ARGS__)
#define LOOPER_TIMER_WITH_ID(id, ms, ...) LOOPER_MAKE_TASK_WITH_ID(id, ::fn::LoopTimer, ms, __VA_ARGS__)

// THREAD
#define LOOPER_THREAD_CASE ::fn::Looper.currentThread()->resumePoint
#define LOOPER_THREAD_RESET() LOOPER_THREAD_CASE = 0;

// Начать поток.
#define LOOPER_THREAD_BEGIN()       \
    switch (LOOPER_THREAD_CASE) {   \
        case 0:;

// Завершить поток.
#define LOOPER_THREAD_END() \
    }                       \
    LOOPER_THREAD_RESET();

#define LOOPER_THREAD_BODY(body) \
    LOOPER_THREAD_BEGIN();       \
    body                         \
    LOOPER_THREAD_END();

#define LOOPER_THREAD(body) \
    static ::fn::LoopThread LOOPER_CONCAT(__looper_thread_, __COUNTER__)([]() { LOOPER_THREAD_BODY(body) });
#define LOOPER_THREAD_WITH_ID(id, body) \
    static ::fn::LoopThread LOOPER_CONCAT(__looper_thread_, __COUNTER__)(::fn::looperTaskId(id), []() { LOOPER_THREAD_BODY(body) });
#define LOOPER_THREAD_DATA(T, data, data_arg, body) \
    static ::fn::LoopThreadData<T> LOOPER_CONCAT(__looper_thread_, __COUNTER__)(data, [](data_arg) { LOOPER_THREAD_BODY(body) });
#define LOOPER_THREAD_DATA_WITH_ID(id, T, data, data_arg, body) \
    static ::fn::LoopThreadData<T> LOOPER_CONCAT(__looper_thread_, __COUNTER__)(::fn::looperTaskId(id), data, [](data_arg) { LOOPER_THREAD_BODY(body) });

// Перезапустить поток с начала.
#define LOOPER_THREAD_RESTART() \
    do {                        \
        LOOPER_THREAD_RESET();  \
        return;                 \
    } while (0);

// Приостановить поток и продолжить со следующей строки.
#define LOOPER_THREAD_SKIP()                    \
    do {                                        \
        LOOPER_THREAD_CASE = __COUNTER__ + 1;   \
        return;                                 \
        case __COUNTER__:;                      \
    } while (0);

// Выйти из потока и вернуться на следующую строку.
#define LOOPER_THREAD_EXIT() LOOPER_THREAD_SKIP()

// Асинхронно ждать условие.
#define LOOPER_WAIT(condition)                  \
    do {                                        \
        LOOPER_THREAD_CASE = __COUNTER__ + 1;   \
        LOOPER_FALLTHROUGH;                     \
        case __COUNTER__:                       \
            if (!(condition)) return;            \
    } while (0);

// Асинхронно ждать события.
#define LOOPER_WAIT_EVENT() LOOPER_WAIT(::fn::Looper.isInEvent());

// Асинхронно ждать время, мс.
#define LOOPER_DELAY(ms)                                              \
    do {                                                              \
        ::fn::Looper.currentThread()->timer.start(ms);                 \
        LOOPER_WAIT(::fn::Looper.currentThread()->timer.expire());     \
    } while (0);

// Освободить семафор.
#define LOOPER_SEMAPHORE_SIGNAL(semaphore) semaphore++;

// Ждать семафор.
#define LOOPER_SEMAPHORE_WAIT(semaphore) \
    do {                                 \
        LOOPER_WAIT(semaphore);          \
        semaphore--;                     \
    } while (0);

// Тип семафора.
#define LOOPER_SEMAPHORE uint16_t

#endif

#pragma once
// Кооперативный планировщик задач: периодические функции по millis().
// Без динамической памяти и без прерываний — всё в одном run().

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC && F_USE_TASK

#include "../timer/Tick.h"

namespace fn {
namespace logic {

using TaskFn = void (*)();

// Слоты стабильны: id, полученный из add(), не меняется после remove().
// interval == 0 означает «выполнять при каждом run()».
template <uint8_t MaxTasks = 8>
class Scheduler {
    static_assert(MaxTasks > 0 && MaxTasks <= 127, "Scheduler: 1..127 tasks");

   public:
    struct Task {
        TaskFn   fn = nullptr;
        uint32_t interval = 0;
        uint32_t last = 0;
        bool     enabled = false;
        bool     used = false;
    };

    // Возвращает id (0..MaxTasks-1) или -1, если нет свободного слота.
    int8_t add(TaskFn fn, uint32_t intervalMs, bool runNow = false) {
        if (!fn) return -1;
        for (uint8_t i = 0; i < MaxTasks; i++) {
            if (_tasks[i].used) continue;
            _tasks[i].fn = fn;
            _tasks[i].interval = intervalMs;
            _tasks[i].enabled = true;
            _tasks[i].used = true;
            _tasks[i].last = runNow ? uint32_t(fn::millis() - intervalMs)
                                    : fn::millis();
            return int8_t(i);
        }
        return -1;
    }

    bool remove(int8_t id) {
        Task* t = slot(id);
        if (!t) return false;
        t->used = false;
        t->enabled = false;
        t->fn = nullptr;
        return true;
    }

    bool setInterval(int8_t id, uint32_t intervalMs) {
        Task* t = slot(id);
        if (!t) return false;
        t->interval = intervalMs;
        return true;
    }

    bool enable(int8_t id, bool on) {
        Task* t = slot(id);
        if (!t) return false;
        t->enabled = on;
        if (on) t->last = fn::millis();
        return true;
    }

    bool isEnabled(int8_t id) const {
        const Task* t = slot(id);
        return t && t->enabled;
    }

    int8_t find(TaskFn fn) const {
        if (!fn) return -1;
        for (uint8_t i = 0; i < MaxTasks; i++)
            if (_tasks[i].used && _tasks[i].fn == fn) return int8_t(i);
        return -1;
    }

    // Выполняет все задачи, у которых истёк интервал. now берётся один раз.
    void run() {
        uint32_t now = fn::millis();
        for (uint8_t i = 0; i < MaxTasks; i++) {
            Task& t = _tasks[i];
            if (!t.used || !t.enabled || !t.fn) continue;
            if (uint32_t(now - t.last) >= t.interval) {
                t.last = now;
                t.fn();
            }
        }
    }

    // Сколько мс до ближайшей задачи: 0 — уже пора, 0xFFFFFFFF — задач нет.
    uint32_t nextIn() const {
        uint32_t now = fn::millis();
        uint32_t best = 0xFFFFFFFFUL;
        for (uint8_t i = 0; i < MaxTasks; i++) {
            const Task& t = _tasks[i];
            if (!t.used || !t.enabled || !t.fn) continue;
            uint32_t elapsed = uint32_t(now - t.last);
            uint32_t remain = (elapsed >= t.interval) ? 0 : (t.interval - elapsed);
            if (remain < best) best = remain;
        }
        return best;
    }

    uint8_t count() const {
        uint8_t n = 0;
        for (uint8_t i = 0; i < MaxTasks; i++) if (_tasks[i].used) n++;
        return n;
    }

    const Task* task(int8_t id) const { return slot(id); }

    void clear() {
        for (uint8_t i = 0; i < MaxTasks; i++) {
            _tasks[i].used = false;
            _tasks[i].enabled = false;
            _tasks[i].fn = nullptr;
        }
    }

    // Сдвинуть все таймеры на «сейчас» (например, после долгой блокировки).
    void reset() {
        uint32_t now = fn::millis();
        for (uint8_t i = 0; i < MaxTasks; i++)
            if (_tasks[i].used) _tasks[i].last = now;
    }

   private:
    Task* slot(int8_t id) {
        if (id < 0 || uint8_t(id) >= MaxTasks) return nullptr;
        Task& t = _tasks[uint8_t(id)];
        return t.used ? &t : nullptr;
    }
    const Task* slot(int8_t id) const {
        if (id < 0 || uint8_t(id) >= MaxTasks) return nullptr;
        const Task& t = _tasks[uint8_t(id)];
        return t.used ? &t : nullptr;
    }

    Task _tasks[MaxTasks] = {};
};

template <uint8_t MaxTasks = 8>
using TaskScheduler = Scheduler<MaxTasks>;

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC && F_USE_TASK

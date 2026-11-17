#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

namespace fn {

// Миллисекундный интервальный таймер.
class IntervalTimer {
   public:
    IntervalTimer(uint32_t ms = 0, bool start = true);

    // Перезапустить таймер с текущим периодом.
    void restart();

    // Запустить таймер с новым периодом.
    // Период складывается из миллисекунд, секунд, минут, часов и суток.
    void start(uint32_t ms, uint32_t seconds = 0, uint16_t minutes = 0,
               uint16_t hours = 0, uint16_t days = 0);

    // Остановить таймер.
    void stop();

    // Форсировать срабатывание при следующей проверке.
    void forceExpire();

    // Таймер запущен.
    bool isRunning();

    // Период истёк (проверка без изменения состояния).
    bool isReady();

    explicit operator bool() { return isReady(); }

    // Период таймера, мс.
    uint32_t period();

    // Период истёк: перезапустить и вернуть true.
    bool expireAndRestart();

    // Период истёк: остановить и вернуть true.
    bool expire();

    // Сколько осталось до срабатывания, мс. 0 — таймер остановлен или истёк.
    uint32_t remaining();

   private:
    uint32_t _startedAt = 0;
    uint32_t _period = 0;
};

}  // namespace fn

#endif

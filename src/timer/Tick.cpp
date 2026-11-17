// ============================================================================
//  Tick.cpp — точная микросекундная задержка и заявка на Timer0
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация delayUsPrecise и фиксация того, что Timer0 принадлежит ядру
//    Arduino. База времени (millis/micros) живёт в ядре, своих ISR здесь нет.
//
//  КАК СЧИТАЕТСЯ ЗАДЕРЖКА
//    _delay_loop_2(n) выполняет ровно 4*n тактов (n=0 даёт 65536 итераций,
//    поэтому нулевой аргумент недопустим). Итераций на микросекунду:
//    F_CPU/4e6 = 4 при 16 МГц, 2 при 8 МГц.
//    Накладные расходы внешнего цикла (decrement, compare, branch) ≈ 12 тактов
//    на chunk; они вычитаются из бюджета, чтобы длинная задержка не уплывала.
//    Прерывания запрещены на всё время: иначе ISR удлинит задержку непредсказуемо.
//
//  ОГРАНИЧЕНИЯ
//    - us == 0: немедленный возврат, без обращения к _delay_loop_2.
//    - Максимум chunk = 1000 мкс: при 16 МГц это 4000 итераций, влезает в
//      uint16 аргумента _delay_loop_2 (лимит 65535).
//    - Погрешность ±1 мкс на chunk плюс тактовая погрешность кварца/RC.
//      Внутренний RC-генератор Nano даёт до ±1% — это 10 мкс на 1 мс.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Tick.h, util/delay_basic.h, avr/interrupt.h
// ============================================================================

#include "../core/Config.h"
#include "Tick.h"

#include <util/delay_basic.h>
#include <avr/interrupt.h>

// Timer0 принадлежит ядру Arduino: millis/micros/delay. Если другой модуль
// заявит FN_CLAIM_TIMER(0, ...) — static_assert упадёт на компиляции.
FN_CLAIM_TIMER(0, FN_OWNER_ARDUINO);
FN_CLAIM_VECTOR(Timer0Ovf, FN_OWNER_ARDUINO);

namespace fn {

void delayUsPrecise(uint16_t us) {
    if (us == 0) return;

    // Итераций _delay_loop_2 на одну микросекунду.
    constexpr uint16_t kIterPerUs = uint16_t(F_CPU / 4000000UL);
    // Накладные расходы цикла на один chunk, в тактах.
    constexpr uint16_t kOverheadCycles = 12;
    constexpr uint16_t kChunkUs = 1000;

    AtomicBlock lock;

    uint16_t left = us;
    while (left > 0) {
        const uint16_t chunk = (left > kChunkUs) ? kChunkUs : left;
        left = uint16_t(left - chunk);

        uint32_t cycles = uint32_t(chunk) * (F_CPU / 1000000UL);
        if (cycles > kOverheadCycles) {
            cycles -= kOverheadCycles;
        }
        uint32_t iter = cycles / 4UL;
        if (iter == 0) iter = 1;
        if (iter > 0xFFFFUL) iter = 0xFFFFUL;

        _delay_loop_2(uint16_t(iter));
    }
}

}  // namespace fn

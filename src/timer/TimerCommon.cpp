// ============================================================================
//  TimerCommon.cpp — общий реестр владельцев таймеров
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация tmr::acquire/release/ownerOf (D69). Три байта SRAM: по одному
//    на таймер. Регистров не трогает — арбитраж advisory, ответственность за
//    конфигурацию несёт потребитель.
//
//  ОГРАНИЧЕНИЯ
//    - Advisory: прямой вызов TimerN::takeover мимо acquire конфликт не поймает.
//    - Timer0 принадлежит ядру Arduino; захватывать его можно только осознанно.
//    - Файл должен лежать в src/timer/ (формат библиотеки 1.5).
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 3 байта. Flash: ~60 байт.
//
//  ЗАВИСИМОСТИ: TimerCommon.h, core/Atomic.h
// ============================================================================

#include "../core/Config.h"
#include "TimerCommon.h"

namespace fn {
namespace tmr {
namespace detail {
uint8_t owner[3] = { 0u, 0u, 0u };
}  // namespace detail

Result acquire(uint8_t n, Client c) {
    if (n >= 3u || c == Client::None) return Result::BadArg;
    AtomicBlock lock;
    const uint8_t cur = detail::owner[n];
    if (cur != 0u && cur != static_cast<uint8_t>(c)) return Result::Busy;
    detail::owner[n] = static_cast<uint8_t>(c);
    return Result::Ok;
}

void release(uint8_t n, Client c) {
    if (n >= 3u) return;
    AtomicBlock lock;
    if (detail::owner[n] == static_cast<uint8_t>(c)) detail::owner[n] = 0u;
}

Client ownerOf(uint8_t n) {
    if (n >= 3u) return Client::None;
    AtomicBlock lock;
    return static_cast<Client>(detail::owner[n]);
}

}  // namespace tmr
}  // namespace fn
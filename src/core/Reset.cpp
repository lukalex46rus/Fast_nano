// ============================================================================
//  Reset.cpp — причина сброса и управление перезапуском
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация Reset поверх снимка MCUSR из fn::crash. Сами примитивы
//    останова и сброса живут в Crash — здесь только прикладная обёртка,
//    чтобы не дублировать работу с WDT в двух модулях.
//
//  ОГРАНИЧЕНИЯ
//    - Порядок разбора причин: WDRF > BORF > EXTRF > PORF. После аппаратного
//      сброса могут быть выставлены несколько флагов одновременно.
//    - toBootloader() делает косвенный переход по словесному адресу: EIND
//      на 328p отсутствует, flash 32 КБ, поэтому достаточно uint16_t.
//
//  ЗАВИСИМОСТИ: Reset.h, Crash.h, Atomic.h, avr/io.h, avr/wdt.h
// ============================================================================

#include "Reset.h"
#include "Atomic.h"

#include <avr/io.h>
#include <avr/wdt.h>

namespace fn {

uint8_t Reset::raw() { return crash::resetFlags(); }

uint8_t Reset::flags() {
    const uint8_t m = crash::resetFlags();
    uint8_t f = 0;
    if (m & (1u << PORF)) f |= FlagPowerOn;
    if (m & (1u << EXTRF)) f |= FlagExternal;
    if (m & (1u << BORF)) f |= FlagBrownOut;
    if (m & (1u << WDRF)) f |= FlagWatchdog;
    return f;
}

Reset::Cause Reset::cause() {
    const uint8_t f = flags();
    if (f & FlagWatchdog) return Cause::Watchdog;
    if (f & FlagBrownOut) return Cause::BrownOut;
    if (f & FlagExternal) return Cause::External;
    if (f & FlagPowerOn) return Cause::PowerOn;
    return Cause::Unknown;
}

bool Reset::isWatchdog() { return (flags() & FlagWatchdog) != 0; }

uint16_t Reset::boots() { return crash::boots(); }

PGM_P Reset::causeName() {
    static const char sPor[] PROGMEM = "PowerOn";
    static const char sExt[] PROGMEM = "External";
    static const char sBor[] PROGMEM = "BrownOut";
    static const char sWdt[] PROGMEM = "Watchdog";
    static const char sUnk[] PROGMEM = "Unknown";

    switch (cause()) {
        case Cause::PowerOn:  return sPor;
        case Cause::External: return sExt;
        case Cause::BrownOut: return sBor;
        case Cause::Watchdog: return sWdt;
        default:              return sUnk;
    }
}

void Reset::clear() {
    AtomicBlock lock;
    MCUSR = 0;
    crash::clearResetFlags();
}

void Reset::soft() { crash::forceReset(); }

void Reset::halt() { crash::halt(); }

void Reset::toBootloader(uint16_t wordAddr) {
    cli();
    wdt_disable();
    // Гасим то, что может дёрнуть загрузчик: таймеры и USART.
    TIMSK0 = 0;
    TIMSK1 = 0;
    TIMSK2 = 0;
    UCSR0B = 0;
    reinterpret_cast<void (*)()>(wordAddr)();
    __builtin_unreachable();
}

}  // namespace fn

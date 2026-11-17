#pragma once
// ============================================================================
//  Reset.h — причина сброса, перезапуск, останов, переход в загрузчик
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Прикладной интерфейс поверх fn::crash. Причина сброса берётся из снимка
//    MCUSR, сделанного в .init3 до main() и до конструкторов, — а не из живого
//    регистра, который к моменту setup() может быть уже затёрт.
//
//  API (namespace fn, класс Reset — только статические методы)
//    enum class Cause : uint8_t { PowerOn, External, BrownOut, Watchdog, Unknown }
//    enum Flag { FlagPowerOn = 1, FlagExternal = 2, FlagBrownOut = 4, FlagWatchdog = 8 }
//
//    cause()      -> Cause     доминирующая причина (WDT > BOR > EXT > POR)
//    flags()      -> uint8_t   маска всех выставленных причин
//    raw()        -> uint8_t   сырой снимок MCUSR
//    causeName()  -> PGM_P     имя причины во flash, для лога
//    isWatchdog() -> bool
//    boots()      -> uint16_t  номер загрузки (1 = первое включение)
//    clear()                   обнулить причины в снимке и в MCUSR
//
//    soft()                    аппаратный сброс через WDT 15 мс
//    halt()                    останов с выключенным WDT
//    toBootloader(wordAddr = 0x3F00)
//                              переход в загрузчик по словесному адресу
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - soft() — настоящий сброс: периферия сбрасывается, MCUSR получает WDRF.
//      Прежняя реализация через jmp 0 сбрасывала только ядро программы:
//      таймеры, UART и WDT оставались настроенными. Отличить программный
//      сброс от зависания можно по fn::crash::code().
//    - halt() выключает WDT. При прошитом фьюзе WDTON выключить его нельзя —
//      произойдёт перезапуск. Это ограничение железа, не кода.
//    - toBootloader(): адрес ЗАВИСИТ от фьюзов BOOTSZ. Значение по умолчанию
//      0x3F00 (байтовый 0x7E00) соответствует типовому Optiboot на Uno/Nano.
//      Переход не является сбросом: периферия сохраняет состояние, поэтому
//      перед вызовом модуль гасит прерывания и WDT.
//    - Загрузчик может сам затирать MCUSR. Снимок делается до его передачи
//      управления только если загрузчик не трогает регистр; на старых
//      версиях Optiboot EXTRF бывает выставлен всегда.
//    - clear() не сбрасывает счётчик загрузок.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM (состояние живёт в записи fn::crash).
//    causeName() — 5 строк во flash, вырезается, если не вызывается.
//
//  ПРИМЕР
//    void setup() {
//        Uart::begin(9600);
//        Uart::print(F("reset: "));
//        Uart::println(FPSTR(fn::Reset::causeName()));
//        Uart::print(F("boot #")); Uart::println(fn::Reset::boots());
//        if (fn::Reset::isWatchdog()) Uart::println(F("watchdog!"));
//    }
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, Crash.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "Crash.h"

#include <stdint.h>
#include <avr/pgmspace.h>

#if F_USE_RESET

namespace fn {

class Reset {
public:
    enum class Cause : uint8_t {
        PowerOn = 0,
        External = 1,
        BrownOut = 2,
        Watchdog = 3,
        Unknown = 4
    };

    enum Flag : uint8_t {
        FlagPowerOn = 0x01,   ///< PORF
        FlagExternal = 0x02,  ///< EXTRF
        FlagBrownOut = 0x04,  ///< BORF
        FlagWatchdog = 0x08   ///< WDRF
    };

    static Cause cause();
    static uint8_t flags();
    static uint8_t raw();
    static PGM_P causeName();
    static bool isWatchdog();
    static uint16_t boots();
    static void clear();

    [[noreturn]] static void soft();
    [[noreturn]] static void halt();
    [[noreturn]] static void toBootloader(uint16_t wordAddr = 0x3F00u);

private:
    Reset() = delete;
};

}  // namespace fn

#endif  // F_USE_RESET

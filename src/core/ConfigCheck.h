#pragma once
// ============================================================================
//  ConfigCheck.h — проверка целевой платформы и среды сборки
//  Проект: Fast_nano (namespace fn)
//  Цель:   ATmega328P / ATmega328 (Nano / Uno / Pro Mini)
//  Среда:  Arduino IDE 2.3.x, avr-gcc 7.3.0, -std=gnu++11, -Os, LTO
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственная точка проверки «тот ли МК и та ли среда». Подключается
//    через Config.h первым include в каждом заголовке модуля, а не только
//    в .cpp: иначе заголовок соберётся на чужом МК без предупреждения.
//
//  ПРОВЕРКИ
//    1. МК: __AVR_ATmega328P__ или __AVR_ATmega328__ (задаются -mmcu).
//       328PB, 328PA, 168, 2560 и прочие — #error.
//    2. Язык: C++11 или новее (__cplusplus >= 201103L).
//    3. Компилятор: avr-gcc >= 5 (рекурсивный constexpr, alias-шаблоны).
//    4. Исключения: -fno-exceptions обязателен (RAII-блокировки, размер кода).
//    5. Оптимизация: -O0 ломает тайминги и атомарность -> #warning.
//    6. F_CPU: должен быть задан; #warning, если не 16 или 8 МГц.
//
//  ОПРЕДЕЛЯЕМЫЕ КОНСТАНТЫ
//    FN_MCU_NAME     "ATmega328P" / "ATmega328"
//    FN_RAM_SIZE     2048   байт SRAM
//    FN_FLASH_SIZE   32768  байт flash
//    FN_EEPROM_SIZE  1024   байт EEPROM
//    FN_SRAM_START   0x0100 первый адрес SRAM
//    FN_SRAM_END     0x08FF последний адрес SRAM
//    FN_PORT_COUNT   3      порты B, C, D
//
//  ОГРАНИЧЕНИЯ
//    - Файл не включает ничего: все проверяемые макросы предопределены
//      компилятором либо заданы в командной строке (-mmcu, -DF_CPU, -std).
//    - F_CPU должен быть известен ДО включения файла. В Arduino его задаёт
//      ядро через -DF_CPU=16000000L.
//    - Проверка раскладки SFR здесь невозможна: &PORTB — это reinterpret_cast
//      из целого, в constant expression он не допускается. Раскладка
//      проверяется в Registers.h отдельным способом.
//
//  ПРИМЕР
//    #include "Config.h"
//    static_assert(fn::RamSize == 2048, "нужен МК с 2 КБ SRAM");
//
//  ЗАВИСИМОСТИ: нет
// ============================================================================

// ------------------------------- целевой МК --------------------------------
#if !defined(__AVR_ATmega328P__) && !defined(__AVR_ATmega328__)
#error "Fast_nano: поддерживаются только ATmega328P / ATmega328 (Nano/Uno/Pro Mini)."
#endif

#if defined(__AVR_ATmega328P__)
#define FN_MCU_NAME "ATmega328P"
#else
#define FN_MCU_NAME "ATmega328"
#endif

// ------------------------------- язык и тулчейн ----------------------------
#if !defined(__cplusplus) || (__cplusplus < 201103L)
#error "Fast_nano: нужен C++11 (Arduino AVR core: -std=gnu++11)."
#endif

#if !defined(__GNUC__)
#error "Fast_nano: поддерживается только avr-gcc."
#elif (__GNUC__ < 5)
#warning "Fast_nano: avr-gcc старше 5.x не тестировался (ожидался 7.3.0)."
#endif

#if defined(__EXCEPTIONS)
#error "Fast_nano: соберите с -fno-exceptions (RAII-блокировки и размер кода)."
#endif

#if !defined(__OPTIMIZE__)
#warning "Fast_nano: сборка без оптимизации (-O0) ломает тайминги и атомарность."
#endif

// ------------------------------- тактовая частота --------------------------
#if !defined(F_CPU)
#error "Fast_nano: F_CPU не определён (ожидается -DF_CPU=16000000L)."
#endif

#if (F_CPU != 16000000UL) && (F_CPU != 8000000UL)
#warning "Fast_nano: модули рассчитаны на 16 или 8 МГц; тайминги могут уйти."
#endif

// ------------------------------- бюджет памяти -----------------------------
#define FN_RAM_SIZE 2048u
#define FN_FLASH_SIZE 32768u
#define FN_EEPROM_SIZE 1024u
#define FN_SRAM_START 0x0100u
#define FN_SRAM_END 0x08FFu
#define FN_PORT_COUNT 3u

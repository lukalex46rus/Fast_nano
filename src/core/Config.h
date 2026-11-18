#pragma once
// ============================================================================
//  Config.h — единая точка входа в конфигурацию Fast_nano
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Обязательный первый include каждого заголовка модуля. Гарантирует, что
//    к моменту компиляции модуля:
//      1) проверен целевой МК и среда сборки   (ConfigCheck.h);
//      2) существует каждый флаг F_USE_*       (Features.h);
//      3) известны бюджеты памяти и версия     (константы ниже).
//    Макросы компилятора подключаются отдельно через Compiler.h — так
//    заголовки чистого препроцессора не тянут лишних определений.
//
//  API (namespace fn)
//    Version      uint16_t  версия библиотеки, 200 = 2.00
//    RamSize      uint16_t  2048   байт SRAM
//    FlashSize    uint16_t  32768  байт flash
//    EepromSize   uint16_t  1024   байт EEPROM
//    SramStart    uint16_t  0x0100 первый адрес SRAM
//    SramEnd      uint16_t  0x08FF последний адрес SRAM
//    PortCount    uint8_t   3      порты B, C, D
//    Debug        bool      признак отладочной сборки (FN_DEBUG)
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - Файл не содержит исполняемого кода и не занимает flash/RAM.
//    - static_assert-ы ловят несоответствие разрядностей: на AVR указатель
//      и int обязаны быть 16-битными, long — 32-битными.
//    - Имя Config.h распространённое; подключать только кавычками ("Config.h"),
//      чтобы брался файл проекта, а не чужой из include-пути.
//    - Отключение модулей — только централизованно, см. шапку Features.h.
//
//  ПРИМЕР (шаблон заголовка модуля)
//    #pragma once
//    #include "Config.h"
//    #if F_USE_CRC
//    namespace fn { namespace crc { /* ... */ } }
//    #endif
//
//  ЗАВИСИМОСТИ: ConfigCheck.h, Features.h, stdint.h, stddef.h, limits.h
// ============================================================================

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#include "ConfigCheck.h"
#include "Features.h"

namespace fn {

constexpr uint16_t Version    = FN_VERSION;
constexpr uint16_t RamSize    = FN_RAM_SIZE;
constexpr uint16_t FlashSize  = FN_FLASH_SIZE;
constexpr uint16_t EepromSize = FN_EEPROM_SIZE;
constexpr uint16_t SramStart  = FN_SRAM_START;
constexpr uint16_t SramEnd    = FN_SRAM_END;
constexpr uint8_t  PortCount  = FN_PORT_COUNT;
constexpr bool     Debug      = (FN_DEBUG != 0);

static_assert(CHAR_BIT == 8, "fn: ожидается 8-битный байт");
static_assert(sizeof(void*) == 2, "fn: ожидается 16-битный указатель (AVR)");
static_assert(sizeof(int) == 2, "fn: ожидается 16-битный int (AVR)");
static_assert(sizeof(long) == 4, "fn: ожидается 32-битный long (AVR)");
static_assert(sizeof(size_t) == 2, "fn: ожидается 16-битный size_t (AVR)");

}  // namespace fn

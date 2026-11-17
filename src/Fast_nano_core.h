#pragma once
// ============================================================================
//  Fast_nano_core.h — минимальный umbrella: только базовый слой
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Для скетчей, которым не нужны шины, дисплеи, моторы и логика.
//    Подключает только фундамент: конфигурацию, макросы, регистры,
//    атомарность, память, битовые типы, CRC, PROGMEM, сброс, ассерты.
//    Существенно быстрее компилируется и не тянет глобальные объекты
//    прикладных модулей.
//
//  API
//    Всё из блока core библиотеки fn. Прикладные слои (pin/bus/logic/
//    display/motor) здесь НЕ доступны — подключайте их заголовки точечно
//    или используйте Fast_nano.h.
//
//  ОГРАНИЧЕНИЯ
//    - Arduino.h включается: часть core (PgmUtils) зависит от Print/Printable.
//    - Отключение модулей — централизованно, см. Features.h.
//    - Взаимоисключаемо с Fast_nano.h по смыслу, но не по механике:
//      оба можно включить, #pragma once защитит от повторного разбора.
//
//  ПРИМЕР
//    #include <Fast_nano_core.h>
//    fn::BitFlags8 st;
//    void setup() { st.set(0x01); }
//    void loop()  { if (st.isSet(0x01)) fn::PortB::toggle(fn::bitMask8(5)); }
//
//  ЗАВИСИМОСТИ: core/*
// ============================================================================

#include "core/Config.h"
#include <Arduino.h>

#include "core/Compiler.h"
#include "core/Types.h"
#include "core/Crash.h"
#include "core/ForMacro.h"
#if F_USE_ASSERT
#include "core/Assert.h"
#endif

#include "core/Registers.h"
#include "core/Atomic.h"
#include "core/Memory.h"
#include "core/Bits.h"        // тянет BitSet и BitReverse
#include "core/Crc.h"
#include "core/Progmem.h"     // внутри PGM-утилиты при F_USE_PGM_UTILS
#include "core/Reset.h"

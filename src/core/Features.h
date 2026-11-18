#pragma once
// ============================================================================
//  Features.h — карта модулей Fast_nano
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Один флаг на модуль: 1 — компилируется, 0 — заголовок становится пустышкой.
//    Позволяет собрать прошивку только из нужного.
//
//  КАК ОТКЛЮЧАТЬ (ВАЖНО)
//    Правильно:
//      1) поменять дефолт прямо в этом файле, либо
//      2) задать -DF_USE_X=0 для ВСЕХ единиц трансляции
//         (Arduino: platform.local.txt, compiler.cpp.extra_flags).
//    НЕПРАВИЛЬНО:
//      #define F_USE_X 0 перед #include в одном .cpp/.ino.
//      Каждый .cpp — отдельная единица трансляции. Разные флаги в разных
//      файлах дают разные определения одних и тех же inline-функций и
//      классов -> нарушение ODR. Компилятор молчит, линкер берёт случайное.
//
//  API
//    FN_DEBUG        1 — отладочная сборка (ассерты включены), 0 — релиз.
//    FN_VERSION      версия библиотеки, 200 = 2.00.
//    F_USE_<МОДУЛЬ>  0/1, список ниже.
//
//  ВСЕГДА ВКЛЮЧЁННЫЕ МОДУЛИ (флагов нет намеренно)
//    ConfigCheck, Config, Compiler, Types, Crash, ForMacro.
//    Это фундамент: его отключение ломает остальные модули. Стоимость —
//    единицы байт RAM и десятки байт flash, экономить здесь нечего.
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - Все флаги определяются через #ifndef: после включения файла любой
//      F_USE_* гарантированно существует. Модуль, проверяющий флаг, ОБЯЗАН
//      включить Config.h.
//    - Включённый флаг != нулевая стоимость. Шаблоны и inline вырезаются
//      линкером, если не используются, но глобальные объекты, vtable,
//      static-буферы и PROGMEM-таблицы остаются всегда.
//    - Зависимости проверяются в конце файла через #error, а не молчаливым
//      обнулением: несовместимая конфигурация не должна собираться.
//    - Префикс F_USE_ исторический; новые макросы библиотеки идут с FN_.
//    - Зачаточные слои L7/L9/L10 закрыты по DEP-9: их API не согласован с контрактами
//      (TMC2209 требует несуществующий bus/Bus.h и тип BusError; MCP23017/Lcd1602I2c
//      вызывают I2c::write(byte) вместо write(ptr,len)). Открывать после этапа 3.
//
//  ПРИМЕР (релиз: минимум диагностики и тяжёлых модулей)
//    #define FN_DEBUG 0
//    #define F_USE_OLED 0
//    #define F_USE_GFX  0
//    #define F_USE_MENU 0
//
//  ЗАВИСИМОСТИ: нет (чистый препроцессор)
// ============================================================================

#define FN_VERSION 200u  // 2.00

// ------------------------------ режим сборки -------------------------------
#ifndef FN_DEBUG
#define FN_DEBUG 1
#endif

// ------------------------------ ядро ---------------------------------------
#ifndef F_USE_BIT_OPS
#define F_USE_BIT_OPS 1
#endif
#ifndef F_USE_BIT_PACK
#define F_USE_BIT_PACK 1
#endif
#ifndef F_USE_BIT_SET
#define F_USE_BIT_SET 1
#endif
#ifndef F_USE_BIT_FLAGS
#define F_USE_BIT_FLAGS 1
#endif
#ifndef F_USE_BIT_REVERSE
#define F_USE_BIT_REVERSE 1
#endif
#ifndef F_USE_CRC
#define F_USE_CRC 1
#endif
#ifndef F_USE_ATOMIC
#define F_USE_ATOMIC 1
#endif
#ifndef F_USE_MEMORY
#define F_USE_MEMORY 1
#endif
#ifndef F_USE_RESET
#define F_USE_RESET 1
#endif
#ifndef F_USE_REGISTERS
#define F_USE_REGISTERS 1
#endif

// ------------------------------ прерывания и таймеры -----------------------
#ifndef F_USE_IRQ
#define F_USE_IRQ 1
#endif
#ifndef F_USE_TIMER0
#define F_USE_TIMER0 1
#endif
#ifndef F_USE_TIMER1
#define F_USE_TIMER1 1
#endif
#ifndef F_USE_TIMER2
#define F_USE_TIMER2 1
#endif
#ifndef F_USE_TICK
#define F_USE_TICK 1
#endif
#ifndef F_USE_TIMER_ARBITER
#define F_USE_TIMER_ARBITER 1
#endif

// ------------------------------ периферия ----------------------------------
#ifndef F_USE_UART
#define F_USE_UART 1
#endif
#ifndef F_USE_SPI
#define F_USE_SPI 1
#endif
#ifndef F_USE_I2C
#define F_USE_I2C 1
#endif
#ifndef F_USE_ADC
#define F_USE_ADC 1
#endif
#ifndef F_USE_AC
#define F_USE_AC 1
#endif
#ifndef F_USE_PWM
#define F_USE_PWM 1
#endif
#ifndef F_USE_EEPROM
#define F_USE_EEPROM 1
#endif
#ifndef F_USE_WDT
#define F_USE_WDT 1
#endif
#ifndef F_USE_PULSE
#define F_USE_PULSE 1
#endif
#ifndef F_USE_LOGIC
#define F_USE_LOGIC 1
#endif
#ifndef F_USE_POWER
#define F_USE_POWER 1
#endif
#ifndef F_USE_SERVO
#define F_USE_SERVO 1
#endif
#ifndef F_USE_LCD
#define F_USE_LCD 0
#endif
#ifndef F_USE_STEPPER
#define F_USE_STEPPER 0
#endif
#ifndef F_USE_KEYPAD
#define F_USE_KEYPAD 1
#endif
#ifndef F_USE_OLED
#define F_USE_OLED 0
#endif
#ifndef F_USE_GFX
#define F_USE_GFX 1
#endif
#ifndef F_USE_CHAR
#define F_USE_CHAR 1
#endif

// ------------------------------ логика и сервис ----------------------------
#ifndef F_USE_TASK
#define F_USE_TASK 1
#endif
#ifndef F_USE_LOOPER
#define F_USE_LOOPER 1
#endif
#ifndef F_USE_BUFFER
#define F_USE_BUFFER 1
#endif
#ifndef F_USE_PULSE_IN
#define F_USE_PULSE_IN 1
#endif
#ifndef F_USE_STATE
#define F_USE_STATE 1
#endif
#ifndef F_USE_ASSERT
#define F_USE_ASSERT FN_DEBUG
#endif
#ifndef F_USE_PACKET
#define F_USE_PACKET 1
#endif
#ifndef F_USE_KALMAN
#define F_USE_KALMAN 1
#endif
#ifndef F_USE_QUAD
#define F_USE_QUAD 1
#endif
#ifndef F_USE_PROGMEM
#define F_USE_PROGMEM 1
#endif
#ifndef F_USE_PID_AUTOTUNE
#define F_USE_PID_AUTOTUNE 1
#endif
#ifndef F_USE_MENU
#define F_USE_MENU 0
#endif
#ifndef F_USE_STRINGN
#define F_USE_STRINGN 1
#endif
#ifndef F_USE_PGM_UTILS
#define F_USE_PGM_UTILS 1
#endif

// ------------------------------ драйверы и математика ----------------------
#ifndef F_USE_DRV8825
#define F_USE_DRV8825 0
#endif
#ifndef F_USE_TMC2209
#define F_USE_TMC2209 0
#endif
#ifndef F_USE_MCP23017
#define F_USE_MCP23017 0
#endif
#ifndef F_USE_MATH
#define F_USE_MATH 1
#endif
#ifndef F_USE_FIXED_POINT
#define F_USE_FIXED_POINT 1
#endif
#ifndef F_USE_ANY
#define F_USE_ANY 1
#endif

// --------------------------- проверка зависимостей -------------------------
#if F_USE_PGM_UTILS && !F_USE_PROGMEM
#error "F_USE_PGM_UTILS требует F_USE_PROGMEM=1"
#endif

#if F_USE_BIT_SET && !F_USE_BIT_PACK
#error "F_USE_BIT_SET требует F_USE_BIT_PACK=1 (BitSet наследует BitPack)"
#endif

#if F_USE_DRV8825 && !F_USE_STEPPER
#error "F_USE_DRV8825 требует F_USE_STEPPER=1"
#endif

#if F_USE_TMC2209 && !(F_USE_STEPPER && F_USE_UART)
#error "F_USE_TMC2209 требует F_USE_STEPPER=1 и F_USE_UART=1"
#endif

#if F_USE_MCP23017 && !F_USE_I2C
#error "F_USE_MCP23017 требует F_USE_I2C=1"
#endif

#if F_USE_PID_AUTOTUNE && !F_USE_LOGIC
#error "F_USE_PID_AUTOTUNE требует F_USE_LOGIC=1 (нужен logic/Pid.h)"
#endif

#if F_USE_QUAD && !F_USE_IRQ
#error "F_USE_QUAD требует F_USE_IRQ=1 (нужен диспетчер PCINT)"
#endif

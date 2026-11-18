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
//  СОСТОЯНИЕ СЛОЁВ (DEP-9, см. docs/ARCHITECTURE.md)
//    Готовы и включены: L0–L6.2 (core/pin/timer/PWM/bus/store/peripheral) + L8
//    (string/math). Зачаточные L7 (logic), L9 (display), L10 (device/motor/input)
//    ОТКЛЮЧЕНЫ: их API не согласован с контрактами pin/timer/шин —
//      * Pin<N> вместо BoardPin<N> (fn::Pin — дескриптор, не шаблон);
//      * PinRef(n, uint8_t) вместо PinRef(n, Mode);
//      * Timer2::Mode::CTC / Timer2::Clock вместо Mode::Ctc / Clock2;
//      * I2c::write(byte) вместо write(ptr, len);
//      * TMC2209 требует несуществующий bus/Bus.h и тип BusError.
//    Открывать по одному модулю после миграции этапа 3 (docs/ROADMAP.md, G-10…G-12).
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
//
//  ПРИМЕР (релиз: минимум диагностики)
//    #define FN_DEBUG 0
//
//  ЗАВИСИМОСТИ: нет (чистый препроцессор)
// ============================================================================

#define FN_VERSION 200u  // 2.00

// ------------------------------ режим сборки -------------------------------
#ifndef FN_DEBUG
#define FN_DEBUG 1
#endif

// ============================ L0–L3: ЯДРО ===================================
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
#ifndef F_USE_PROGMEM
#define F_USE_PROGMEM 1
#endif
#ifndef F_USE_PGM_UTILS
#define F_USE_PGM_UTILS 1
#endif

// ============================ L1: ДОСТУП К ЖЕЛЕЗУ ===========================
#ifndef F_USE_ATOMIC
#define F_USE_ATOMIC 1
#endif
#ifndef F_USE_REGISTERS
#define F_USE_REGISTERS 1
#endif
#ifndef F_USE_MEMORY
#define F_USE_MEMORY 1
#endif
#ifndef F_USE_RESET
#define F_USE_RESET 1
#endif
#ifndef F_USE_ASSERT
#define F_USE_ASSERT FN_DEBUG
#endif

// ============================ L4: ПРЕРЫВАНИЯ ================================
#ifndef F_USE_IRQ
#define F_USE_IRQ 1
#endif

// ============================ L5.1: ПИНЫ / ВВОД =============================
#ifndef F_USE_PULSE
#define F_USE_PULSE 1
#endif
#ifndef F_USE_PULSE_IN
#define F_USE_PULSE_IN 1
#endif
#ifndef F_USE_QUAD
#define F_USE_QUAD 1
#endif

// ============================ L5.2: ТАЙМЕРЫ =================================
#ifndef F_USE_TICK
#define F_USE_TICK 1
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
#ifndef F_USE_WDT
#define F_USE_WDT 1
#endif
#ifndef F_USE_TIMER_ARBITER
#define F_USE_TIMER_ARBITER 1
#endif

// ============================ L5.3: ШИМ =====================================
#ifndef F_USE_PWM
#define F_USE_PWM 1
#endif

// ============================ L6: ШИНЫ ======================================
#ifndef F_USE_BUFFER
#define F_USE_BUFFER 1
#endif
#ifndef F_USE_UART
#define F_USE_UART 1
#endif
#ifndef F_USE_SPI
#define F_USE_SPI 1
#endif
#ifndef F_USE_I2C
#define F_USE_I2C 1
#endif
#ifndef F_USE_PACKET
#define F_USE_PACKET 1
#endif

// ============================ L6.1: ХРАНИЛИЩЕ ===============================
#ifndef F_USE_EEPROM
#define F_USE_EEPROM 1
#endif

// ============================ L6.2: ПИТАНИЕ / АНАЛОГ ========================
#ifndef F_USE_POWER
#define F_USE_POWER 1
#endif
#ifndef F_USE_ADC
#define F_USE_ADC 1
#endif
#ifndef F_USE_AC
#define F_USE_AC 1
#endif

// ============================ L8: СТРОКИ / МАТЕМАТИКА =======================
#ifndef F_USE_STRINGN
#define F_USE_STRINGN 1
#endif
#ifndef F_USE_ANY
#define F_USE_ANY 1
#endif
#ifndef F_USE_MATH
#define F_USE_MATH 1
#endif
#ifndef F_USE_FIXED_POINT
#define F_USE_FIXED_POINT 1
#endif

// ============================ L7: ЛОГИКА — ЗАЧАТОК, ВЫКЛ ====================
#ifndef F_USE_LOGIC
#define F_USE_LOGIC 0
#endif
#ifndef F_USE_TASK
#define F_USE_TASK 0
#endif
#ifndef F_USE_LOOPER
#define F_USE_LOOPER 0
#endif
#ifndef F_USE_STATE
#define F_USE_STATE 0
#endif
#ifndef F_USE_KALMAN
#define F_USE_KALMAN 0
#endif
#ifndef F_USE_PID_AUTOTUNE
#define F_USE_PID_AUTOTUNE 0
#endif

// ============================ L9: ДИСПЛЕИ — ЗАЧАТОК, ВЫКЛ ===================
#ifndef F_USE_LCD
#define F_USE_LCD 0
#endif
#ifndef F_USE_OLED
#define F_USE_OLED 0
#endif
#ifndef F_USE_GFX
#define F_USE_GFX 0
#endif
#ifndef F_USE_CHAR
#define F_USE_CHAR 0
#endif
#ifndef F_USE_MENU
#define F_USE_MENU 0
#endif

// ============================ L10: УСТРОЙСТВА — ЗАЧАТОК, ВЫКЛ ===============
#ifndef F_USE_SERVO
#define F_USE_SERVO 0
#endif
#ifndef F_USE_MCP23017
#define F_USE_MCP23017 0
#endif
#ifndef F_USE_STEPPER
#define F_USE_STEPPER 0
#endif
#ifndef F_USE_DRV8825
#define F_USE_DRV8825 0
#endif
#ifndef F_USE_TMC2209
#define F_USE_TMC2209 0
#endif
#ifndef F_USE_KEYPAD
#define F_USE_KEYPAD 0
#endif
// ShiftRegister приведён к BoardPin<N> (P0-18) — единственный готовый модуль L10.
#ifndef F_USE_SHIFTREG
#define F_USE_SHIFTREG 1
#endif

// ============================ ПРОВЕРКА ЗАВИСИМОСТЕЙ =========================
#if F_USE_PGM_UTILS && !F_USE_PROGMEM
#error "F_USE_PGM_UTILS требует F_USE_PROGMEM=1"
#endif

#if F_USE_BIT_SET && !F_USE_BIT_PACK
#error "F_USE_BIT_SET требует F_USE_BIT_PACK=1 (BitSet наследует BitPack)"
#endif

#if F_USE_QUAD && !F_USE_IRQ
#error "F_USE_QUAD требует F_USE_IRQ=1 (нужен диспетчер PCINT)"
#endif

#if F_USE_PULSE && !F_USE_TIMER1
#error "F_USE_PULSE требует F_USE_TIMER1=1 (InputCapture на Timer1)"
#endif

#if F_USE_PWM && !F_USE_TIMER_ARBITER
#warning "F_USE_PWM без F_USE_TIMER_ARBITER: конфликт за Timer1 не отлавливается"
#endif

#if F_USE_PACKET && !F_USE_CRC
#error "F_USE_PACKET требует F_USE_CRC=1"
#endif

#if F_USE_EEPROM && !F_USE_ATOMIC
#error "F_USE_EEPROM требует F_USE_ATOMIC=1"
#endif

#if F_USE_UART && !F_USE_BUFFER
#error "F_USE_UART требует F_USE_BUFFER=1 (приём через кольцевой буфер)"
#endif

#if F_USE_UART && !F_USE_TICK
#error "F_USE_UART требует F_USE_TICK=1 (таймауты через expired)"
#endif

#if F_USE_TASK && !F_USE_LOGIC
#error "F_USE_TASK требует F_USE_LOGIC=1"
#endif

#if F_USE_LOOPER && !F_USE_LOGIC
#error "F_USE_LOOPER требует F_USE_LOGIC=1"
#endif

#if F_USE_STATE && !F_USE_LOGIC
#error "F_USE_STATE требует F_USE_LOGIC=1"
#endif

#if F_USE_KALMAN && !F_USE_LOGIC
#error "F_USE_KALMAN требует F_USE_LOGIC=1"
#endif

#if F_USE_PID_AUTOTUNE && !F_USE_LOGIC
#error "F_USE_PID_AUTOTUNE требует F_USE_LOGIC=1 (нужен logic/Pid.h)"
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

#if F_USE_SHIFTREG && !F_USE_TICK
#error "F_USE_SHIFTREG требует F_USE_TICK=1 (delayUs)"
#endif
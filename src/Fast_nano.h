#pragma once
// ============================================================================
//  Fast_nano.h — umbrella-заголовок библиотеки fn / Fast_nano
//  Цель: ATmega328P / ATmega328 (Nano / Uno / Pro Mini), avr-gcc, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Подключает весь стек одним include. Порядок блоков отражает направление
//    зависимостей: фундамент → нумерация платы → таймеры → выводы → шины →
//    логика → дисплеи → моторы. Каждый модуль самодостаточен и может
//    подключаться напрямую, если нужен только он.
//
//  ПОРЯДОК БЛОКОВ (G13)
//    1. core: Config (ConfigCheck + Features), Compiler, Types, ForMacro,
//       Crash, Assert, Registers, Atomic, Memory, Bits, Crc, Progmem, Reset
//    2. board: pin/Board.h — нумерация выводов платы. Вынесена ОТДЕЛЬНЫМ
//       блоком, потому что её используют и timer (карта каналов PWM), и pin.
//       Это устраняет чтение «pin раньше timer, а timer зависит от pin».
//    3. timer: TimerCommon, Tick, Timer0/1/2, Watchdog, PWM
//    4. pin: Pin, PinRef, Port, Irq, Pulse, PulseInput, Quadrature
//    5. bus, store, peripheral, logic, display, motor, input
//    Заголовки самодостаточны, поэтому порядок не влияет на компиляцию —
//    он влияет на читаемость направления зависимостей.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Раскладка: файлы в src/ (формат библиотеки 1.5), иначе .cpp из
//      подпапок не компилируются. dot_a_linkage=false — иначе теряется .init3.
//    - Отключение модулей — только централизованно (правка Features.h или
//      -D для всех TU). Переопределение флага в одном .cpp даёт нарушение ODR.
//    - Все F_USE_* по умолчанию = 1. Для мелких скетчей — Fast_nano_core.h.
//    - Модули с прерываниями конкурируют за векторы: один вектор — один
//      владелец, заявки FN_CLAIM_* в .cpp модулей.
//    - Timer0 по умолчанию принадлежит ядру Arduino (millis/delay).
//    - Timer1 — общий ресурс Pulse / Pwm1 / Servo / Stepper: ровно один.
//    - PWM замещает analogWrite() на D3/D5/D6/D9/D10/D11.
//
//  ПРИМЕР
//    #include <Fast_nano.h>
//    void setup() { fn::BoardPin<13>::output(); }
//    void loop()  { fn::BoardPin<13>::toggle(); fn::delayMs(500); }
//
//  ЗАВИСИМОСТИ: все модули библиотеки
// ============================================================================

#include "core/Config.h"

// Arduino-слой нужен шинам, дисплеям и Tick. Включаем явно, чтобы заголовок
// работал и из .cpp, а не только из .ino.
#include <Arduino.h>

// --- core: фундамент ---
#include "core/Compiler.h"
#include "core/Types.h"
#include "core/ForMacro.h"
#include "core/Crash.h"
#if F_USE_ASSERT
#include "core/Assert.h"
#endif

#include "core/Registers.h"
#include "core/Atomic.h"
#include "core/Memory.h"
#include "core/Bits.h"
#include "core/Crc.h"
#include "core/Progmem.h"
#include "core/Reset.h"

// --- board: нумерация выводов (общая для timer и pin) ---
#include "pin/Board.h"

// --- timer ---
#include "timer/TimerCommon.h"
#if F_USE_TICK
#include "timer/Tick.h"
#endif
#if F_USE_TIMER0
#include "timer/Timer0.h"
#endif
#if F_USE_TIMER1
#include "timer/Timer1.h"
#endif
#if F_USE_TIMER2
#include "timer/Timer2.h"
#endif
#if F_USE_WDT
#include "timer/Watchdog.h"
#endif
#if F_USE_PWM
#include "timer/PwmImpl.h"
#endif

// --- pin ---
#include "pin/Pin.h"
#include "pin/PinRef.h"
#include "pin/Port.h"
#if F_USE_IRQ
#include "pin/Irq.h"
#endif
#if F_USE_PULSE
#include "pin/Pulse.h"
#endif
#if F_USE_PULSE_IN
#include "pin/PulseInput.h"
#endif
#if F_USE_QUAD
#include "pin/Quadrature.h"
#endif
#include "device/ShiftRegister.h"
#if F_USE_SERVO
#include "device/Servo.h"
#endif
#if F_USE_MCP23017
#include "device/MCP23017.h"
#endif

// --- string / math ---
#if F_USE_STRINGN
#include "string/StringN.h"
#endif
#if F_USE_ANY
#include "string/Any.h"
#endif
#if F_USE_MATH
#include "math/Mapping.h"
#include "math/Interpolation.h"
#include "math/Approximation.h"
#include "math/Statistics.h"
#include "math/LinearRegression.h"
#endif
#if F_USE_FIXED_POINT
#include "math/FixedPoint.h"
#endif

// --- bus ---
#include "core/BusContract.h"
#if F_USE_BUFFER
#include "bus/Buffer.h"
#endif
#if F_USE_UART
#include "bus/Uart.h"
#include "bus/UartSoft.h"
#endif
#if F_USE_I2C
#include "bus/I2c.h"
#include "bus/I2cSoft.h"
#endif
#if F_USE_SPI
#include "bus/Spi.h"
#include "bus/SpiSoft.h"
#endif
#if F_USE_PACKET
#include "bus/Packet.h"
#endif

// --- store / peripheral ---
#if F_USE_EEPROM
#include "store/Eeprom.h"
#include "store/EepromStore.h"
#endif
#if F_USE_POWER
#include "peripheral/Power.h"
#endif
#if F_USE_ADC
#include "peripheral/Adc.h"
#endif
#if F_USE_AC
#include "peripheral/AnalogComparator.h"
#endif

// --- logic ---
#if F_USE_LOGIC
#include "logic/Debounce.h"
#include "logic/Button.h"
#include "logic/Encoder.h"
#include "logic/Joystick.h"
#include "logic/Filter.h"
#include "logic/Pid.h"
#include "logic/Sequencer.h"
#include "logic/Ramp.h"
#include "logic/Smooth.h"
#include "logic/Hysteresis.h"
#if F_USE_TASK
#include "logic/Task.h"
#endif
#if F_USE_LOOPER
#include "logic/Looper.h"
#endif
#if F_USE_STATE
#include "logic/StateMachine.h"
#endif
#if F_USE_KALMAN
#include "logic/filters/KalmanFilter.h"
#endif
#if F_USE_PID_AUTOTUNE
#include "logic/PidAutoTune.h"
#endif
#if F_USE_MENU
#include "logic/Menu.h"
#endif
#endif

// --- display ---
#if F_USE_LCD
#include "display/Hd44780.h"
#include "display/Lcd1602.h"
#if F_USE_I2C
#include "display/Lcd1602I2c.h"
#endif
#endif
#if F_USE_OLED
#include "display/OledDisplay.h"
#endif
#if F_USE_GFX
#include "display/GraphicsDisplay.h"
#include "display/gfx/ScrollingText.h"
#endif
#if F_USE_CHAR
#include "display/char/CharBar.h"
#include "display/char/CharMatrix.h"
#include "display/char/CharPlot.h"
#endif
#if F_USE_MENU && (F_USE_LCD || F_USE_OLED || F_USE_GFX)
#include "display/DisplayMenu.h"
#endif

// --- motor / input ---
#if F_USE_STEPPER
#include "motor/StepperDriver.h"
#include "motor/A4988.h"
#if F_USE_DRV8825
#include "motor/DRV8825.h"
#endif
#if F_USE_TMC2209
#include "motor/TMC2209.h"
#endif
#endif
#if F_USE_KEYPAD
#include "input/Keys.h"
#include "input/Keypad4x4.h"
#include "input/Keypad4x4Interrupt.h"
#endif
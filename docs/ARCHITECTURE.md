# ARCHITECTURE.md — архитектура Fast_nano

**Версия:** 1.0 · **Дата:** 17.11.2026
**Границы:** единственный источник истины по слоям, графу зависимостей и правилам
направлений. Контракты API → CONTRACTS.md; инварианты → INVARIANTS.md; реестр модулей →
MODULES.md; бюджеты/владение → RESOURCES.md; решения → DECISIONS.md.

---

## 1. Платформа и тулчейн (неизменяемые границы)

- МК: ATmega328P/328. Остальные AVR / ESP / не-AVR — вне проекта.
- Тулчейн: Arduino IDE 2.3.10, avr-gcc 7.3.0, -std=gnu++11, -Os -flto,
  -fno-exceptions -fno-threadsafe-statics.
- Раскладка: library.properties + src/ (формат 1.5), dot_a_linkage=false (иначе теряется .init3).
- Внешние зависимости: только avr-libc и стандарт C++11. Arduino.h допустим в прикладных
  слоях; ядро по возможности чистое. Отвергнуто: Adafruit_*, U8g2, Servo.h, SoftwareSerial,
  Wire, SPI (сторонние).
- Исключение avr-libc: тайминг-критичные последовательности (EEPROM EEMPE→EEPE,
  _delay_loop_*) делегируются avr-libc (ручной C на границе 4-тактового окна ломается при -O/LTO).

---

## 2. Слои (L0–L10)

Каждый слой зависит ТОЛЬКО от нижележащих. Номер слоя = максимальная глубина зависимостей.

| Слой | Назначение | Модули | Зависит от |
|---|---|---|---|
| L0 | конфиг и диагностика | ConfigCheck, Features, Config, Compiler, Types, Crash, Assert | — (avr-libc) |
| L1 | доступ к железу | Atomic, Registers, Memory, Reset | L0 |
| L2 | битовый слой | BitMask/Scan/Count/Ops/Shift/Reverse/Pack/Set/Flags, Bits, Tables | L0–L1 |
| L3 | данные | Progmem, Crc, ForMacro | L0–L2 |
| L4 | прерывания и ресурсы | Resources, pin/Irq | L0–L3 |
| L5.1 | пины/ввод | Board, Pin, PinRef, Port, Pulse, Quadrature, PulseInput | L0–L4 |
| L5.2 | таймеры | TimerCommon, Tick, Timer0, Timer1, Timer2, Watchdog | L0–L4 |
| L5.3 | ШИМ | Pwm, PwmCommon, PwmTimer0/1/2, PwmImpl | L5.1–L5.2 |
| L6 | шины | BusContract, Buffer, Uart(+Soft), I2c(+Soft), Spi(+Soft), Packet | L0–L5 |
| L6.1 | хранилище | Eeprom, EepromStore | L0–L3 (+L1 atomic) |
| L6.2 | питание/аналог | Power, Adc, AnalogComparator | L0–L5 |
| L7 | логика | Debounce, Button, Encoder, Filter, Pid, Task, Looper, StateMachine, Ramp, Smooth, Hysteresis, Sequencer, Joystick, Kalman, PidAutoTune, Menu | L0–L6 |
| L8 | строки/математика | StringN, Any, StringBuilder; Mapping, Interpolation, Approximation, Statistics, LinearRegression, FixedPoint, MathUtils | L0–L3 |
| L9 | дисплеи | Hd44780, Lcd1602(+I2c), OledDisplay, GraphicsDisplay, ScrollingText, CharBar/Matrix/Plot, DisplayMenu | L6, L8 |
| L10 | устройства/моторы/ввод | ShiftRegister, Servo, MCP23017, StepperDriver, A4988, DRV8825, TMC2209, Keys, Keypad4x4(+Interrupt) | L5–L7 |

Примечание: L8 (строки/математика) по зависимостям ниже L7, но исторически разрабатывается
после логики; номер отражает порядок работ, а не глубину. Фактические зависимости L8 — L0–L3.

---

## 3. Правила зависимостей

| ID | Правило | Проверка |
|---|---|---|
| DEP-1 | Зависимости только вниз: L(n) → L(<n). Обратные запрещены | скрипт графа #include (этап 4.1) |
| DEP-2 | Запрет циклов между модулями | скрипт графа (топсорт) |
| DEP-3 | Шина не знает потребителя: хелперы принимают шину параметром шаблона (BusContract, D35) | REVIEW |
| DEP-4 | Регистровый драйвер не заявляет владение устройством; заявляет потребитель (INV-19) | REVIEW |
| DEP-5 | Таймерный словарь (TimerCommon) ни от кого не зависит; таймеры включают его, не наоборот (D23) | скрипт графа |
| DEP-6 | Прикладной модуль не пишет своих ISR и не зовёт attachInterrupt напрямую (кроме шин с уникальными векторами) | GREP ISR/attachInterrupt |
| DEP-7 | Диспетчер Irq живёт в pin/, не в core/ (знает нумерацию платы, D16) | скрипт графа |
| DEP-8 | Трейты (Select/Id/UnsignedOf/…) только в Types.h; дублирование запрещено | GREP дублей (INV-22) |
| DEP-9 | umbrella тянет только готовые слои; зачаточные за гейтом F_USE_* | REVIEW Features.h |

Запрещённые связи (примеры): logic → регистры напрямую (только через HAL/драйверы);
display → конкретная шина жёстко (через BusContract-параметр); timer/Pwm → Timer0.h ради enum
(устранено, D23); любой модуль → Tables.cpp напрямую (только через объявления Types.h/Progmem.h).

---

## 4. Граф зависимостей as-is (ключевые рёбра)

Types ← Compiler ← Config ← ConfigCheck/Features
Atomic ← Types; Registers ← {Types, Atomic, Assert}
Bits/BitPack ← Types; Tables ← Types (определения resultName/ownerName)
Crc ← Types; Progmem ← Types
Resources ← Types; Irq ← {Board, Registers, Resources, Atomic, Types}
TimerCommon ← Types (ни от кого больше); Tick ← {Types, Atomic, Resources, Arduino}
Timer0/1/2 ← {TimerCommon, Registers, Resources, Atomic}; Watchdog ← {Resources, Crash}
PwmCommon ← {TimerCommon, Pwm, Registers, Atomic}; PwmTimer0/1/2 ← PwmCommon
BusContract ← {Types, Compiler, Progmem, Tick}; Buffer ← Types; Uart/I2c/Spi ← {BusContract, Buffer, Registers, Resources, Atomic, Tick}
Eeprom ← {Types, Atomic, Crc}; EepromStore ← {Eeprom, Crc}
Power ← {Types, Atomic, Resources}; Adc ← {Types, Atomic, Resources, Tick}; AC ← {Types, Atomic, Resources}

Выявленные проблемы as-is:
- AlwaysFalse дублируется Compiler↔Types (P0-1, INV-22).
- Pulse.cpp заявляет Timer1 compile-time вопреки runtime-модели (P0-2, DEP-4/INV-19).
- PwmCommon ссылается на несуществующий tmr::acquire (P0-4).
- umbrella тянет зачаточные L7/L9/L10 (DEP-9).

---

## 5. Целевой граф (target)

Изменения относительно as-is:
1. AlwaysFalse — одно определение в Types.h (Compiler.h использует, не определяет).
2. Timer1: единственный механизм владения — runtime acquire/release (Timer1::acquire или
   общий tmr::acquire). Compile-time FN_CLAIM_TIMER(1,…) убран из потребителей; FN_CLAIM_VECTOR
   для Timer1-векторов остаётся как фиксация «никто не занял», но владелец задаётся потребителем.
3. Pwm1::begin вызывает acquire(Client::Pwm); конфликт с Pulse/Servo/Stepper → Busy.
4. Дефолты владения одиночными ресурсами приведены к RESOURCES.md §3.
5. umbrella: L7/L9/L10 не подключаются до готовности; ядро через Fast_nano_core.h.

---

## 6. Диаграмма слоёв (текстовая)

        ┌─────────────────────────────────────────────┐
 L10    │ device / motor / input                       │
 L9     │ display                                      │
 L7     │ logic                                        │
        ├─────────────────────────────────────────────┤
 L6.2   │ peripheral (Power/Adc/AC)                    │
 L6.1   │ store (Eeprom)        L6 bus (Uart/I2c/Spi)  │
 L5.3   │ PWM                                          │
 L5.1/2 │ pin / timer                                  │
 L4     │ Irq / Resources                              │
 L3     │ Progmem / Crc / ForMacro                     │
 L2     │ bits / Tables                                │
 L1     │ Atomic / Registers / Memory                  │
 L0     │ Config / Types / Compiler / Crash / Assert   │
        └─────────────────────────────────────────────┘
              стрелки зависимостей только вниз

---

## 7. Сквозные механизмы

- Время: Timer0 принадлежит ядру; все сравнения через elapsed/expired (INV-20).
- Конкурентность: ISR vs main + SPSC; AtomicBlock для RMW/многобайтовых (INV-07). DMA отсутствует.
- Владение: compile-time FN_CLAIM_* (одиночные) + runtime acquire (разделяемые) — RESOURCES.md.
- Диагностика: Crash (.noinit + .init3 захват MCUSR) — общий узел Assert/Reset (D1/D15).
- Ошибки: единый fn::Result (INV-21); фатальные — через Assert→Crash, не Result.
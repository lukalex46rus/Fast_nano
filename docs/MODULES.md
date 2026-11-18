# MODULES.md — реестр модулей Fast_nano

**Версия:** 1.0 · **Дата:** 17.11.2026
**Границы:** единственный источник истины по составу модулей, их ответственности,
public/internal API, зависимостям и статусу готовности. Контракты → CONTRACTS.md;
зависимости-правила → ARCHITECTURE.md; sizeof/бюджеты → RESOURCES.md.

Статусы: ✅ готов (контракт+код+проверка) · 🔧 почти готов (код есть, контракт/фиксы в работе)
· 🌱 зачаток · ❌ блокер. Поля: public API семантически; internal — не входит в контракт.

---

## L0 — конфиг и диагностика

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Config.h | базовые дефолты/F_CPU | макросы конфигурации | — | 🔧 |
| Features.h | гейты F_USE_* | флаги включения модулей | Config | 🔧 |
| ConfigCheck.h | проверка конфигурации | static_assert-проверки | Features | 🔧 |
| Compiler.h | атрибуты/макросы компилятора | FN_INLINE/FN_NORETURN/FN_PSTR/arraySize | — | ❌ AlwaysFalse дубль (P0-1) |
| Types.h | типы-валюта | Pin?/Result/трейты/resultName | Compiler | ❌ AlwaysFalse дубль (P0-1) |
| Crash.cpp/h | .noinit запись сбоя, halt/forceReset | record/halt/forceReset/policy/resetFlags/boots | Types | 🔧 |
| Assert.h | проверки предусловий | FN_ASSERT/FN_VERIFY/setAssertHandler | Crash | 🔧 |

## L1 — доступ к железу

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Atomic.h | критические секции | AtomicBlock/atomicRead*/Write*/Modify/Exchange/Counter | Types | 🔧 |
| Registers.h | GPIO без таблиц | regs::sfr/read/write/mask/mode/toggle/FastPin/save/restore | Types,Atomic,Assert | 🔧 (G-6 FastPin) |
| Memory.cpp/h | диагностика SRAM | stackPaint/stackUsed/freeRam/report | Atomic | 🔧 |
| Reset.cpp/h | причина сброса, перезапуск | cause/flags/soft/halt/toBootloader | Crash, Atomic | 🔧 |

## L2 — битовый слой

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| BitMask.h | маски битов | mask/maskRange | Types | 🔧 |
| BitScan.h | поиск битов | ctz/clz/ffs | Types | 🔧 |
| BitCount.h | подсчёт битов | popcount/parity | Types | 🔧 |
| BitOps.h | операции над битами | set/clear/toggle/test | Types | 🔧 |
| BitShift.h/BitReverse.h | сдвиг/реверс | shiftLeft/shiftRight/reverseBitOrder | Types | 🔧 |
| BitPack.h | битовые массивы CRTP | BitPack<N>/BitPackWide/normalize | Types | 🔧 |
| BitSet.h/BitFlags.h | множества бит/флаги | BitSet/Flags | Types | 🔧 |
| Bits.h | фасад битового слоя | агрегатор | Bit* | 🔧 |
| Tables.cpp | таблицы во flash | определения resultName/ownerName | Types | 🔧 |

## L3 — данные

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Progmem.h | чтение flash | pgmRead*/чтение строк | Types | 🔧 |
| Crc.h | CRC-движок | crc8_maxim/crc16_ccitt/crc16_modbus | Types | 🔧 |
| ForMacro.h | препроцессорные циклы | FN_FM_* (64 уровня) | — | 🔧 |

## L4 — прерывания и ресурсы

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Resources.h | реестр владения | FN_CLAIM_VECTOR/TIMER, ownerName | Types | ❌ дефолты≠заявки (P0-2) |
| pin/Irq.cpp/h | диспетчер PCINT/INT | PinChange/ExternalInt attach/detach/enable/disable | Pin,Board,Resources,Atomic,Registers | 🔧 (G-4 enable/disable) |

## L5.1 — пины/ввод

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Board.h | карта платы Nano | boardPin/BoardPin<N> | Types | 🔧 |
| Pin.h | тип вывода | Pin/port/bit/mask/valid/fromId | Types | 🔧 |
| PinRef.h/Port.h | ссылка на пин/порт | PinRef/Port-операции | Pin,Registers | 🔧 |
| Pulse.cpp/h | InputCapture измерение | begin/measure/frequency/periodMicros | Timer1,Irq,Resources | ❌ claim Timer1 (P0-2) |
| Quadrature.cpp/h | декодер квадратуры | begin/position/takeDelta/errors | Irq,Pin | 🔧 |
| PulseInput.h | блокирующее измерение импульса | measure (бюджеты ожидания/измерения) | Pin,Timer | 🔧 |

## L5.2 — таймеры

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| TimerCommon.h | словарь таймеров | Clock/Out/comBits/bestClock/ticksToUs | Types | 🔧 (G-2 kClocks RAM) |
| TimerCommon.cpp | реестр владельцев таймеров | tmr::acquire/release/ownerOf | TimerCommon, Atomic | ✅ |
| Tick.cpp/h | база времени | millis/micros/elapsed/expired/Timeout/Stopwatch | Types,Atomic,Resources,Arduino | 🔧 (G-1 Stopwatch) |
| Timer0.cpp/h | драйвер Timer0 | begin/takeover/restoreArduino | TimerCommon,Registers,Resources | 🔧 |
| Timer1.cpp/h | драйвер Timer1 + арбитраж | acquire/release/takeover/setMode/setPeriodUs/capture | TimerCommon,Registers,Resources | 🔧 (G-7 setMode CS) |
| Timer2.cpp/h | драйвер Timer2 | begin/takeover/setPeriod | TimerCommon,Registers,Resources | 🔧 (G-2 kClocks RAM) |
| Watchdog.cpp/h | WDT | enable/disable/feed/setTimeout | Resources,Crash | ❌ claim Wdt (P0-2) |

## L5.3 — ШИМ

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Pwm.h | словарь ШИМ/карта каналов | PwmMode/PwmOut/channelPin/channelTimer | Types | 🔧 |
| PwmCommon.h | шаблонный движок | Engine<Traits>::begin/setFrequency/setDuty/attach | TimerCommon,Pwm,Registers,Atomic | 🔧 |
| PwmTimer0/1/2.h | traits таймеров | Traits0/1/2, алиасы Pwm0/1/2 | PwmCommon | ❌ P0-3 (Traits0 бит WGM02) |
| PwmImpl.h | сборка ШИМ-слоя | include-агрегатор | Pwm* | 🔧 |

## L6 — шины

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| BusContract.h | контракт шины | writeP/waitUntil/transferFill<Bus> | Types, Compiler, Progmem, Tick | 🔧 |
| Buffer.h | SPSC ring buffer | RingBuffer<T,N>/push/pop/available/dropped | Types | 🔧 |
| Uart.cpp/h (+Soft) | USART0/soft TX | begin/end/write/read/transfer/available/flush | BusContract,Buffer,Registers,Resources,Tick | ❌ claim UsartRx (P0-2) |
| I2c.cpp/h (+Soft) | TWI/soft I2C | begin/end/start/stop/write/read/recover/scan | BusContract,Registers,Resources,Tick | ❌ claim Twi (P0-2) |
| Spi.cpp/h (+Soft) | SPI/soft SPI | begin/end/transfer/read/write/transfer16 | BusContract,Registers,Resources,Tick | ❌ claim SpiStc (P0-2), G-2/G-5 |
| Packet.h | кадровый автомат | Packet/CRC-8/16/reset | Crc,Buffer | 🔧 |

## L6.1 — хранилище

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Eeprom.cpp/h | EEPROM байт/блок | read/write/update/validRange/written | Types,Atomic,Crc | 🔧 (G-3 cli-окно) |
| EepromStore.h | типизированный блок+CRC | EepromStore<T>/tick/commit/load | Eeprom,Crc | 🔧 |

## L6.2 — питание/аналог

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| Power.cpp/h | сон/делители/PRR | setMode/sleepOnce/setClockDivider/disable/enable | Registers,Resources,Atomic | 🔧 |
| Adc.cpp/h | АЦП | begin/read10/read8/readMux/readVcc/readTemperature | Registers,Resources,Atomic,Tick | ❌ claim Adc (P0-2), G-8 |
| AnalogComparator.cpp/h | аналоговый компаратор | begin/setNegativeMux/callback | Types, Atomic, Resources | ❌ claim AnalogComp (P0-2) |

## L8 — строки/математика

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| StringN.h | строка фикс. ёмкости | StringN/add/c_str | Types,StringBuilder | 🔧 |
| Any.h | тип-значение | Any/toInt/toUint/toFloat/readFromStr | Types | 🔧 (FN_ANY_64BIT гейт ✅) |
| StringBuilder.cpp/h | форматирование | appendUnsigned/appendFloat/appendUnsigned64 | Types | 🔧 |
| Mapping.h | масштабирование | mapFast/mapFloat/Mapping | Types | 🔧 |
| Interpolation.h | интерполяция | lerp/lerpInt/smoothStep/bilinear | Types | 🔧 |
| Approximation.h | табличная аппроксимация | get/getInt/segment | Types,Progmem | 🔧 |
| Statistics.h | статистика | Statistics/IntVariance/FloatStatistics/arrayRms | Types,MathUtils | 🔧 |
| LinearRegression.h | линейная регрессия | add/fit/slope/predict | Types | 🔧 |
| FixedPoint.h | фикс. точка Q | Fixed8/16/24/арифметика | Types | 🔧 |
| MathUtils.h | целочисленная математика | isqrt32/isqrt64/gcd/lcm | Types | 🔧 (не в umbrella) |

## L7 (предконтракт)

| Модуль | Ответственность | Public API | Зависимости | Статус |
|---|---|---|---|---|
| core/LogicContract.h | фиксация формы API слоя logic: Task/Filter/Periodic | контракты tick/active/begin/end, update/value/reset, Periodic | Types, Tick | 🌱 предконтракт |

Примечание: файл задаёт контракты до реализации модулей. При наполнении CONTRACTS.md §2.16
(L7) его содержимое поглощается туда; до тех пор действует правило DEP-9 (за гейтом F_USE_LOGIC).

## L7 / L9 / L10 — зачаточные (вне текущей волны)

| Слой | Модули | Статус |
|---|---|---|
| L7 logic | Debounce, Button, Encoder, Filter(+filters/*), Pid, PidAutoTune, Task, Looper(+looper/*), StateMachine, Ramp, Smooth, Hysteresis, Sequencer, Joystick, Menu, DelayTimer, PeriodicTimer | 🌱 |
| L9 display | Hd44780, Lcd1602(+I2c/+lcd/*), OledDisplay(+oled/*), GraphicsDisplay(+gfx/*), DisplayMenu(+menu/*), char/* | 🌱 |
| L10 device/motor/input | ShiftRegister, Servo, MCP23017, StepperDriver, A4988, DRV8825, TMC2209, Keys, Keypad4x4(+Interrupt) | 🌱 |

Гейты закрыты (P0-10/P0-20): F_USE_LOGIC/SERVO/KEYPAD/GFX/CHAR/STEPPER/TMC2209/
DRV8825/MCP23017/LCD/MENU/OLED = 0. Причина: API слоёв не согласован с контрактами
pin/timer/шин (G-10/G-11/G-12). Открывать по одному модулю после миграции этапа 3.
Исключение: ShiftRegister приведён к BoardPin<N> и включён (F_USE_SHIFTREG=1).


Замечания инвентаризации:
- logic/Menu.h ↔ display/menu/Menu.h — два Menu, развести ответственность (L7 vs L9).
- logic/DelayTimer.h, PeriodicTimer.h, filters/* (кроме Kalman) не подключены umbrella.
- math/MathUtils.h не подключён umbrella (F_USE_MATH).
- display/gfx/fonts/Icons8x8.h ↔ display/oled/Icons8x8.h — возможный дубль имён (DEP-8/INV-22 проверить).
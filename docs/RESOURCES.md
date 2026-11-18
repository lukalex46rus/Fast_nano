# RESOURCES.md — бюджеты и реестр ресурсов Fast_nano

**Версия:** 1.0 · **Дата:** 17.11.2026
**Границы:** единственный источник истины по бюджетам памяти, sizeof объектов, реестру
векторов/таймеров/шин/выводов и модели владения. Контракты API → CONTRACTS.md;
правила → INVARIANTS.md; история → DECISIONS.md.

---

## 1. Бюджеты платформы

| Ресурс | Лимит | Примечание |
|---|---|---|
| SRAM | 2048 Б | .data + .bss + heap + stack; стек растёт вниз от RAMEND |
| Flash | 32768 Б | старый загрузчик Nano 2048 Б → 30720 Б пользователю (Optiboot 512 Б → 32256 Б) |
| EEPROM | 1024 Б | запись 3.4 мс/байт, ресурс ~100k циклов |
| Тактовая | 16 МГц (кварц Nano) | TicksPerUs = 16; внутренний RC ±1% |
| Стек | нижняя граница через stackPaint | guard 64 Б, stackUsed занижает (D84) |

Правило: регресс .data/.bss/flash после каждого этапа фиксируется avr-size (PROCESS.md).

---

## 2. sizeof публичных объектов (SRAM)

| Объект | sizeof | Состав | Статус |
|---|---|---|---|
| fn::Pin | 1 Б | (port<<3)\|bit | ✅ |
| AtomicBlock | 1 Б (stack) | SREG | ✅ |
| AtomicCounter<T> | sizeof(T) | volatile T | ✅ |
| Timeout | 8 Б | uint32 start + uint32 period | ✅ |
| Stopwatch | 9 Б | uint32 start + uint32 acc + bool run | ⚠️ цель 8 (G-1) |
| RingBuffer<u8,64> | 68 Б | buf[64]+head(1)+tail(1)+dropped(2) | ✅ |
| irq::Slot | 5 Б | Pin(1)+cb(2)+ctx(2) | ✅ |
| пул irq (FN_IRQ_SLOTS=8) | 40 Б + 6 на INT | 8×5 + ext | ✅ |
| LinearRegression | 17 Б | 4×float + bool | ✅ (float-путь) |
| Crash Record (.noinit) | 12 Б | magic+boots+flags+code+line+expr+file+sum | ✅ |
| tmr::acquire реестр | 3 Б | по 1 Б на таймер (TimerCommon.cpp) | ✅ |

Числа flash по модулям — из шапок файлов, сводятся после этапа 3 (avr-size).

---

## 3. Реестр векторов и модель владения

Модель (устраняет двойственность P0-2): одиночный владелец → compile-time FN_CLAIM_VECTOR;
разделяемый ресурс → runtime acquire. Дефолты ниже = целевые (приведены к контракту).

| Вектор | Владелец по умолчанию | Можно отдать | Механизм |
|---|---|---|---|
| INT0, INT1 | ARDUINO | нет | attachInterrupt (ISR в WInterrupts.c) |
| TIMER0_OVF | ARDUINO | нет | millis/micros/delay |
| PCINT0/1/2 | IRQ | да | compile-time claim |
| TIMER0_COMPA/B | NONE | да | compile-time claim |
| TIMER1_CAPT/OVF/COMPA/B | NONE | да | runtime acquire (потребитель) |
| TIMER2_OVF/COMPA/B | NONE | да | compile-time claim |
| USART_RX | UART | да | compile-time claim |
| USART_UDRE | NONE | да | compile-time claim |
| SPI_STC | SPI | да | compile-time claim |
| TWI | I2C | да | compile-time claim |
| ADC | ADC | да | compile-time claim |
| ANALOG_COMP | ADC | да | compile-time claim |
| WDT | USER | да | compile-time claim |

Отличие от текущих дефолтов Resources.h: USART_RX/SPI_STC/TWI/ADC/ANALOG_COMP/WDT
сейчас NONE → привести к таблице (P0-2). Timer1 — снять compile-time заявку с Pulse.

---

## 4. Таймеры целиком

| Таймер | Владелец | Претенденты | Ограничения |
|---|---|---|---|
| Timer0 | ARDUINO | Tick(клиент), Pwm0 | смена TOP/CS ломает millis; PWM только COM/OCR |
| Timer1 | NONE | Pulse, Pwm1, Servo, Stepper | ровно один потребитель; runtime acquire |
| Timer2 | NONE | Pwm2, Timer2 | делители 1/8/32/64/128/256/1024 (CS2x≠CS0x) |

---

## 5. Шины как ресурс

| Ресурс | Владелец | Освобождение |
|---|---|---|
| USART0 + D0/D1 | Uart | Uart::end() |
| SPI + D10..D13 | Spi | Spi::end() (прежнее состояние пинов НЕ восстанавливается) |
| TWI + A4/A5 | I2c | I2c::end() |
| ADC | Adc | Adc::end(); компаратору в режиме Mux нужен ADEN=0 |
| EEPROM | Eeprom | не захватывается; запись 3.4 мс/байт блокирует |

Порядок перед сном: Xxx::end() → Power::disable(...) (INV-17/INV-08 контекст).

---

## 6. Выводы с двойным назначением

| Вывод | Функции | Конфликт |
|---|---|---|
| D0/D1 (PD0/PD1) | RX/TX USART0 | занят при Uart |
| D2 (PD2) | INT0 | — |
| D3 (PD3) | INT1 / OC2B | PWM на D3 убивает INT1 |
| D5 (PD5) | OC0B / T1 | внешнее тактирование Timer1 vs PWM |
| D6 (PD6) | OC0A / AIN0 | CTC по OCR0A убивает PWM и millis; компаратор |
| D7 (PD7) | AIN1 | компаратор |
| D8 (PB0) | ICP1 | захват Pulse; ACIC компаратора |
| D9/D10 | OC1A/OC1B | PWM Timer1 vs Servo |
| D10 (PB2) | SS SPI | обязан быть выходом в мастере |
| D11 (PB3) | OC2A / MOSI | PWM vs SPI |
| D12 (PB4) | MISO | SPI |
| D13 (PB5) | SCK / LED | SPI vs светодиод платы |
| A4/A5 (PC4/PC5) | SDA/SCL | TWI vs GPIO/ADC |
| A6/A7 | ADC6/ADC7 | только ADC, цифрового порта нет |
| PB6/PB7 | кварц / TOSC | асинхронный Timer2 невозможен на Nano |
| PC6 | RESET | только при RSTDISBL, после чего ISP недоступен |
| PC7 | — | физически отсутствует |

---

## 7. WCET критических путей (худший случай)

| Путь | Оценка | Условие |
|---|---|---|
| regs::set/clear (FastPin, sbi) | 2 такта | константный адрес (G-6: проверить objdump) |
| regs::toggle (PINx) | 1 запись | атомарно |
| AtomicBlock накладные | ~4 такта | in/cli/out (G: inline) |
| delayUsPrecise(us) | us мкс, cli | блокирует ISR на всё время |
| EEPROM write byte | 3.4 мс | busy_wait; G-3: cli-окно сузить |
| I2c транзакция | таймаут на фазу | не на транзакцию |
| PulseInput измерение | сумма ожидания+измерения | раздельные бюджеты (D55) |
| CRC-8 побитовый | ~N×цикл | без таблиц (D20), пакеты ≤64 Б |

Точные замеры — этап 3 (осциллограф/objdump), сводятся сюда.
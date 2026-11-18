# CONTRACTS.md — контракты интерфейсов Fast_nano

**Версия:** 2.0 (полная переработка) · **Дата:** 17.11.2026
**Статус:** каркас + сквозные контракты + фундамент заполнены; остальные модули в очереди (§2.9).
**Платформа:** ATmega328P/328, gnu++11, avr-gcc 7.3.0.

## Границы документа
Здесь живут ТОЛЬКО контракты публичных API: предусловия, постусловия, гарантии при
ошибках, таймауты, ISR-семантика. НЕ живут здесь (ссылаться, не дублировать):
- глобальные правила/инварианты → INVARIANTS.md (INV-*)
- бюджеты, sizeof, реестр векторов/таймеров/выводов → RESOURCES.md
- состав модулей и зависимости → MODULES.md
- история решений → DECISIONS.md (D*)
- порядок работ и расхождения → ROADMAP.md

Приоритет: контракт описывает ЦЕЛЕВОЕ поведение. Текущее расхождение кода с контрактом
фиксируется в ROADMAP.md (backlog G-*), а не маскируется в контракте.

---

## 0. Шаблон контракта модуля

Каждый публичный модуль описывается по полям:

| Поле | Содержание |
|---|---|
| ID | C-<СЛОЙ>-<МОДУЛЬ>, напр. C-CORE-REG |
| Модуль / слой | путь файла, слой L0–L10 |
| Назначение | одна строка |
| API | публичные функции/типы (семантически; точные сигнатуры — в шапке файла) |
| Предусловия | что обязано быть истинно ДО вызова |
| Постусловия | что гарантировано ПОСЛЕ успешного вызова |
| Гарантии при ошибке | что НЕ меняется при Result != Ok; какой код возвращается |
| Таймаут / блокировка | макс. время блокировки, WCET; запрет бесконечного ожидания |
| ISR / конкурентность | можно ли из ISR; что защищать; атомарность |
| Ресурсы | SRAM/flash/stack, занимаемые векторы/таймеры/пины (числа → RESOURCES.md) |
| Инварианты | ссылки INV-* / D* |
| Верификация | host-test / static_assert / hardware / objdump |

---

## 1. Сквозные контракты (действуют для всех модулей)

### 1.1 fn::Result — коды и семантика
enum class uint8_t. Многоисходные операции возвращают Result; bool — только для
бинарного исхода. Свой enum с Ok/Timeout/Busy запрещён (INV-21).

| Код | Семантика | Повторяемость |
|---|---|---|
| Ok | операция выполнена, постусловия holds | — |
| Fail | общая ошибка (избегать; использовать конкретный код) | по контексту |
| BadArg | невалидный аргумент (nullptr, NoPin, порт вне диапазона) | нет, исправить аргумент |
| OutOfRange | значение вне диапазона (адрес EEPROM>1023, частота/делитель не подобрать) | нет, исправить аргумент |
| NoBuffer | нет буфера/места | да, после освобождения |
| Busy | ресурс занят другим владельцем (acquire) | да, после release |
| Timeout | истёк таймаут ожидания железа/шины | да |
| NotReady | модуль не инициализирован (begin не вызван) / линия не готова | да, после begin |
| Unsupported | операция не поддерживается конфигурацией/ролью (SoftUartTx.read, Timer0 *Top) | нет |
| CrcError | контрольная сумма не совпала | да, повторить обмен |
| AddrNack | I2C: адрес не подтверждён | да, после recover |
| DataNack | I2C: байт данных не подтверждён | да, после recover |

### 1.2 Политика ошибок и восстановления
- Гарантия по умолчанию: при Result != Ok состояние модуля НЕ меняется (strong).
  Исключения обязаны быть явно оговорены в контракте модуля (напр. Uart.begin при
  OutOfRange по baud — настройка применяется; Spi.read — частичный прогресс).
- Ошибки вызывающего (BadArg/OutOfRange/Unsupported) не повторять без изменения аргументов.
- Ошибки связи (CrcError/AddrNack/DataNack) — восстановление по контракту шины (I2c.recover).
- Фатальные нарушения предусловий — через FN_ASSERT → Crash (Halt/Reset), НЕ через Result.
- resultName() возвращает PGM_P (flash), определение в Tables.cpp.

### 1.3 Время и таймауты
- Сравнения времени ТОЛЬКО через elapsed()/expired()/expiredUs() (wrap-safe).
  Прямое `millis() > deadline` запрещено (INV-20, D25).
- timeoutMs == 0 — одна проверка без ожидания. Бесконечного ожидания железа НЕТ (INV-13).
- Разрешение millis() = 1 мс, micros() = 4 мкс. Точность до такта — InputCapture (Pulse).
- Блокирующие измерители декларируют худший случай блокировки в контракте.

### 1.4 Конкурентность ISR / main
- 8-битный одиночный доступ атомарен аппаратно (без cli).
- Многобайтовые разделяемые данные и любой RMW — под AtomicBlock (INV-07).
- ISR не вкладываются; ISR vs main защищается AtomicBlock в main.
- SPSC-буферы: producer=ISR, consumer=main; при 8-битных индексах блокировка не нужна.
- Коллбэки из ISR — короткие, без блокирующих операций.

### 1.5 Инициализация и владение
- begin() конфигурирует ресурс; повторный begin допустим (для шин эквивалентен end+begin, D74).
- end() освобождает ресурс. Порядок перед сном: Xxx::end() → Power::disable(...) (INV-08).
- Разделяемый ресурс (Timer1) — runtime acquire/release; одиночный — compile-time FN_CLAIM_* (RESOURCES.md).
- Инициализация объекта завершается ДО регистрации в диспетчере/ISR.

---

## 2. Контракты модулей

### 2.1 C-CORE-TYPES — fn::Pin / fn::Result / трейты
- **Модуль/слой:** core/Types.h, pin/Pin.h · L0/L1
- **Назначение:** единые типы-валюта проекта.
- **API:** Pin(port,bit), Pin::fromId(id), NoPin; port()/bit()/mask()/id()/valid();
  Result; трейты Select/Id/UnsignedOf/IsBitType/WidthOf/UBits; resultName(), Status, ok(), toStatus(), toResult().
- **Предусловия:** конструирование Pin — port∈{B,C,D}, bit∈0..7; fromId принимает любой
  uint8_t (валидность проверяет valid()).
- **Постусловия:** sizeof(Pin)==1; id()==(port<<3)|bit; mask()==1<<bit; valid()==false
  для PC7, id≥PortCount*8 и NoPin(0xFF).
- **Гарантии при ошибке:** не бросает; невалидный Pin → valid()==false, потребители
  возвращают BadArg. resultName неизвестного кода → "Unknown".
- **Таймаут/блокировка:** нет (чистые/constexpr).
- **ISR:** безопасно (без состояния).
- **Ресурсы:** 0 SRAM; таблица resultName во flash (Tables.cpp). → RESOURCES.md
- **Инварианты:** INV-10 (не совпадать с макросами Arduino), INV-11 (нет _Заглавная).
- **Верификация:** host static_assert sizeof/значения; unit valid() по всем 256 id.

### 2.2 C-CORE-REG — fn::regs
- **Модуль/слой:** core/Registers.h · L1 (HAL)
- **Назначение:** единая точка GPIO без таблиц в RAM.
- **API:** sfr/valid/validMask/layoutOk; readInput/readLatch/readDir; write;
  setMask/clearMask/writeMask(+Atomic); toggle; modeOutput/modeInput/modeInputPullup(+Atomic);
  per-pin set/clear/toggle/write/read/mode*; FastPin<id>; save/restore/pullupsDisable/idleUnused.
- **Предусловия:** layoutOk()==true (FN_ASSERT при старте); порт валиден; toggle применять
  только к PINx (аппаратная инверсия), не к регистрам с иной семантикой записи 1.
- **Постусловия:** адрес = _SFR_MEM_ADDR(PINB)+3*port+reg; toggle пишет маску в PINx
  (атомарно, без RMW); mode* пишет PORT затем DDR (без glitch); *Atomic — под AtomicBlock;
  readInput читает PINx, readLatch — PORTx.
- **Гарантии при ошибке:** невалидный порт → nullptr из sfr / OutOfRange (порт) / BadArg (пин);
  состояние железа не меняется.
- **Таймаут/блокировка:** нет (~2–6 тактов на операцию).
- **ISR:** неатомарные setMask/clearMask/mode* — RMW, небезопасны при ISR по тому же порту →
  брать *Atomic или FastPin. toggle и 8-бит read/write атомарны.
- **Ресурсы:** 0 SRAM. FastPin → sbi/cbi при константном адресе (требует FN_INLINE sfr; backlog G-6).
- **Инварианты:** INV-08 (PORT→DDR), INV-07, INV-01 (нет таблиц).
- **Верификация:** hardware (glitch/осциллограф), layoutOk runtime, objdump для FastPin.

### 2.3 C-CORE-ATOMIC — AtomicBlock / atomic*
- **Модуль/слой:** core/Atomic.h · L1
- **Назначение:** критические секции и атомарные обращения.
- **API:** AtomicBlock, FN_ATOMIC, atomicRead8/16/32, atomicWrite8/16/32, atomicModify,
  atomicExchange, AtomicCounter<T>.
- **Предусловия:** объект AtomicBlock именованный (безымянный уничтожается сразу и не защищает);
  T∈{uint8_t,uint16_t,uint32_t}.
- **Постусловия:** ctor сохраняет SREG→cli→барьер; dtor барьер→SREG=сохранённый (вложенность
  корректна, I восстанавливается только если был установлен); atomicRead8/Write8 без cli;
  16/32 и все RMW AtomicCounter — под блокировкой.
- **Гарантии при ошибке:** не бросает; по выходе флаг I в исходном состоянии (не форсируется).
- **Таймаут/блокировка:** нет; держать блок коротким (задержка ISR).
- **ISR:** AtomicBlock в main защищает от ISR; внутри ISR избыточен (ISR не вкладываются), допустим.
- **Ресурсы:** AtomicBlock = 1 Б stack (SREG); AtomicCounter<T> = sizeof(T). → RESOURCES.md
- **Инварианты:** INV-07.
- **Верификация:** host (логика вложенности), hardware (гонки ISR/main).

### 2.4 C-TIMER-TICK — база времени
- **Модуль/слой:** timer/Tick.h/.cpp · L5.2
- **Назначение:** время ядра Arduino + wrap-safe таймауты + точная мкс-задержка.
- **API:** millis/micros/delayMs/delayUs/delayUsPrecise; elapsed/expired/expiredUs; Timeout; Stopwatch.
- **Предусловия:** Timer0 принадлежит Arduino (FN_CLAIM_TIMER(0,ARDUINO)); Timer0 не захвачен другим модулем.
- **Постусловия:** elapsed(since,now)==now-since по модулю 2^32; expired(since,t)==elapsed>=t;
  delayUsPrecise(0) возвращается сразу; delayUsPrecise компенсирует накладные цикла.
- **Гарантии при ошибке:** чистые функции времени без состояния; delayUsPrecise не вызывает _delay_loop_2(0).
- **Таймаут/блокировка:** delayUsPrecise блокирует до us мкс с запрещёнными прерываниями
  (худший случай декларировать; при us=1000 это 1 мс без ISR → риск потери байта UART).
- **ISR:** millis/micros ISR-safe (ядро); elapsed/expired чистые; Timeout/Stopwatch не ISR-safe
  при конкурентном start/stop из ISR — защищать вызывающему.
- **Ресурсы:** 0 SRAM без объектов; sizeof(Timeout)/sizeof(Stopwatch) → RESOURCES.md (backlog G-1).
  Timer0 не занимается (клиент, D24).
- **Инварианты:** INV-13, INV-20 (D25).
- **Верификация:** host unit wrap (since=0xFFFFFFF0, now=0x10 → elapsed=0x20); hardware delayUsPrecise.

### 2.5 C-CORE-BITS — битовый слой (BitMask/Scan/Count/Ops/Shift/Reverse/Pack/Set/Flags, Bits)
- **Модуль/слой:** core/Bit*.h, core/Bits.h · L2
- **Назначение:** операции над битами и битовыми массивами без таблиц в RAM.
- **API:** маски/скан/подсчёт/сдвиг/реверс; BitPack<N> и BitPackWide (единый CRTP- API, D8);
  BitSet; normalize(); operator~ (константный, по значению) vs invert() (in-place).
- **Предусловия:** N>0; для BitPackWide — буфер достаточного размера; индекс < N при обращении.
- **Постусловия:** биты за пределами N всегда ноль (normalize публичный); ctz/clz/popcount
  корректны для всех входов включая 0; reverseBitOrder через XOR масок (D83).
- **Гарантии при ошибке:** индекс вне диапазона → BadArg (при FN_BITS_CHECKS); состояние не меняется.
- **Таймаут/блокировка:** нет (чистые функции).
- **ISR:** безопасно для локальных значений; разделяемый BitSet — RMW под AtomicBlock (INV-07).
- **Ресурсы:** BitPack<N> = ceil(N/8) Б; BitPackWide — буфер разделяемый, без операторов по значению.
- **Инварианты:** INV-01, INV-03, INV-07, INV-12.
- **Верификация:** HOST (все 256 значений для ctz/clz/popcount/reverse; нормализация по всем N).

### 2.6 C-CORE-PROGMEM — доступ к flash
- **Модуль/слой:** core/Progmem.h · L3
- **Назначение:** чтение данных из flash (pgm_read_*), единый узел (Progmem+PgmUtils слиты, D10).
- **API:** pgmRead для u8/u16/u32/i8/i16/i32/float; чтение строк/буферов из PROGMEM.
- **Предусловия:** указатель ссылается на объект в PROGMEM; для float — допустимость soft-float
  в данном пути (не hot path, INV-03).
- **Постусловия:** возвращает значение из flash; не разыменовывает flash-указатель напрямую.
- **Гарантии при ошибке:** nullptr → 0/пусто (документируется); не выходит за границу буфера.
- **Таймаут/блокировка:** нет.
- **ISR:** безопасно (чтение flash).
- **Ресурсы:** 0 SRAM; таблицы во flash.
- **Инварианты:** INV-01, INV-03 (float-перегрузка только вне hot path).
- **Верификация:** HW (чтение контрольных таблиц); HOST с эмуляцией pgm_read.

### 2.7 C-CORE-CRC — контрольные суммы
- **Модуль/слой:** core/Crc.h · L3
- **Назначение:** единый CRC-движок (D9), побитовый без таблиц (D20).
- **API:** crc8_maxim (init=0x00, poly 0x8C reflected), crc16_ccitt (init=0xFFFF),
  crc16_modbus; опционально CRC-16 в Packet (FN_PACKET_CRC16, D67).
- **Предусловия:** буфер валиден, len корректен; init соответствует алгоритму.
- **Постусловия:** контрольные значения: "123456789" → crc8_maxim=0xA1, ccitt=0x29B1,
  modbus=0x4B37.
- **Гарантии при ошибке:** nullptr/len=0 → документированный результат (init); не читает за границей.
- **Таймаут/блокировка:** нет; ~N×цикл, пакеты ≤64 Б.
- **ISR:** допустимо для коротких пакетов; длинные — вне ISR.
- **Ресурсы:** 0 SRAM, ~176 Б flash на движок (без таблиц).
- **Инварианты:** INV-01, INV-03.
- **Верификация:** HOST (контрольные значения всех трёх алгоритмов) — обязательно по критерию 8.

### 2.8 L4 — прерывания и ресурсы

#### C-CORE-RES — fn::res / Resources.h
- **Модуль/слой:** core/Resources.h · L4
- **Назначение:** compile-time реестр владения векторами/таймерами; единая точка
  согласования «кто занимает ресурс». Числа и модель владения → RESOURCES.md §3–4.
- **API:** FN_CLAIM_VECTOR(vecName, owner), FN_CLAIM_TIMER(n, owner),
  FN_VECTOR_OWNER(vecName), ownerName(owner) -> PGM_P; constexpr-зеркала vec*/devTimer*.
- **Предусловия:** vecName — camelCase-идентификатор зеркала (vecTimer1Capt, vecAdc, …),
  не UPPERCASE из даташита; owner — один из FN_OWNER_*; дефолт владельца задан централизованно
  в Resources.h и совпадает с RESOURCES.md §3.
- **Постусловия:** FN_CLAIM_* разворачивается в static_assert(dev/vec == owner); при
  совпадении — компиляция проходит, при расхождении — падает с именем ресурса и кодом владельца.
  ownerName возвращает строку из flash (Tables.cpp).
- **Гарантии при ошибке:** расхождение владения = ошибка компиляции (не runtime); состояние
  железа не меняется. Неизвестный owner → "Unknown".
- **Таймаут/блокировка:** нет (compile-time).
- **ISR:** не применимо (нет кода исполнения).
- **Ресурсы:** 0 SRAM; ownerName — таблица во flash.
- **Инварианты:** INV-05 (один вектор = один владелец), INV-19 (драйвер не заявляет владение),
  INV-22 (одно определение), DEP-4.
- **Верификация:** SA (static_assert в каждом потребителе); компиляция дефолтного скетча.
- **Расхождения:** P0-2 — дефолты NONE противоречат заявкам Uart/I2c/Spi/Adc/AC/Wdt/Pulse/Timer2;
  привести к RESOURCES.md §3. Док-пример UPPERCASE (TIMER1_CAPT) не компилируется — исправить.
  Разделяемый Timer1 — только runtime acquire (см. C-TIMER-T1), compile-time заявку снять.

#### C-PIN-IRQ — fn::irq (Irq.cpp/h)
- **Модуль/слой:** pin/Irq.cpp/h · L4
- **Назначение:** диспетчер PCINT0/1/2 + обёртка INT0/INT1 через attachInterrupt ядра.
  Единственный владелец PCINT-векторов; прикладные модули своих ISR не пишут (DEP-6).
- **API:** PinChange::attach(pin, cb, ctx)/detach(pin)/enable(pin)/disable(pin);
  ExternalInt::attach(pin, cb, ctx, edge)/detach/setEdge; коллбэк void(*)(void* ctx, Pin changed)
  (+ перегрузка void(*)()); пул FN_IRQ_SLOTS (8).
- **Предусловия:** pin валиден (Pin::valid); для attach — свободный слот есть; инициализация
  объекта завершена ДО регистрации; INT0/INT1 — только через ExternalInt (свои ISR невозможны, D22).
- **Постусловия:** attach добавляет слот {pin,cb,ctx} и вызывает refreshPort (пересчёт PCMSK
  из пула — единственный источник истины); ISR передаёт prev^now (изменение, не уровень);
  detach уплотняет пул и refreshPort; повторная регистрация того же pin отклоняется.
- **Гарантии при ошибке:** нет свободного слота → Result::NoBuffer; дубликат pin → BadArg;
  невалидный pin → BadArg; состояние пула/железа не меняется. При отказе повторной регистрации
  первая снимается (атомарность регистрации).
- **Таймаут/блокировка:** attach/detach под AtomicBlock (короткая критическая секция); ISR без блокировок.
- **ISR:** коллбэки вызываются из ISR — короткие, без блокирующих операций; ctx=this для N
  экземпляров (реестры внутри модулей не заводятся, D17/D52). Прерывания не вкладываются.
- **Ресурсы:** слот 5 Б × FN_IRQ_SLOTS(8) = 40 Б + 6 Б на INT; векторы PCINT0/1/2 (владелец IRQ).
  sizeof → RESOURCES.md §2. Лимит декодеров квадратуры = FN_IRQ_SLOTS/2 (D54).
- **Инварианты:** INV-05, INV-06 (ISR в .cpp), INV-07, INV-19, DEP-6/7.
- **Верификация:** HOST (логика пула/уплотнения/refreshPort); HW (PCINT-гонки, INT edge).
- **Расхождения:** G-4 — enable/disable пишут PCMSK напрямую, минуя пул; последующий
  refreshPort (при attach/detach) возвращает отключённый пин. Цель: флаг enabled в слоте,
  учитываемый refreshPort, либо убрать enable/disable как отдельную сущность.

### 2.9 L5.1 — пины / ввод

#### C-PIN-BOARD — fn::Board (Board.h)
- **Модуль/слой:** pin/Board.h · L5.1
- **Назначение:** карта выводов Arduino Nano (D0..D13, A0..A7 → Port/bit); физические
  ограничения платы отдельно от regs::validMask (который знает только про PC7).
- **API:** boardPin(n) -> Pin (constexpr), BoardPin<N> (тип-константа),映射 допустимых выводов.
- **Предусловия:** n — номер вывода платы в диапазоне Nano; A6/A7 — только ADC (цифрового порта нет).
- **Постусловия:** boardPin возвращает корректный Pin для существующего вывода; для
  несуществующего (PC7, n вне карты) → NoPin. BoardPin<N>::pin() constexpr.
- **Гарантии при ошибке:** несуществующий вывод → NoPin (valid()==false); потребители вернут BadArg.
- **Таймаут/блокировка:** нет (constexpr).
- **ISR:** безопасно.
- **Ресурсы:** 0 SRAM (constexpr-карта во flash при необходимости).
- **Инварианты:** INV-10/11 (имена), DEP-7 (диспетчер знает карту).
- **Верификация:** HOST static_assert (boardPin по всем N против карты); SA.
- **Расхождения:** нет.

#### C-PIN-PIN — fn::Pin (Pin.h)
- **Модуль/слой:** pin/Pin.h · L5.1
- **Назначение:** компактный тип вывода (1 байт). Базовый контракт типа — C-CORE-TYPES (§2.1);
  здесь — специфика Pin.h как отдельного модуля.
- **API:** Pin(port,bit), Pin::fromId(id), NoPin=0xFF; port()/bit()/mask()/id()/valid().
- **Предусловия:** port∈{B,C,D}, bit∈0..7 при конструировании; fromId принимает любой uint8_t.
- **Постусловия:** sizeof==1; id()==(port<<3)|bit; mask()==1<<bit; valid()==false для PC7,
  id≥PortCount*8, NoPin. Физические ограничения платы — на Board.h, не на Pin.
- **Гарантии при ошибке:** не бросает; невалидный Pin → valid()==false.
- **Таймаут/блокировка:** нет (constexpr/чистые).
- **ISR:** безопасно.
- **Ресурсы:** 1 Б на объект; 0 статики.
- **Инварианты:** INV-12 (проверки границ), INV-18 (диапазон).
- **Верификация:** HOST unit valid() по всем 256 id; SA sizeof.
- **Расхождения:** нет.

#### C-PIN-PINREF — fn::PinRef / Port (PinRef.h, Port.h)
- **Модуль/слой:** pin/PinRef.h, pin/Port.h · L5.1
- **Назначение:** PinRef — ссылка на вывод с операциями уровня HAL; Port — операции над
  портом целиком поверх regs. Тонкая обёртка, без своего состояния.
- **API:** PinRef: set/clear/toggle/write/read/modeOutput/modeInput/modeInputPullup;
  Port: readInput/readLatch/readDir/write/setMask/clearMask/toggle/mode*.
- **Предусловия:** PinRef хранит валидный Pin; Port — валидный Port; layoutOk()==true.
- **Постусловия:** делегируют в regs с теми же гарантиями (PORT→DDR, toggle через PINx,
  *Atomic под блокировкой); read читает PINx.
- **Гарантии при ошибке:** невалидный Pin/Port → BadArg/OutOfRange (как regs); железо не меняется.
- **Таймаут/блокировка:** нет (~2–6 тактов).
- **ISR:** неатомарные RMW небезопасны при ISR по тому же порту → *Atomic/FastPin (INV-07).
- **Ресурсы:** PinRef = sizeof(Pin) = 1 Б; Port — без состояния.
- **Инварианты:** INV-07/08/12, DEP-1 (поверх L1).
- **Верификация:** HOST (делегирование); HW (glitch/toggle).
- **Расхождения:** наследует G-6 (FastPin sbi) через regs.

#### C-PIN-PULSE — fn::Pulse (Pulse.cpp/h)
- **Модуль/слой:** pin/Pulse.cpp/h · L5.1
- **Назначение:** измерение периода/частоты через InputCapture Timer1 (ICP1=D8), точность
  до такта. Потребитель Timer1 (не драйвер).
- **API:** begin()/end(); measure()/periodMicros()/frequencyMhz(); коллбэк на захват;
  lost() (счётчик пропущенных захватов).
- **Предусловия:** Timer1 свободен → acquire(Client::Pulse) == Ok; ICP1 (D8) настроен как вход;
  FN_CLAIM_VECTOR(Timer1Capt/Ovf) согласован с потребителем (не драйвером, INV-19).
- **Постусловия:** ISR CAPT читает ICR→TOV→TCNT (порядок D48), корректирует переполнение
  (TOV1 && cnt>=icr → +1 epoch); period = now-prev (wrap-safe uint32); periodMicros без
  переполнения (q=t/TicksPerUs затем q<<sh, насыщение D50); frequencyMhz = 1e9/us (мГц).
- **Гарантии при ошибке:** Timer1 занят → Busy (acquire), ничего не настраивается; пропущенный
  захват (ICF1 повторно) → ++lost, период не публикуется; невалидный аргумент → BadArg.
- **Таймаут/блокировка:** измерение неблокирующее (ISR); худший случай ожидания сигнала
  декларируется отдельно (см. PulseInput для блокирующего варианта).
- **ISR:** владеет TIMER1_CAPT/OVF (уникальные векторы, DEP-6 исключение); коллбэк короткий;
  overflows читается в ISR без блокировки (ISR не вкладываются).
- **Ресурсы:** Timer1 целиком (runtime acquire); векторы Timer1Capt/Ovf; ~420 Б flash с двумя
  ISR без soft-float/__udivdi3. sizeof → RESOURCES.md.
- **Инварианты:** INV-05/06/13/19/20, INV-03 (без float/64 в ISR), DEP-4.
- **Верификация:** HW (захват известной частоты, переполнение epoch); HOST (арифметика periodMicros).
- **Расхождения:** P0-2 — безусловная compile-time заявка Timer1 (FN_CLAIM_TIMER(1,PULSE))
  конфликтует с runtime-моделью и дефолтом NONE. Цель: снять compile-time заявку устройства,
  оставить runtime acquire + фиксацию векторов потребителем.

#### C-PIN-QUAD — fn::Quadrature (Quadrature.cpp/h)
- **Модуль/слой:** pin/Quadrature.cpp/h · L5.1
- **Назначение:** декодер квадратурного энкодера без таблицы (D51), на диспетчере Irq.
  Своих ISR не пишет (DEP-6).
- **API:** begin(pinA, pinB)/end(); position() -> int32; takeDelta() -> int32; errors() -> счётчик;
  onChange (коллбэк).
- **Предусловия:** pinA/pinB валидны; свободных слотов Irq ≥ 2; инициализация состояния завершена
  ДО attach; лимит декодеров = FN_IRQ_SLOTS/2 (D54).
- **Постусловия:** state = A|(B<<1); diff=prev^state; diff==0 → нет изменения; diff==3 →
  ++errors, позиция НЕ меняется (D53); иначе dir=((prev>>1)^state)&1 → ±1. Позиция/delta/errors
  читаются под AtomicBlock (int32). При отказе второго attach первый снимается.
- **Гарантии при ошибке:** невалидные пины → BadArg; нет слотов → NoBuffer; состояние не меняется.
  Пропуск фронта (diff==3) не уезжает позицию молча.
- **Таймаут/блокировка:** begin под AtomicBlock (короткая секция); ISR без блокировок.
- **ISR:** коллбэк из диспетчера PCINT; ctx=this (реестр декодеров удалён, D52); оба канала
  регистрируются отдельно.
- **Ресурсы:** 2 слота Irq на декодер; состояние ~? Б (position int32 + prev + errors) → RESOURCES.md.
  Таблиц нет (~10 инструкций на переход против LPM+16 Б).
- **Инварианты:** INV-01/03/07/06, DEP-6, INV-19.
- **Верификация:** HOST (все 8 переходов, diff==3 → error без изменения позиции); HW (энкодер).
- **Расхождения:** нет (декод проверен по всем переходам).

#### C-PIN-PULSEIN — fn::PulseInput (PulseInput.h)
- **Модуль/слой:** pin/PulseInput.h · L5.1
- **Назначение:** блокирующее измерение длительности импульса (в т.ч. sonar), с раздельными
  бюджетами ожидания и измерения (D55). Декларирует худший случай блокировки.
- **API:** measure(...) -> PulseResult{status, us}; sonarBegin/sonarMeasure (trig/echo);
  статусы Timeout/OutOfRange/BadArg/NotReady (D56).
- **Предусловия:** пины настроены (sonarMeasure сама настраивает trig/echo, D58); таймауты
  ожидания и измерения заданы раздельно; худший случай = сумма бюджетов (не максимум).
- **Постусловия:** адрес PINx вычисляется один раз, цикл читает *reg&mask (D57, разрешение);
  триггер 10 мкс через _delay_us с константой (D59); результат различает отсутствие сигнала
  (Timeout) от залипания линии (OutOfRange) — не «0 на всё» (D56).
- **Гарантии при ошибке:** истёк бюджет ожидания → Timeout; импульс вне диапазона → OutOfRange;
  невалидный пин → BadArg; не готов → NotReady. Состояние пинов восстанавливается.
- **Таймаут/блокировка:** БЛОКИРУЮЩИЙ; худший случай = ожидание + измерение (декларировать
  в шапке для планировщика). Никаких бесконечных ожиданий (INV-13).
- **ISR:** не использует ISR (опрос в цикле с таймаутом); interrupts должны быть разрешены
  для работы millis/micros.
- **Ресурсы:** 0 статики; временные пины trig/echo; flash без soft-float.
- **Инварианты:** INV-13/03/12/18, INV-20 (таймауты через expired).
- **Верификация:** HW (известная длительность, залипание линии → OutOfRange); HOST (статус-машина).
- **Расхождения:** нет.

### 2.10 L5.2 — таймеры

#### C-TIMER-COMMON — fn::tmr (TimerCommon.h)
- **Модуль/слой:** timer/TimerCommon.h · L5.2
- **Назначение:** общий словарь таймерного слоя (enum + хелперы). Ни от одного таймера не
  зависит (DEP-5, D23); каждый таймерный модуль включает его.
- **API:** Clock/Out/Channel/Waveform; comBits(Out)/clockBits(Clock)/prescaleOf(Clock);
  bestClock(cycles,maxCount,&out)/ticksToUs/usToTicks; TicksPerUs.
- **Предусловия:** cycles>0, maxCount>0; Clock∈допустимых; ExtFall/ExtRise только Timer0/1
  (у Timer2 внешнего входа нет).
- **Постусловия:** значения Clock == поле CSn2..0 (пишутся напрямую); Out преобразуется comBits
  (Toggle==1); bestClock подбирает минимальный делитель, при котором cycles укладывается в
  maxCount, возвращает такты (1..maxCount) или 0; ticksToUs делит на константу (сдвиг), без 64 бит.
- **Гарантии при ошибке:** не влезает ни при каком делителе → 0, out=Div1024; Stopped/Ext →
  prescaleOf=0, ticksToUs=0. Состояние не меняется (чистые функции).
- **Таймаут/блокировка:** нет; bestClock — цикл по 5 делителям (~60 Б flash), только при настройке.
- **ISR:** безопасно (чистые/constexpr).
- **Ресурсы:** 0 SRAM/flash при неиспользовании. Таблиц в RAM быть НЕ должно.
- **Инварианты:** INV-01 (нет таблиц в RAM), INV-03 (без 64/float), INV-15 (коды из даташита), DEP-5.
- **Верификация:** HOST (bestClock по граничным cycles/maxCount; ticksToUs wrap/overflow path).
- **Расхождения:** G-2 — static const Clock kClocks[5] в RAM (5 Б), шапка заявляет «таблиц нет».
  Цель: PROGMEM или развернуть в арифметику без массива.

#### C-TIMER-T0 — fn::Timer0 (Timer0.cpp/h)
- **Модуль/слой:** timer/Timer0.cpp/h · L5.2
- **Назначение:** регистровый драйвер Timer0 с защитой ядра Arduino. Timer0 принадлежит
  ARDUINO (millis/micros/delay); драйвер не заявляет владение (INV-19).
- **API:** begin() (отказывает при владении ядра), takeover() (явный захват), restoreArduino()
  (возвращает конфигурацию wiring.c), setMode/setPeriod/регистровые операции.
- **Предусловия:** для takeover — осознанное решение (ломает millis); FN_TIMER0_DEVICE согласован;
  begin при ARDUINO-владении → отказ.
- **Постусловия:** begin не трогает TOP/CS при владении ядра; takeover конфигурирует явно;
  restoreArduino восстанавливает делитель/режим wiring.c; periodTicks для PhaseCorrect = 2·TOP (D61).
- **Гарантии при ошибке:** begin при владении ядра → Unsupported/Busy, регистры не меняются;
  невалидный режим/делитель → BadArg/OutOfRange.
- **Таймаут/блокировка:** нет (регистровые операции).
- **ISR:** своих ISR не определяет (TIMER0_OVF у ядра); при takeover ответственность за millis
  переходит к вызывающему.
- **Ресурсы:** Timer0 (владелец ARDUINO по умолчанию); 0 статики драйвера.
- **Инварианты:** INV-15/19, INV-05, DEP-4.
- **Верификация:** HW (millis до/после restoreArduino); HOST (periodTicks PhaseCorrect).
- **Расхождения:** нет (D60 реализован).

#### C-TIMER-T1 — fn::Timer1 (Timer1.cpp/h)
- **Модуль/слой:** timer/Timer1.cpp/h · L5.2
- **Назначение:** регистровый драйвер Timer1 + runtime-арбитраж потребителей. Владелец по
  умолчанию NONE; претенденты Pulse/Pwm1/Servo/Stepper — ровно один (RESOURCES.md §4).
- **API:** acquire(Client)/release(Client)/client(); takeover(Mode,Clock) (без проверки, для
  того кто уже acquire); setMode/setPeriodUs/setFrequency; capture()/counter()/ocrA/B()/top();
  periodTicks().
- **Предусловия:** для конфигурации — acquire(Client)==Ok (advisory); Client уникален;
  режим/делитель из даташита (WGM13..0, CS1x).
- **Постусловия:** acquire занят другим → Busy; release освобождает; periodTicks корректен для
  всех WGM (CTC TOP=OCR1A/ICR1, FastPWM 8/9/10-bit, PhaseCorrect 2·TOP, D61); setPeriodUs
  через bestClock без переполнения; 16-битные чтения под AtomicBlock (TEMP).
- **Гарантии при ошибке:** acquire при занятом таймере → Busy, ничего не меняется; период не
  влезает → OutOfRange, возврат 0; невалидный режим → BadArg.
- **Таймаут/блокировка:** нет (регистровые); 16-бит доступ ~4 такта накладных AtomicBlock.
- **ISR:** драйвер своих ISR не определяет; векторы Timer1Capt/Ovf/Compa/B заявляет потребитель.
- **Ресурсы:** Timer1 целиком; acquire-реестр 1 Б (или общий tmr::acquire 3 Б, D69); 0 прочей статики.
- **Инварианты:** INV-05/07/15/19, INV-14 (частота), DEP-4.
- **Верификация:** HW (частота/скважность, захват); HOST (periodTicks по всем WGM, setPeriodUs overflow).
- **Расхождения:** G-7 — setMode маска 0xE0 очищает CS (таймер останавливается), комментарий
  врёт про «сохраняет CS»; period-функции восстанавливают CS, одиночный setMode глушит тактирование.
  P0-4 — общий tmr::acquire (D69) не реализован; решить: реализовать реестр или оставить Timer1::acquire.
  setFrequency floor-округление может чуть превышать запрос (дух INV-14) — задокументировать.

#### C-TIMER-T2 — fn::Timer2 (Timer2.cpp/h)
- **Модуль/слой:** timer/Timer2.cpp/h · L5.2
- **Назначение:** регистровый драйвер Timer2. Делители свои: 1/8/32/64/128/256/1024 (CS2x≠CS0x, D18).
  Асинхронный режим удалён (TOSC занят кварцем, D19).
- **API:** begin/takeover/setPeriod/setMode; clockBits через Clock2.
- **Предусловия:** FN_TIMER2_DEVICE согласован с потребителем; делитель из набора Timer2;
  внешний тактовый вход недоступен (нет T2).
- **Постусловия:** корректные делители Timer2 (3→32, 5→128); periodTicks учитывает режимы;
  takeover конфигурирует явно.
- **Гарантии при ошибке:** недопустимый делитель/режим → BadArg/OutOfRange; регистры не меняются.
- **Таймаут/блокировка:** нет.
- **ISR:** своих ISR не определяет по умолчанию; векторы Timer2 заявляет потребитель.
- **Ресурсы:** Timer2; 0 статики драйвера.
- **Инварианты:** INV-15/19, INV-05, DEP-4.
- **Верификация:** HW (частота на делителях Timer2); HOST (clockBits/periodTicks).
- **Расхождения:** P0-2 — FN_CLAIM_TIMER(2,TIMER2) внутри takeover падает при дефолте NONE;
  привести дефолт/заявку в согласие. G-2 — static const Clock2 kClocks[7] в RAM (7 Б) → PROGMEM/арифметика.

#### C-TIMER-WDT — fn::Watchdog (Watchdog.cpp/h)
- **Модуль/слой:** timer/Watchdog.cpp/h · L5.2
- **Назначение:** прикладной WDT (единственный ISR(WDT_vect)), сброс/прерывание, защита от зависаний.
- **API:** enable(mode,timeout)/disable()/feed()/setTimeout(); режимы Interrupt/Reset/SystemReset.
- **Предусловия:** FN_WDT_OWNER согласован (USER); перед enable сброшен WDRF (D28, иначе флаг
  форсирует WDE и Interrupt даёт Reset); фьюз WDTON может запрещать выключение.
- **Постусловия:** enable конфигурирует WDTCSR с корректным timeout; ISR без wdr (D29, wdr
  отодвигал сброс на полный период); feed сбрасывает счётчик; disable выключает (если не WDTON).
- **Гарантии при ошибке:** недопустимый timeout → BadArg; при WDTON disable не выключает
  (документируется, будет перезапуск).
- **Таймаут/блокировка:** timeout из даташита (15 мс…8 с); последовательность смены WDE/WDP
  через avr-libc (4-тактовое окно).
- **ISR:** владеет WDT_vect (уникальный вектор); ISR короткий, без wdr.
- **Ресурсы:** вектор WDT (владелец USER); 0 статики.
- **Инварианты:** INV-05/06/15, INV-19.
- **Верификация:** HW (срабатывание сброса/прерывания по timeout); REVIEW (WDRF перед enable).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(Wdt,USER) падает при дефолте NONE; привести дефолт к USER.

### 2.11 L5.3 — ШИМ

#### C-PWM-DICT — fn::Pwm (Pwm.h)
- **Модуль/слой:** timer/Pwm.h · L5.3
- **Назначение:** словарь ШИМ-слоя и карта каналов. Слой ЗАМЕЩАЕТ analogWrite (D30);
  смешивать нельзя.
- **API:** PwmMode(Fast/PhaseCorrect/FastTop/PhaseCorrectTop), PwmOut(Disabled/Toggle/Normal/Inverted),
  PwmChannel(D3/D5/D6/D9/D10/D11); channelPin/channelTimer/channelIsA/modeUsesOcrATop/modeIsPhase.
- **Предусловия:** канал ∈ карте; *Top на Timer0 запрещён (ломает millis).
- **Постусловия:** constexpr-карта канал→вывод/таймер/канал A/B корректна; modeUsesOcrATop
  отражает TOP=OCRnA (T0/T2) vs ICR1 (T1, через hasIcr).
- **Гарантии при ошибке:** нет (чистые constexpr).
- **Таймаут/блокировка:** нет.
- **ISR:** безопасно.
- **Ресурсы:** 0 SRAM/flash (enum + constexpr).
- **Инварианты:** INV-15, DEP-1.
- **Верификация:** HOST static_assert (карта каналов).
- **Расхождения:** modeUsesOcrATop имя слегка вводит в заблуждение для Timer1 (там ICR1) —
  логика верна через hasIcr; уточнить имя/док.

#### C-PWM-ENGINE — fn::pwm::Engine<Traits> (PwmCommon.h)
- **Модуль/слой:** timer/PwmCommon.h · L5.3
- **Назначение:** единый шаблонный движок ШИМ на traits (D31). Один движок на три таймера.
- **API:** begin(PwmMode)/end(); setFrequency(hz)/setTop(n)/resolution()/top()/frequency();
  attachA/B/detachA/B; setDutyA/B/setDutyPercent*/setDutyPermille*.
- **Предусловия:** для разделяемого таймера — acquire(Client::Pwm)==Ok; *Top на lockedByCore
  (Timer0) запрещён; направление вывода задаёт вызывающий (regs::modeOutput), слой DDR не трогает.
- **Постусловия:** applyWgm записывает WGM в КОРРЕКТНЫЕ позиции (TCCRnA [1:0], TCCRnB [4:3] для
  T1 / [3] для T0/T2), сохраняя COM (A&0xF0), CS (B&7) и tccrBKeep; scaleDuty без предварительного
  деления (D33); кламп скважности до TOP (не TOP+1, D34); 100% работает при TOP<MAX.
- **Гарантии при ошибке:** таймер занят → Busy, ничего не меняется; end() без владения — no-op
  (регистры чужого потребителя не трогаются); *Top на Timer0 → Unsupported; частота не влезает → 0;
  невалидный канал/режим → BadArg.
- **Таймаут/блокировка:** нет (регистровые); смена частоты на работающем ШИМ даёт один искажённый
  период (ICR буферизуется) — документировать.
- **ISR:** ШИМ не использует прерываний таймера; вектор Ovf фиксируется как NONE потребителем.
- **Ресурсы:** 3 Б SRAM на движок; ~180–320 Б flash; таймер через acquire. sizeof → RESOURCES.md.
- **Инварианты:** INV-14/15/18/19, INV-05, DEP-4.
- **Верификация:** HW (частота/скважность на всех режимах, 100% при TOP<MAX); HOST (scaleDuty/кламп).
- **Расхождения:** ❗P0-3 — applyWgm/wgmB кладут ((w>>2)&3) в биты 1:0 TCCRnB (поле CS) вместо
  4:3 (T1) / 3 (T0/T2): все режимы Timer1 неверны + портится CS, *Top Timer2 сломан. Цель:
  сдвиг B-части (T1 (w&0x0C)<<1, T0/T2 (w&0x04)<<1). P0-4 — begin не вызывает acquire (обещан
  tmr::acquire); реализовать арбитраж.

#### C-PWM-TRAITS — Traits0/1/2 (PwmTimer0/1/2.h)
- **Модуль/слой:** timer/PwmTimer0/1/2.h · L5.3
- **Назначение:** traits таймеров для Engine + алиасы Pwm0/Pwm1/Pwm2. Описывают разрядность,
  hasIcr, lockedByCore, tccrBKeep, wgmBits, делители, доступ к регистрам.
- **API:** Traits{bits,hasIcr,lockedByCore,prescaleCount,tccrBKeep,tccrA/B,wgmBits,prescaleAt/
  clockBits/prescaleOf,writeTop/readTop/writeOcrA/B/readOcrA/B}; using PwmN=Engine<TraitsN>.
- **Предусловия:** wgmBits упакован [3:2]→B,[1:0]→A; tccrBKeep сохраняет не-WGM биты TCCRnB;
  делители из даташита (T2 свои).
- **Постусловия:** Traits1 hasIcr=true (оба канала при произвольной частоте, TOP=ICR1);
  Traits0 lockedByCore=true (*Top запрещён); WGM02/22 — бит 3 TCCRnB (НЕ бит 4).
- **Гарантии при ошибке:** нет (статические traits).
- **Таймаут/блокировка:** нет.
- **ISR:** безопасно.
- **Ресурсы:** 0 SRAM (traits статические).
- **Инварианты:** INV-15 (коды/биты из даташита), INV-22.
- **Верификация:** HOST static_assert (tccrBKeep, wgmBits packing); HW (режимы).
- **Расхождения:** ❗P0-3 — комментарий Traits0/D32 называет WGM02 битом 4 (фактически бит 3);
  исправить номер бита и согласовать с фиксом applyWgm.

### 2.12 L6 — шины

#### C-BUS-CONTRACT — fn::bus (BusContract.h)
- **Модуль/слой:** core/BusContract.h · L6
- **Назначение:** единый контракт шины + хелперы. Шина передаётся параметром шаблона, не тегом (D35).
- **API:** каждая шина: Config; begin(Config)/end()/ready(); write/read/transfer; available/flush/busy.
  Хелперы: writeP<Bus>/writeByte/writeStr/writeLine/transferFill<Bus>; waitUntil(pred,ms)/waitUntilUs.
- **Предусловия:** шина удовлетворяет интерфейсу; timeoutMs≥0; pred — callable.
- **Постусловия:** timeoutMs==0 — одна проверка без ожидания; бесконечного ожидания нет;
  без буфера (SPI/I2c) available=0, flush=Ok сразу; без приёма (SoftUartTx) read=Unsupported.
- **Гарантии при ошибке:** истёк таймаут → Timeout; шина не готова → NotReady; неподдерживаемая
  операция → Unsupported.
- **Таймаут/блокировка:** waitUntil через expired (INV-13); худший случай = timeoutMs.
- **ISR:** хелперы вызываются из main; ISR шин владеют своими векторами.
- **Ресурсы:** 0 SRAM (шаблоны).
- **Инварианты:** INV-13/21, DEP-3.
- **Верификация:** HOST (waitUntil timeout=0/истечение); HW (шины).
- **Расхождения:** нет.

#### C-BUS-BUFFER — fn::RingBuffer<T,N> (Buffer.h)
- **Модуль/слой:** bus/Buffer.h · L6
- **Назначение:** SPSC кольцевой буфер, монотонные индексы, N степень двойки (D36).
- **API:** push/pop/available/empty/full/clear/dropped/clearDropped; capacity.
- **Предусловия:** N — степень двойки (static_assert); producer только push, consumer только pop
  (SPSC); Index 8-бит при N≤128, 16-бит при N≥256.
- **Постусловия:** available=(Index)(head-tail) различает пусто/полно; %N заменён маской (нет
  __udivqi3); pushOverwrite отсутствует (D37); dropped считает отказы push.
- **Гарантии при ошибке:** push в полный → false, ++dropped, данные не теряются молча; pop из
  пустого → false. При 8-бит индексах блокировка не нужна (атомарность).
- **Таймаут/блокировка:** нет; clear под AtomicBlock.
- **ISR:** producer=ISR, consumer=main; 8-бит индексы атомарны, 16-бит — защищать.
- **Ресурсы:** RingBuffer<u8,64>=68 Б (buf64+head1+tail1+dropped2). sizeof → RESOURCES.md.
- **Инварианты:** INV-03 (маска вместо деления), INV-07, INV-12.
- **Верификация:** HOST (SPSC гонки, wrap индексов, full/empty, dropped).
- **Расхождения:** открытый вопрос ROADMAP — нужен ли FN_BUFFER_NO_STATS для экономии 2 Б.

#### C-BUS-UART — fn::Uart (+UartSoft)
- **Модуль/слой:** bus/Uart.cpp/h, UartSoft.cpp/h · L6
- **Назначение:** USART0 (аппаратный) + soft TX. Приём всегда через ISR в ring buffer (D38).
- **API:** begin(Config)/end()/ready(); write/read/transfer; available/flush/busy; baudErrorPermille.
  SoftUartTx: write (read=Unsupported).
- **Предусловия:** FN_USART_RX_OWNER=UART; Config.baud в допустимом диапазоне; RX-буфер задан.
- **Постусловия:** UBRR с округлением в обоих режимах (U2X); ошибка>3‰ → OutOfRange, настройка
  применяется; ISR читает UCSR0A до UDR0, битые кадры (FE/DOR/UPE) отбрасывает и считает;
  SoftUart тайминг в тактах (D39), секция на байт (~1.04 мс при 9600).
- **Гарантии при ошибке:** baud вне диапазона → OutOfRange (настройка применена — исключение
  из strong-guarantee, оговорено); read пустого буфера с timeout → Timeout; SoftUartTx.read → Unsupported.
- **Таймаут/блокировка:** write ждёт UDRE с таймаутом; read с timeoutMs; SoftUart блокирует на байт.
- **ISR:** владеет USART_RX (уникальный вектор); RX в ring buffer (SPSC).
- **Ресурсы:** USART0+D0/D1; RX-буфер (sizeof по конфигу); вектор UsartRx. → RESOURCES.md.
- **Инварианты:** INV-05/06/13/14/21, INV-03.
- **Верификация:** HW (baud/ошибка, потеря байтов при переполнении); HOST (calcUbrr/округление).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(UsartRx,UART) падает при дефолте NONE → привести к UART.

#### C-BUS-I2C — fn::I2c (+I2cSoft)
- **Модуль/слой:** bus/I2c.cpp/h, I2cSoft.cpp/h · L6
- **Назначение:** TWI (аппаратный) + soft I2C. Частота через TWBR и прескалер TWPS (D40).
- **API:** begin(Config)/end()/ready(); start(addr,rw)/stop(); write/read/transfer; recover()/scan();
  readReg. SoftI2c: настоящий open-drain, clock stretching с таймаутом.
- **Предусловия:** FN_TWI_OWNER=I2C; hz в диапазоне 490 Гц…1 МГц (при 16 МГц); A4/A5 свободны.
- **Постусловия:** подбор TWPS+TWBR даёт диапазон 490 Гц…1 МГц; таймаут на каждую фазу (не на
  транзакцию); recover: TWEN=0, open-drain, 9 тактов, проверка освобождения линий; SoftI2c
  «высокий»=input без подтяжки (D41); I2cTransaction делает STOP только если открыт этим объектом.
- **Гарантии при ошибке:** hz вне диапазона → OutOfRange, настройка не меняется; адрес не ACK →
  AddrNack; байт не ACK → DataNack; фаза истекла → Timeout; recover возвращает линии в idle.
- **Таймаут/блокировка:** таймаут на фазу; recover блокирует на 9 тактов + проверка.
- **ISR:** владеет TWI (уникальный вектор).
- **Ресурсы:** TWI+A4/A5; вектор Twi; 0 буфера (available по rxLeft). → RESOURCES.md.
- **Инварианты:** INV-05/06/13/14/16/21.
- **Верификация:** HW (частота, recover после залипания, scan); HOST (calcClock границы 490/1M).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(Twi,I2C) падает при дефолте NONE → привести к I2C.
  available() возвращает rxLeft (контракт BusContract говорит 0 для I2c) — уточнить/согласовать.
  transfer() write→read без repeated START — для большинства устройств нужен readReg; уточнить док.

#### C-BUS-SPI — fn::Spi (+SpiSoft)
- **Модуль/слой:** bus/Spi.cpp/h, SpiSoft.cpp/h · L6
- **Назначение:** SPI (аппаратный) + soft SPI. Основная операция transfer (full-duplex).
- **API:** begin(Config)/end()/ready(); transfer/read/write/transfer16(v,highByteFirst);
  available/flush/busy. SoftSpi: 4 режима CPOL/CPHA + LSB-first, toggle CLK записью в PINx.
- **Предусловия:** FN_SPI_STC_OWNER=SPI; hz≥F_CPU/128 (125 кГц при 16 МГц); SS(D10) — выход в мастере.
- **Постусловия:** делители 2/4/8/16/32/64/128 сверху вниз (не превышает запрос, D42); read=transfer
  с fill 0xFF; transferByte→int16_t(-1 при ошибке, D73); SoftSpi указатели регистров один раз на
  байт (D70), MISO после выбирающего фронта (D71), pause через _delay_loop_2 (D72), порядок бит
  упакован в байт режима (D68), begin вызывает end (D74).
- **Гарантии при ошибке:** hz ниже минимума → OutOfRange, настройка не меняется (медленнее — SoftSpi);
  ошибка обмена → -1/Timeout; TX-only read → Unsupported.
- **Таймаут/блокировка:** transfer ждёт SPIF с таймаутом; SoftSpi блокирует на байт.
- **ISR:** владеет SPI_STC (уникальный вектор); без буфера available=0, flush=Ok.
- **Ресурсы:** SPI+D10..D13; вектор SpiStc; SoftSpi sizeof=8. → RESOURCES.md.
- **Инварианты:** INV-05/06/13/14/21, INV-01 (kDivs).
- **Верификация:** HW (режимы CPOL/CPHA, делители, SS); HOST (calcDiv границы).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(SpiStc,SPI) падает при дефолте NONE → привести к SPI.
  G-2 — const DivEntry kDivs[7] в RAM (28 Б) → PROGMEM. G-5 — busy()=isReady&&(SPIF==0) всегда
  true (после обмена SPIF сброшен чтением SPDR); для синхронного SPI busy()=false.

#### C-BUS-PACKET — fn::Packet (Packet.h)
- **Модуль/слой:** bus/Packet.h · L6
- **Назначение:** кадровый автомат (чистый, без таймаута), CRC-8 по умолчанию, CRC-16 опционально.
- **API:** feed(byte)/reset()/state(); флаги кадра; FN_PACKET_CRC16 (D67).
- **Предусловия:** сброс флагов на входе в Len (D65); reset по таймауту шины снаружи (D66).
- **Постусловия:** флаги кадра живут ровно один кадр (overflow не залипает); CRC-8 по умолчанию,
  CRC-16 для радио (1/65536 против 1/256); формат прежних кадров сохранён.
- **Гарантии при ошибке:** CRC не совпал → CrcError; невалидная длина → сброс кадра; состояние
  автомата детерминировано.
- **Таймаут/блокировка:** нет (чистый автомат, тестируем на хосте, D66).
- **ISR:** можно кормить из ISR шин; автомат без блокировок.
- **Ресурсы:** sizeof по конфигу кадра; 0 статики. → RESOURCES.md.
- **Инварианты:** INV-01/03/21, INV-13 (таймаут снаружи).
- **Верификация:** HOST (автомат по всем переходам, CRC-8/16 контрольные, overflow не залипает).
- **Расхождения:** нет.

### 2.13 L6.1 — хранилище

#### C-STORE-EEPROM — fn::Eeprom (Eeprom.cpp/h)
- **Модуль/слой:** store/Eeprom.cpp/h · L6.1
- **Назначение:** EEPROM байт/блок с проверкой границ и контролем износа. Тайминг делегирован avr-libc.
- **API:** read/write/update(addr,len)/validRange/written(); crc8/crc16 (делегат Crc).
- **Предусловия:** addr+len≤1024 (EEAR 10-бит, D43); len корректен; при активном WDT кормить между порциями.
- **Постусловия:** границы проверяются всегда (addr>1023 → OutOfRange, не порча); update пишет
  только при отличии; written — счётчик износа; блочная запись N байт = N×3.4 мс.
- **Гарантии при ошибке:** addr/len вне диапазона → OutOfRange, запись не выполняется; nullptr → BadArg.
- **Таймаут/блокировка:** запись 3.4 мс/байт блокирующая; busy_wait должен идти с разрешёнными
  прерываниями (цель), под блокировкой только окно EEAR/EEDR/EEMPE/EEPE.
- **ISR:** байтовые операции атомарны (avr-libc защищает 4-тактовое окно); ISR+EEPROM сериализовать.
- **Ресурсы:** EEPROM 1024 Б (не захватывается); written 4 Б; 0 прочей статики. → RESOURCES.md.
- **Инварианты:** INV-07/12/18, INV-03.
- **Верификация:** HW (запись/чтение границ, износ); HOST (validRange арифметика).
- **Расхождения:** G-3 — AtomicBlock оборачивает весь вызов avr-libc включая busy_wait (~3.4 мс
  под cli на байт) → UART/millis страдают. Цель: busy_wait вне блокировки, критическое окно узкое.

#### C-STORE-EEPROMSTORE — fn::EepromStore<T> (EepromStore.h)
- **Модуль/слой:** store/EepromStore.h · L6.1
- **Назначение:** типизированный блок настроек с CRC и двухфазной записью. Формат сохранён.
- **API:** load()/commit()/tick()/dirty(); формат crc(2)+size(2)+ver(1)+data.
- **Предусловия:** T trivially copyable (static_assert); блок в пределах EEPROM; версия формата совместима.
- **Постусловия:** CRC считается по size_lo,size_hi,ver,data; tick смотрит dirty-флаг (CRC только
  в commit, D44); запись двухфазная (данные, затем заголовок); существующие данные читаются.
- **Гарантии при ошибке:** CRC не совпал → CrcError (данные не применяются); невалидный размер →
  OutOfRange; двухфазность защищает от частичной записи.
- **Таймаут/блокировка:** commit блокирует на N×3.4 мс (см. Eeprom); tick неблокирующий.
- **ISR:** не использовать из ISR (длительная запись).
- **Ресурсы:** sizeof(T)+заголовок в EEPROM; dirty-флаг; 0 лишней статики. → RESOURCES.md.
- **Инварианты:** INV-07/12/18/21, INV-03.
- **Верификация:** HOST (CRC формата, двухфазность, trivially_copyable SA); HW (восстановление после сбоя).
- **Расхождения:** наследует G-3 через Eeprom.

### 2.14 L6.2 — питание / аналог

#### C-PERIPH-POWER — fn::Power (Power.cpp/h)
- **Модуль/слой:** peripheral/Power.cpp/h · L6.2
- **Назначение:** сон, делители тактовой, отключение периферии (PRR). Маска SMCR хранится готовой (D45).
- **API:** setMode(Mode)/sleepOnce(bodOff)/setClockDivider/disable(mask)/enable/adjustOsccal.
- **Предусловия:** перед сном Xxx::end() затем Power::disable (INV-17 контекст); setClockDivider/
  disable(Timer0) отвергаются пока FN_TIMER0_DEVICE==ARDUINO; adjustOsccal только на внутреннем RC.
- **Постусловия:** sleepOnce применяет сохранённую маску SMCR (не пересчитывает из enum);
  последовательность сна: SREG→cli→sleep_enable→[sleep_bod_disable]→sei→sleep_cpu→sleep_disable→SREG;
  PeripheralAll фильтрует Timer0 молча; disable гасит ADEN/ACD затем PRR.
- **Гарантии при ошибке:** смена делителя при Timer0-ядре → Unsupported; недопустимый режим → BadArg;
  источник пробуждения не проверяется (все прерывания запрещены → сон до WDT).
- **Таймаут/блокировка:** sleepOnce блокирует до пробуждения; последовательность BOD в 4-тактовом окне.
- **ISR:** пробуждение по разрешённому источнику; sei перед sleep_cpu (ISR после wake).
- **Ресурсы:** маска SMCR 1 Б; 0 прочей статики. → RESOURCES.md.
- **Инварианты:** INV-15/17/19, INV-07.
- **Верификация:** HW (ток в PowerDown, пробуждение по AC/WDT, делитель); REVIEW (последовательность сна).
- **Расхождения:** нет (D45 реализован).

#### C-PERIPH-ADC — fn::Adc (Adc.cpp/h)
- **Модуль/слой:** peripheral/Adc.cpp/h · L6.2
- **Назначение:** АЦП 10/8 бит, измерение Vcc/температуры, шумоподавление. DIDR0 — обязанность модуля.
- **API:** begin(Config)/end()/read10/read8/readMux/readNoiseReduced/readVccMillivolts/readTemperatureC; коллбэк.
- **Предусловия:** FN_ADC_OWNER=ADC; частота 50–200 кГц для 10 бит (prescaler); read8 корректен
  только при Bits8 (ADLAR); компаратору в режиме Mux нужен ADEN=0.
- **Постусловия:** readMux отбрасывает первую конверсию после смены входа; read10 атомарен;
  readVcc = 1100·1024/raw (цель, D: формула); readTemperature целочисленно (offset/slope из конфига);
  readNoiseReduced входит в SLEEP_MODE_ADC (авто-старт конверсии).
- **Гарантии при ошибке:** частота вне 50–200 кГц → OutOfRange; не готов → NotReady; невалидный mux → BadArg.
- **Таймаут/блокировка:** ожидание ADSC с таймаутом (INV-13); readNoiseReduced блокирует до конверсии.
- **ISR:** владеет ADC_vect (уникальный вектор); коллбэк короткий.
- **Ресурсы:** ADC; вектор Adc; 0 статики. → RESOURCES.md.
- **Инварианты:** INV-05/06/13/15/21, INV-03 (без float).
- **Верификация:** HW (известное напряжение/Vcc/температура, prescaler); HOST (целочисленные формулы).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(Adc,ADC) падает при дефолте NONE → привести к ADC.
  G-8 — readVccMillivolts использует 1023 вместо 1024 (~0.1% ошибка) → 1024.

#### C-PERIPH-AC — fn::AnalogComparator (AnalogComparator.cpp/h)
- **Модуль/слой:** peripheral/AnalogComparator.cpp/h · L6.2
- **Назначение:** аналоговый компаратор, единственный аналоговый источник пробуждения в PowerDown.
  Свой гейт F_USE_AC (D46).
- **API:** begin(Config)/end()/setNegativeMux/setCallback; режимы Mux/AIN0-AIN1.
- **Предусловия:** FN_ANALOG_COMP_OWNER=ADC (та же аналоговая подсистема); режим Mux требует ADEN=0;
  setNegativeMux гасит ADEN сам и запоминает состояние (D47).
- **Постусловия:** работает в PowerDown и будит МК; ACME действует только при ADEN=0; режим Mux
  корректно включается через setNegativeMux.
- **Гарантии при ошибке:** недопустимый режим/mux → BadArg; конфликт с ADC (ADEN) разрешается
  модулем (запоминает/восстанавливает).
- **Таймаут/блокировка:** нет (компаратор асинхронен); пробуждение по событию.
- **ISR:** владеет ANALOG_COMP_vect (уникальный вектор); коллбэк короткий.
- **Ресурсы:** вектор AnalogComp (владелец ADC); 0 статики. → RESOURCES.md.
- **Инварианты:** INV-05/06/15/19, INV-09 (свой гейт).
- **Верификация:** HW (пробуждение из PowerDown, порог срабатывания); REVIEW (ADEN-логика).
- **Расхождения:** P0-2 — FN_CLAIM_VECTOR(AnalogComp,ADC) падает при дефолте NONE → привести к ADC.

### 2.15 L8 — строки / математика

#### C-STR-STRINGN — fn::StringN<N> (StringN.h)
- **Модуль/слой:** string/StringN.h · L8
- **Назначение:** строка фиксированной ёмкости без динамической памяти.
- **API:** StringN<N>; add(int/uint/float/double/long long)/c_str()/length/clear; операторы concat.
- **Предусловия:** N>0; add не превышает ёмкость (обрезка документирована); float-перегрузки
  тянут soft-float (не hot path).
- **Постусловия:** c_str() всегда NUL-терминирован в пределах буфера; add(float,dec) через
  appendFloat; обрезка цифр справа (D81), дробная не печатается при обрезке целой (D82).
- **Гарантии при ошибке:** переполнение ёмкости → обрезка (не выход за границу); nullptr → BadArg/игнор.
- **Таймаут/блокировка:** нет.
- **ISR:** не рекомендуется (форматирование); локальные объекты безопасны.
- **Ресурсы:** N+1 Б на объект; 0 статики. → RESOURCES.md.
- **Инварианты:** INV-03 (float вне hot path), INV-12/18.
- **Верификация:** HOST (обрезка D81/D82, NUL-терминация, границы).
- **Расхождения:** нет.

#### C-STR-ANY — fn::Any (Any.h)
- **Модуль/слой:** string/Any.h · L8
- **Назначение:** тип-значение (variant) без динамики; 64-бит по гейту FN_ANY_64BIT=0 (D76).
- **API:** Any(типы/указатели); toInt/toUint/toFloat; toInt64/toUint64 (под гейтом); readFromStr; read<T>.
- **Предусловия:** хранимый тип ∈ поддерживаемых; 64-бит только при FN_ANY_64BIT=1; сужение
  проверяет диапазон приёмника (D77).
- **Постусловия:** Any(char) хранит символ и ноль в c[2] (c_str не уходит за границу, D78);
  toInt/toUint при _size==8 читают int64 только под гейтом; parseFloat десятичный.
- **Гарантии при ошибке:** сужение вне диапазона → документированный результат (не мусор);
  невалидный тип → BadArg/0; без 64-бит гейта case 8 не инстанцируется (проверено ✅).
- **Таймаут/блокировка:** нет.
- **ISR:** локальные объекты безопасны; parseFloat не из ISR.
- **Ресурсы:** sizeof по конфигу (storage union); 0 статики. → RESOURCES.md.
- **Инварианты:** INV-03/18/22, INV-12.
- **Верификация:** HOST (сужение D77, char D78, гейт 64-бит, parse).
- **Расхождения:** нет (гейт FN_ANY_64BIT проверен).

#### C-STR-BUILDER — fn::StringBuilder (StringBuilder.cpp/h)
- **Модуль/слой:** string/StringBuilder.cpp/h · L8
- **Назначение:** форматирование чисел в буфер без динамики.
- **API:** appendUnsigned/appendSigned/appendFloat/appendUnsigned64/appendSigned64.
- **Предусловия:** buf достаточен; left≥-1; dec≤6 для float (точность ~7 цифр); base допустим.
- **Постусловия:** /10 константой (GCC сводит к умножению, D80); обрезка цифр справа (D81);
  appendFloat при обрезке целой части дробную не печатает (D82); 64-бит версии тянут __udivdi3
  (только при использовании, gc-sections).
- **Гарантии при ошибке:** переполнение буфера → обрезка (не выход за границу); nullptr → 0/BadArg.
- **Таймаут/блокировка:** нет.
- **ISR:** не из ISR (форматирование).
- **Ресурсы:** 0 статики; flash по использованию (64-бит опционально). → RESOURCES.md.
- **Инварианты:** INV-03/12/18.
- **Верификация:** HOST (D80/D81/D82 контрольные, границы буфера).
- **Расхождения:** нет.

#### C-MATH-MAPPING — fn::math::Mapping (Mapping.h)
- **Модуль/слой:** math/Mapping.h · L8
- **Назначение:** масштабирование диапазона, целочисленный (Fast) и float пути.
- **API:** mapFast/mapFastClamped (int64 внутри), mapFloat/mapFloatClamped, Mapping::apply/applyFloat.
- **Предусловия:** inMax≠inMin (иначе деление на 0 → документированный результат); float не в hot path.
- **Постусловия:** mapFast целочисленно с int64 промежуточным (без soft-float); clamped ограничивает
  выход диапазоном; mapFloat — soft-float (десятки мкс).
- **Гарантии при ошибке:** inMax==inMin → документированный результат (не деление на 0); переполнение
  int64 при экстремальных диапазонах — документировать предел.
- **Таймаут/блокировка:** нет; mapFast ~такты+__muldi3, mapFloat десятки мкс (не ISR).
- **ISR:** mapFast допустим для коротких путей; mapFloat не из ISR.
- **Ресурсы:** 0 статики; flash по использованию. → RESOURCES.md.
- **Инварианты:** INV-03/12/18.
- **Верификация:** HOST (mapFast контрольные, clamped, inMax==inMin).
- **Расхождения:** нет.

#### C-MATH-INTERP — fn::math::Interpolation (Interpolation.h)
- **Модуль/слой:** math/Interpolation.h · L8
- **Назначение:** линейная/билинейная интерполяция, smoothStep; float/int32 пути.
- **API:** lerp/lerpClamped/inverseLerp/smoothStep/smootherStep/bilinear (float); lerpInt/lerpIntRound (int64).
- **Предусловия:** tMax≠0 для int-пути; float не в hot path/ISR.
- **Постусловия:** int-путь через int64 с округлением (без soft-float); smoothStep/smootherStep —
  soft-float (десятки мкс, не ISR).
- **Гарантии при ошибке:** tMax==0 → документированный результат; clamp t в [0,tMax].
- **Таймаут/блокировка:** нет; float-варианты десятки мкс.
- **ISR:** int-путь допустим; float не из ISR.
- **Ресурсы:** 0 статики. → RESOURCES.md.
- **Инварианты:** INV-03/12/18.
- **Верификация:** HOST (lerpInt контрольные, smoothStep границы, tMax==0).
- **Расхождения:** нет.

#### C-MATH-APPROX — fn::math::Approximation (Approximation.h)
- **Модуль/слой:** math/Approximation.h · L8
- **Назначение:** табличная аппроксимация (термопара/NTC и т.п.), таблицы в PROGMEM.
- **API:** get(float)/get(float,&out)/getInt(int32)/segment(float).
- **Предусловия:** таблица валидна (size>0, X монотонен); таблицы в PROGMEM; getInt использует
  int64-деление (~44 мкс, не ISR).
- **Постусловия:** getInt целочисленно без soft-float (экономит ~600 Б если float больше нигде
  не нужен); get(float) — soft-float; сегмент бинарным поиском.
- **Гарантии при ошибке:** невалидная таблица → get=0 / get(&out)=false; выход за края → clamp
  к крайнему Y.
- **Таймаут/блокировка:** нет; getInt ~44 мкс, get(float) десятки мкс (не ISR).
- **ISR:** не из ISR.
- **Ресурсы:** таблицы во flash (PROGMEM); 0 SRAM таблиц. → RESOURCES.md.
- **Инварианты:** INV-01/03/12.
- **Верификация:** HOST (getInt контрольные, clamp, невалидная таблица); HW (термопара).
- **Расхождения:** нет.

#### C-MATH-STATS — fn::math::Statistics (Statistics.h)
- **Модуль/слой:** math/Statistics.h · L8
- **Назначение:** статистика: целочисленная, int-дисперсия без float, float по Уэлфорду.
- **API:** Statistics/IntVariance/FloatStatistics; add/reset/mean/variance/stdDev/min/max;
  arrayMean/arrayVariance/arrayStdDev/arrayRms.
- **Предусловия:** count>0 для mean/variance; float-путь не в hot path; arrayRms целочисленно (isqrt).
- **Постусловия:** IntVariance дисперсия без float (int64 num/den); FloatStatistics по Уэлфорду
  (численно устойчиво); arrayRms = isqrt(sum(x²)/n) без float; mean = float(int64 sums)/count.
- **Гарантии при ошибке:** count==0 → 0 (документировано); переполнение int64 при больших sums —
  документировать предел.
- **Таймаут/блокировка:** нет; FloatStatistics.add ~30 мкс, stdDev sqrtf ~15 мкс (не ISR).
- **ISR:** целочисленные пути допустимы для коротких окон; float не из ISR.
- **Ресурсы:** Statistics ~? Б (sum int64+count+min/max); FloatStatistics 17+ Б. → RESOURCES.md.
- **Инварианты:** INV-03/12/18.
- **Верификация:** HOST (Уэлфорд контрольные, IntVariance, arrayRms, count==0).
- **Расхождения:** нет.

#### C-MATH-LINREG — fn::math::LinearRegression (LinearRegression.h)
- **Модуль/слой:** math/LinearRegression.h · L8
- **Назначение:** линейная регрессия (МНК), float-путь.
- **API:** add(x,y)/fit/slope/intercept/delta/span/predict; linearFit(x,y,n,&slope,&intercept).
- **Предусловия:** count≥2 для fit; count≤65535 (uint16); float не в hot path/ISR; X близкие →
  потеря точности float (документировано).
- **Постусловия:** slope/intercept по МНК; предсказание slope·x+intercept; span=maxX-minX.
- **Гарантии при ошибке:** count<2 → fit не выполняется (документированный результат); вырожденный
  X (все равны) → документированный результат (не деление на 0).
- **Таймаут/блокировка:** нет; ~30 мкс/точка при 16 МГц (не ISR).
- **ISR:** не из ISR.
- **Ресурсы:** sizeof=17 Б (4 float+bool); ~450 Б flash soft-float. → RESOURCES.md.
- **Инварианты:** INV-03/12/18.
- **Верификация:** HOST (МНК контрольные, вырожденный X, точность на близких X).
- **Расхождения:** нет.

#### C-MATH-FIXED — fn::math::Fixed<N> (FixedPoint.h)
- **Модуль/слой:** math/FixedPoint.h · L8
- **Назначение:** фиксированная точка Q (Fixed8/16/24) для Pid/Filter/Ramp без soft-float.
- **API:** Fixed<N>(int/float)/fromRaw/fromFloatSaturated; toInt/toFloat/raw; арифметика +/-/*//;
  литералы _fx8/_fx16/_fx24.
- **Предусловия:** N∈{8,16,24}; float-конструкторы только для настройки/вывода (не hot path);
  constexpr Fixed(float) для compile-time коэффициентов.
- **Постусловия:** целочисленная арифметика через int64 промежуточное с насыщением (satRaw);
  toFloat/fromFloat тянут soft-float только при использовании; неявных конверсий в int/float нет.
- **Гарантии при ошибке:** переполнение → насыщение (INT32_MAX/MIN); деление на 0 → Fixed() (0);
  NaN → 0 (fromFloatSaturated).
- **Таймаут/блокировка:** нет; целочисленный путь ~такты+__muldi3.
- **ISR:** целочисленный путь допустим; float-конструкторы не из ISR.
- **Ресурсы:** sizeof=4 Б (int32 raw); 0 статики. → RESOURCES.md.
- **Инварианты:** INV-03/18/12.
- **Верификация:** HOST (насыщение, деление на 0, constexpr коэффициенты, арифметика контрольные).
- **Расхождения:** нет.

#### C-MATH-UTILS — fn::math::MathUtils (MathUtils.h)
- **Модуль/слой:** math/MathUtils.h · L8
- **Назначение:** целочисленная математика без <math.h>/soft-float (isqrt, gcd/lcm, округление).
- **API:** isqrt32/isqrt64/gcd/lcm/divRound/alignUp/absDiff.
- **Предусловия:** isqrt64 для сумм квадратов (uint64); gcd/lcm знаковые/беззнаковые корректно.
- **Постусловия:** isqrt floor(sqrt) побитово без float; lcm через gcd без переполнения (порядок
  a/g*b); divRound округление к ближайшему; alignUp выравнивание.
- **Гарантии при ошибке:** isqrt(0)=0; gcd(0,0)=0; lcm с 0 → 0; переполнение lcm при больших a,b —
  документировать предел.
- **Таймаут/блокировка:** нет; isqrt64 ~64 итерации (не ISR для больших).
- **ISR:** isqrt32/gcd допустимы; isqrt64 для коротких значений.
- **Ресурсы:** 0 статики; flash по использованию. → RESOURCES.md.
- **Инварианты:** INV-03/12.
- **Верификация:** HOST (isqrt контрольные включая 0/UINT64_MAX, gcd/lcm, divRound).
- **Расхождения:** не подключён umbrella (F_USE_MATH) — решить, включать ли (ROADMAP открытый вопрос).

---

## 3. Расхождения код↔контракт
Ведутся в ROADMAP.md (backlog G-*). Контракт описывает цель; код подтягивается на этапе 3.





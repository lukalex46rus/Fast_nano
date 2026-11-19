// ============================================================================
//  Spi.cpp — SPI-мастер: подбор делителя, обмен по SPIF с таймаутом
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация аппаратного SPI. Обмен по флагу SPIF без прерываний: вектор
//    SPI_STC заявляется, но ISR не определяется.
//
//  ПОДБОР ДЕЛИТЕЛЯ
//    Доступны 2, 4, 8, 16, 32, 64, 128 (SPI2X удваивает базовый делитель).
//    Выбирается МАКСИМАЛЬНАЯ частота, не превышающая запрос. Прежняя версия
//    сравнивала F_CPU/clock с порогами через <= и округляла вверх: для 30 кГц
//    получалось 125 кГц (в 4.17 раза быстрее запроса). Проверено численно.
//    Запрос ниже F_CPU/128 -> OutOfRange, настройка не меняется.
//
//  ТАЙМАУТЫ
//    Ожидание SPIF по fn::expired() с таймаутом из Config. Счётчик итераций
//    не годится: его длительность зависит от -O и версии GCC. При таймауте
//    модуль помечает ошибку и возвращает Timeout; шина остаётся инициализированной.
//
//  ПОРЯДОК БАЙТ
//    transfer16 шлёт старший байт первым по умолчанию (большинство外设).
//    Порядок задаётся параметром, а не зашит.
//
//  ОГРАНИЧЕНИЯ
//    - SS (D10) настраивается как выход и удерживается HIGH: низкий уровень
//      аппаратно сбрасывает MSTR. При ownSs=false это ответственность вызывающего.
//    - end() не восстанавливает прежнее состояние выводов (PWM и т.п.).
//    - Файл должен лежать в src/bus/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Spi.h, core/BusContract.h, timer/Tick.h, avr/io.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_SPI

#include "Spi.h"
#include "../timer/Tick.h"

#include <avr/io.h>
#include <avr/pgmspace.h>

FN_CLAIM_VECTOR(SpiStc, FN_OWNER_SPI);

namespace fn {
namespace spi_detail {

uint32_t wantClock = 0;
uint32_t realClock = 0;
uint32_t tmoMs = FN_SPI_TIMEOUT_MS;
bool isReady = false;
Result last = Result::Ok;
volatile uint16_t cntTimeout = 0;

// Выводы SPI на Nano/Uno.
constexpr uint8_t kMosiPin = 11;   // PB3
constexpr uint8_t kMisoPin = 12;   // PB4
constexpr uint8_t kSckPin  = 13;   // PB5
constexpr uint8_t kSsPin   = 10;   // PB2

// Таблица делителей во flash (INV-01, D11). Индекс = порядок перебора
// от максимальной частоты к минимальной: 2/4/8/16/32/64/128.
struct DivEntry {
    uint16_t div;
    uint8_t  spr;      // SPR1:0
    uint8_t  spi2x;    // бит SPI2X
};

const DivEntry kDivs[7] PROGMEM = {
    {  2, 0, 1 },
    {  4, 0, 0 },
    {  8, 1, 1 },
    { 16, 1, 0 },
    { 32, 2, 1 },
    { 64, 2, 0 },
    {128, 3, 0 }
};

inline Result fail(Result r) {
    last = r;
    if (r == Result::Timeout) ++cntTimeout;
    return r;
}

// Ожидание завершения обмена. true — SPIF выставлен.
inline bool waitSpif() {
    const uint32_t t0 = ::fn::millis();
    while ((SPSR & (1u << SPIF)) == 0) {
        if (elapsed(t0, ::fn::millis()) >= tmoMs) return false;
    }
    return true;
}

// Подбор делителя: максимальная частота, не превышающая hz.
bool calcDiv(uint32_t hz, uint8_t& sprOut, uint8_t& x2Out, uint32_t& realOut) {
    if (hz == 0) return false;
    for (uint8_t i = 0; i < 7; ++i) {
        const uint16_t dv = pgm_read_word(&kDivs[i].div);
        const uint32_t f = F_CPU / dv;
        if (f <= hz) {
            sprOut  = pgm_read_byte(&kDivs[i].spr);
            x2Out   = pgm_read_byte(&kDivs[i].spi2x);
            realOut = f;
            return true;
        }
    }
    return false;   // запрос ниже F_CPU/128
}

}  // namespace spi_detail

// ============================ инициализация ================================
Result Spi::begin(const Config& cfg) {
    using namespace spi_detail;

    uint8_t spr = 0, x2 = 0;
    uint32_t real = 0;
    if (!calcDiv(cfg.clock, spr, x2, real)) return Result::OutOfRange;

    AtomicBlock lock;
    wantClock = cfg.clock;
    realClock = real;
    tmoMs = cfg.timeoutMs ? cfg.timeoutMs : 1;

    // Направление выводов. SS обязателен как выход в режиме мастера.
    regs::modeOutput(boardPin(kMosiPin), false);
    regs::modeOutput(boardPin(kSckPin), false);
    regs::modeInput(boardPin(kMisoPin));
    if (cfg.ownSs) regs::modeOutput(boardPin(kSsPin), true);

    uint8_t cr = uint8_t((1u << SPE) | (1u << MSTR));
    if (cfg.order == BitOrder::LsbFirst) cr = uint8_t(cr | (1u << DORD));
    const uint8_t m = uint8_t(cfg.mode);
    if (m & 0x02u) cr = uint8_t(cr | (1u << CPOL));
    if (m & 0x01u) cr = uint8_t(cr | (1u << CPHA));
    cr = uint8_t(cr | (spr & 0x03u));
    SPCR = cr;

    if (x2) SPSR = uint8_t(SPSR | (1u << SPI2X));
    else    SPSR = uint8_t(SPSR & uint8_t(~(1u << SPI2X)));

    // Сбросить возможный незавершённый обмен.
    (void)SPSR;
    (void)SPDR;

    last = Result::Ok;
    cntTimeout = 0;
    isReady = true;
    return Result::Ok;
}

Result Spi::begin(uint32_t clock) { return begin(Config(clock)); }

void Spi::end() {
    AtomicBlock lock;
    SPCR = 0;
    regs::modeInput(boardPin(spi_detail::kMosiPin));
    regs::modeInput(boardPin(spi_detail::kSckPin));
    regs::modeInput(boardPin(spi_detail::kMisoPin));
    regs::modeInput(boardPin(spi_detail::kSsPin));
    spi_detail::isReady = false;
}

bool Spi::ready() { return spi_detail::isReady; }

Result Spi::setClock(uint32_t hz) {
    uint8_t spr = 0, x2 = 0;
    uint32_t real = 0;
    if (!spi_detail::calcDiv(hz, spr, x2, real)) return Result::OutOfRange;
    AtomicBlock lock;
    SPCR = uint8_t((SPCR & uint8_t(~0x03u)) | spr);
    if (x2) SPSR = uint8_t(SPSR | (1u << SPI2X));
    else    SPSR = uint8_t(SPSR & uint8_t(~(1u << SPI2X)));
    spi_detail::wantClock = hz;
    spi_detail::realClock = real;
    return Result::Ok;
}

uint32_t Spi::actualClock() { return spi_detail::realClock; }

void Spi::setMode(Mode m) {
    AtomicBlock lock;
    uint8_t cr = uint8_t(SPCR & uint8_t(~((1u << CPOL) | (1u << CPHA))));
    const uint8_t v = uint8_t(m);
    if (v & 0x02u) cr = uint8_t(cr | (1u << CPOL));
    if (v & 0x01u) cr = uint8_t(cr | (1u << CPHA));
    SPCR = cr;
}

void Spi::setOrder(BitOrder o) {
    AtomicBlock lock;
    if (o == BitOrder::LsbFirst) SPCR = uint8_t(SPCR | (1u << DORD));
    else                        SPCR = uint8_t(SPCR & uint8_t(~(1u << DORD)));
}

// ============================ обмен ========================================
int16_t Spi::transferByte(uint8_t tx) {
    using namespace spi_detail;
    if (!isReady) { fail(Result::NotReady); return -1; }

    SPDR = tx;
    if (!waitSpif()) { fail(Result::Timeout); return -1; }
    const uint8_t rx = SPDR;      // чтение SPDR сбрасывает SPIF
    last = Result::Ok;
    return int16_t(rx);
}

Result Spi::write(const uint8_t* data, uint16_t len) {
    if (data == nullptr && len) return Result::BadArg;
    if (!spi_detail::isReady) return Result::NotReady;
    const uint8_t* p = data;
    const uint8_t* end = p + len;
    for (; p != end; ++p) {
        if (transferByte(*p) < 0) return spi_detail::last;
    }
    return Result::Ok;
}

Result Spi::read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) {
    if (dst == nullptr && len) return Result::BadArg;
    if (!spi_detail::isReady) return Result::NotReady;

    // timeoutMs ограничивает всю операцию; per-byte таймаут остаётся из Config.
    const uint32_t t0 = ::fn::millis();
    for (uint16_t i = 0; i < len; ++i) {
        const int16_t v = transferByte(0xFF);
        if (v < 0) return spi_detail::last;
        dst[i] = uint8_t(v);
        if (elapsed(t0, ::fn::millis()) >= timeoutMs) {
            return spi_detail::fail(Result::Timeout);
        }
    }
    return Result::Ok;
}

Result Spi::transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) {
    if ((tx == nullptr || rx == nullptr) && len) return Result::BadArg;
    if (!spi_detail::isReady) return Result::NotReady;
    for (uint16_t i = 0; i < len; ++i) {
        const int16_t v = transferByte(tx[i]);
        if (v < 0) return spi_detail::last;
        rx[i] = uint8_t(v);
    }
    return Result::Ok;
}

Result Spi::transferInPlace(uint8_t* data, uint16_t len) {
    if (data == nullptr && len) return Result::BadArg;
    if (!spi_detail::isReady) return Result::NotReady;
    for (uint16_t i = 0; i < len; ++i) {
        const int16_t v = transferByte(data[i]);
        if (v < 0) return spi_detail::last;
        data[i] = uint8_t(v);
    }
    return Result::Ok;
}

int32_t Spi::transfer16(uint16_t v, bool msbFirst) {
    const uint8_t hi = uint8_t(v >> 8);
    const uint8_t lo = uint8_t(v);
    int16_t a, b;
    if (msbFirst) { a = transferByte(hi); b = transferByte(lo); }
    else          { a = transferByte(lo); b = transferByte(hi); }
    if (a < 0 || b < 0) return -1;
    return msbFirst ? int32_t((uint16_t(a) << 8) | uint16_t(b))
                    : int32_t((uint16_t(b) << 8) | uint16_t(a));
}

// ============================ контракт: вырожденные ========================
uint16_t Spi::available() { return 0; }          // буфера нет
Result Spi::flush(uint32_t) { return Result::Ok; }

// Обмен синхронный (transferByte ждёт SPIF и читает SPDR). Асинхронного
// состояния "в процессе" снаружи не существует: busy() всегда false.
// Прежняя формула (SPIF==0) давала true и в простое, и после обмена (G-5).
bool Spi::busy() { return false; }

Result Spi::lastError() { return spi_detail::last; }

uint16_t Spi::timeouts() {
    AtomicBlock lock;
    return spi_detail::cntTimeout;
}

void Spi::clearStats() {
    AtomicBlock lock;
    spi_detail::cntTimeout = 0;
}

}  // namespace fn

#endif  // F_USE_SPI

// ============================================================================
//  I2c.cpp — TWI-мастер: подбор частоты, фазы обмена, восстановление шины
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация аппаратного I2C. Обмен по флагу TWINT без прерываний:
//    вектор TWI заявляется, но ISR не определяется — это исключает конфликт
//    с чужим обработчиком на уровне кода и упрощает таймауты.
//
//  ПОДБОР ЧАСТОТЫ
//    f_SCL = F_CPU / (16 + 2 * TWBR * 4^TWPS)
//    Подбирается минимальный TWPS, при котором TWBR <= 255. Прежняя версия
//    держала TWPS=0 и клампила TWBR в 255: любой запрос ниже 30418 Гц
//    silently давал 30418 Гц. Проверено численно: 10 кГц -> 30418 Гц.
//    Теперь доступны 490 Гц .. 1 МГц при 16 МГц.
//
//  ТАЙМАУТЫ
//    Каждая фаза ждёт TWINT до timeoutMs через fn::expired(). Счётчик
//    итераций не годится: его длительность зависит от -O и версии GCC.
//    При таймауте TWCR сбрасывается (TWEN сохраняется), шина помечается
//    свободной; физическое залипание лечится recover().
//
//  ВОССТАНОВЛЕНИЕ
//    TWEN=0 отдаёт линии GPIO. Далее open-drain: «низкий» = output+low,
//    «высокий» = input (линию тянет внешний резистор). 9 тактов SCL
//    освобождают slave, застрявший в середине байта, затем STOP.
//    Прежняя версия звала Pin::low() при DDR=input — тактов не было.
//
//  ОГРАНИЧЕНИЯ
//    - Файл должен лежать в src/bus/ (формат библиотеки 1.5).
//    - Статусы TWSR читаются с маской 0xF8: младшие 3 бита — прескалер.
//    - Файл не определяет ISR: при использовании Wire ядра заявка
//      FN_CLAIM_VECTOR упадёт на компиляции, что и требуется.
//
//  ЗАВИСИМОСТИ: I2c.h, core/BusContract.h, timer/Tick.h, util/twi.h, avr/io.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_I2C

#include "I2c.h"
#include "../timer/Tick.h"

#include <avr/io.h>
#include <util/twi.h>
#include <util/delay.h>

FN_CLAIM_VECTOR(Twi, FN_OWNER_I2C);

namespace fn {
namespace i2c_detail {

uint32_t wantClock = 0;
uint32_t realClock = 0;
uint32_t tmoMs = FN_I2C_TIMEOUT_MS;
bool isReady = false;
bool inTransaction = false;
uint16_t rxLeft = 0;
Result last = Result::Ok;

volatile uint16_t cntTimeout = 0;
volatile uint16_t cntAddrNack = 0;
volatile uint16_t cntDataNack = 0;
volatile uint16_t cntArbLost = 0;

// Выводы шины: A4 = PC4 = SDA, A5 = PC5 = SCL.
constexpr uint8_t kSdaPin = 18;
constexpr uint8_t kSclPin = 19;

inline Result fail(Result r) {
    last = r;
    switch (r) {
        case Result::Timeout:  ++cntTimeout; break;
        case Result::AddrNack: ++cntAddrNack; break;
        case Result::DataNack: ++cntDataNack; break;
        case Result::Fail:     ++cntArbLost; break;
        default: break;
    }
    return r;
}

// Ожидание TWINT с таймаутом. true — флаг выставлен.
inline bool waitInt() {
    const uint32_t t0 = ::fn::millis();
    while ((TWCR & (1u << TWINT)) == 0) {
        if (elapsed(t0, ::fn::millis()) >= tmoMs) return false;
    }
    return true;
}

// Запуск фазы и ожидание завершения.
inline bool launch(uint8_t cr) {
    TWCR = uint8_t(cr | (1u << TWINT) | (1u << TWEN));
    if (!waitInt()) {
        fail(Result::Timeout);
        TWCR = uint8_t(1u << TWEN);      // снять управление, TWEN оставить
        inTransaction = false;
        rxLeft = 0;
        return false;
    }
    return true;
}

inline uint8_t status() { return uint8_t(TWSR & 0xF8u); }

// Подбор TWBR/TWPS. Возвращает false, если частота недостижима.
bool calcClock(uint32_t hz, uint8_t& twbrOut, uint8_t& twpsOut, uint32_t& realOut) {
    if (hz == 0) return false;
    for (uint8_t ps = 0; ps < 4; ++ps) {
        const uint16_t div = uint16_t(1u << (2 * ps));   // 1, 4, 16, 64
        // TWBR = (F_CPU/hz - 16) / (2*div), округление к ближайшему.
        const uint32_t num = F_CPU / hz;
        if (num < 16) continue;                          // слишком быстро
        const uint32_t denom = 2UL * div;
        const uint32_t raw = (num - 16UL + denom / 2UL) / denom;
        if (raw > 255UL) continue;                       // нужен больший прескалер
        twbrOut = uint8_t(raw);
        twpsOut = ps;
        realOut = F_CPU / (16UL + 2UL * raw * div);
        return true;
    }
    return false;
}

}  // namespace i2c_detail

// ============================ инициализация ================================
Result I2c::begin(const Config& cfg) {
    using namespace i2c_detail;

    uint8_t twbr = 0, twps = 0;
    uint32_t real = 0;
    if (!calcClock(cfg.clock, twbr, twps, real)) return Result::OutOfRange;

    AtomicBlock lock;
    wantClock = cfg.clock;
    realClock = real;
    tmoMs = cfg.timeoutMs ? cfg.timeoutMs : 1;

    TWCR = 0;                                  // сброс модуля
    TWSR = twps;
    TWBR = twbr;
    TWDR = 0xFF;
    TWCR = uint8_t(1u << TWEN);

    // Подтяжки: A4/A5 как вход с PORT=1. Внешние резисторы предпочтительнее.
    if (cfg.pullups) {
        regs::modeInputPullup(boardPin(kSdaPin));
        regs::modeInputPullup(boardPin(kSclPin));
    }

    inTransaction = false;
    rxLeft = 0;
    last = Result::Ok;
    cntTimeout = 0;
    cntAddrNack = 0;
    cntDataNack = 0;
    cntArbLost = 0;
    isReady = true;
    return Result::Ok;
}

Result I2c::begin(uint32_t clock) { return begin(Config(clock)); }

void I2c::end() {
    if (i2c_detail::inTransaction) stop();
    AtomicBlock lock;
    TWCR = 0;
    i2c_detail::isReady = false;
    i2c_detail::rxLeft = 0;
}

bool I2c::ready() { return i2c_detail::isReady; }

Result I2c::setClock(uint32_t hz) {
    uint8_t twbr = 0, twps = 0;
    uint32_t real = 0;
    if (!i2c_detail::calcClock(hz, twbr, twps, real)) return Result::OutOfRange;
    AtomicBlock lock;
    TWSR = twps;
    TWBR = twbr;
    i2c_detail::wantClock = hz;
    i2c_detail::realClock = real;
    return Result::Ok;
}

uint32_t I2c::actualClock() { return i2c_detail::realClock; }

// ============================ фазы обмена ==================================
Result I2c::start(uint8_t addr, bool read) {
    using namespace i2c_detail;
    if (!isReady) return Result::NotReady;
    if (addr > 0x7Fu) return Result::BadArg;

    last = Result::Ok;
    rxLeft = 0;

    // START (повторный, если транзакция уже открыта).
    if (!launch(uint8_t(1u << TWSTA))) return last;

    const uint8_t st = status();
    if (st != TW_START && st != TW_REP_START) {
        fail(Result::Fail);
        stop();
        return last;
    }
    inTransaction = true;

    // Адрес + R/W.
    TWDR = uint8_t((addr << 1) | (read ? 1u : 0u));
    if (!launch(0)) return last;

    const uint8_t as = status();
    const uint8_t expect = read ? TW_MR_SLA_ACK : TW_MT_SLA_ACK;
    if (as != expect) {
        const bool nack = (as == TW_MT_SLA_NACK) || (as == TW_MR_SLA_NACK);
        fail(nack ? Result::AddrNack : Result::Fail);
        stop();
        return last;
    }
    return Result::Ok;
}

void I2c::stop() {
    using namespace i2c_detail;
    if (!isReady) return;
    TWCR = uint8_t((1u << TWSTO) | (1u << TWINT) | (1u << TWEN));
    // TWSTO сбрасывается аппаратно; ждём с таймаутом, не бесконечно.
    const uint32_t t0 = ::fn::millis();
    while ((TWCR & (1u << TWSTO)) != 0) {
        if (elapsed(t0, ::fn::millis()) >= tmoMs) {
            fail(Result::Timeout);
            break;
        }
    }
    inTransaction = false;
    rxLeft = 0;
}

Result I2c::writeByte(uint8_t b) {
    using namespace i2c_detail;
    if (!isReady) return Result::NotReady;
    if (!inTransaction) return Result::NotReady;
    if (!ok(last)) return last;

    TWDR = b;
    if (!launch(0)) return last;

    const uint8_t st = status();
    if (st == TW_MT_DATA_ACK) return Result::Ok;
    if (st == TW_MT_DATA_NACK) return fail(Result::DataNack);
    if (st == TW_MT_ARB_LOST) { inTransaction = false; return fail(Result::Fail); }
    return fail(Result::Fail);
}

int16_t I2c::readByte(bool ack) {
    using namespace i2c_detail;
    if (!isReady || !inTransaction || !ok(last)) return -1;

    const uint8_t cr = ack ? uint8_t(1u << TWEA) : 0;
    if (!launch(cr)) return -1;

    const uint8_t st = status();
    const uint8_t expect = ack ? TW_MR_DATA_ACK : TW_MR_DATA_NACK;
    if (st != expect) {
        fail(Result::Fail);
        stop();
        return -1;
    }
    const uint8_t d = TWDR;
    if (rxLeft) --rxLeft;
    return int16_t(d);
}

// ============================ контракт шины ================================
Result I2c::write(const uint8_t* data, uint16_t len) {
    if (data == nullptr) return Result::BadArg;
    if (len == 0) return Result::Ok;
    const uint8_t* p = data;
    const uint8_t* end = p + len;
    for (; p != end; ++p) {
        const Result r = writeByte(*p);
        if (!ok(r)) return r;
    }
    return Result::Ok;
}

Result I2c::read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) {
    if (dst == nullptr) return Result::BadArg;
    if (len == 0) return Result::Ok;
    if (!i2c_detail::isReady) return Result::NotReady;

    // timeoutMs здесь ограничивает всю операцию чтения, а не одну фазу.
    const uint32_t t0 = ::fn::millis();
    for (uint16_t i = 0; i < len; ++i) {
        const bool ack = (i + 1u < len);
        const int16_t v = readByte(ack);
        if (v < 0) return i2c_detail::last;
        dst[i] = uint8_t(v);
        if (elapsed(t0, ::fn::millis()) >= timeoutMs) {
            stop();
            return i2c_detail::fail(Result::Timeout);
        }
    }
    return Result::Ok;
}

// Комбинированная операция: записать tx, затем repeated START и прочитать len.
// Адрес берётся из уже открытой транзакции — вызывающий делает start(addr,false).
Result I2c::transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) {
    if (tx == nullptr || rx == nullptr) return Result::BadArg;
    const Result w = write(tx, len);
    if (!ok(w)) return w;
    return read(rx, len, i2c_detail::tmoMs * len);
}

uint16_t I2c::available() { return i2c_detail::rxLeft; }

Result I2c::flush(uint32_t) { return Result::Ok; }   // буфера TX нет

bool I2c::busy() { return i2c_detail::inTransaction; }

// ============================ регистровые операции =========================
Result I2c::writeReg(uint8_t addr, uint8_t reg, const uint8_t* d, uint16_t n) {
    if (d == nullptr && n) return Result::BadArg;
    Result r = start(addr, false);
    if (!ok(r)) return r;
    r = writeByte(reg);
    if (!ok(r)) { stop(); return r; }
    r = write(d, n);
    stop();
    return r;
}

Result I2c::readReg(uint8_t addr, uint8_t reg, uint8_t* d, uint16_t n) {
    if (d == nullptr && n) return Result::BadArg;
    Result r = start(addr, false);
    if (!ok(r)) return r;
    r = writeByte(reg);
    if (!ok(r)) { stop(); return r; }
    r = start(addr, true);                     // repeated START
    if (!ok(r)) return r;
    i2c_detail::rxLeft = n;
    r = read(d, n, i2c_detail::tmoMs * (n ? n : 1));
    stop();
    return r;
}

Result I2c::writeReg8(uint8_t addr, uint8_t reg, uint8_t v) {
    return writeReg(addr, reg, &v, 1);
}

int16_t I2c::readReg8(uint8_t addr, uint8_t reg) {
    uint8_t v = 0;
    const Result r = readReg(addr, reg, &v, 1);
    return ok(r) ? int16_t(v) : int16_t(-1);
}

// ============================ сервис =======================================
bool I2c::ping(uint8_t addr) {
    if (addr > 0x7Fu) return false;
    const Result r = start(addr, false);
    stop();
    return ok(r);
}

uint8_t I2c::scan(uint8_t* out, uint8_t maxCount) {
    if (out == nullptr || maxCount == 0) return 0;
    uint8_t n = 0;
    // 0x08..0x77 — допустимый диапазон 7-битных адресов.
    for (uint8_t a = 0x08; a <= 0x77u && n < maxCount; ++a) {
        if (ping(a)) out[n++] = a;
    }
    return n;
}

Result I2c::recover() {
    using namespace i2c_detail;

    // Отдать линии GPIO: TWI-модуль больше не держит SCL/SDA.
    TWCR = 0;
    inTransaction = false;
    rxLeft = 0;

    const Pin sda = boardPin(kSdaPin);
    const Pin scl = boardPin(kSclPin);

    // Open-drain: «низкий» = выход+LOW, «высокий» = вход (тянет резистор).
    regs::modeInput(sda);
    regs::modeInput(scl);
    _delay_us(5);

    // 9 тактов: slave, застрявший в середине байта, дочитает и отпустит SDA.
    for (uint8_t i = 0; i < 9; ++i) {
        regs::modeOutput(scl, false);
        _delay_us(5);
        regs::modeInput(scl);
        _delay_us(5);
        if (regs::read(sda)) break;            // SDA отпущена — хватит тактов
    }

    // STOP: SDA low -> SCL high -> SDA high.
    regs::modeOutput(sda, false);
    _delay_us(5);
    regs::modeInput(scl);
    _delay_us(5);
    regs::modeInput(sda);
    _delay_us(5);

    const bool freed = regs::read(sda) && regs::read(scl);

    // Вернуть TWI.
    TWCR = uint8_t(1u << TWEN);
    last = freed ? Result::Ok : Result::Fail;
    return last;
}

Result I2c::lastError() { return i2c_detail::last; }

uint16_t I2c::timeouts() { AtomicBlock l; return i2c_detail::cntTimeout; }
uint16_t I2c::nackAddr() { AtomicBlock l; return i2c_detail::cntAddrNack; }
uint16_t I2c::nackData() { AtomicBlock l; return i2c_detail::cntDataNack; }
uint16_t I2c::arbLost()  { AtomicBlock l; return i2c_detail::cntArbLost; }

void I2c::clearStats() {
    AtomicBlock lock;
    i2c_detail::cntTimeout = 0;
    i2c_detail::cntAddrNack = 0;
    i2c_detail::cntDataNack = 0;
    i2c_detail::cntArbLost = 0;
}

}  // namespace fn

#endif  // F_USE_I2C

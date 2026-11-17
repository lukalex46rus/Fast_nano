#pragma once
// ============================================================================
//  I2c.h — аппаратный TWI (A4 = SDA, A5 = SCL): мастер, транзакции, диагностика
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственный владелец TWI-модуля. Режим мастера: запись, чтение,
//    комбинированные транзакции (repeated START), сканирование шины,
//    восстановление зависшей линии. Соответствует core/BusContract.h.
//    Slave-режим не реализован намеренно: на 328p он требует ISR и буфера,
//    а типовая задача — опрос外设.
//
//  ВЛАДЕНИЕ
//    Вектор TWI заявляется модулем (FN_CLAIM_VECTOR(Twi, FN_OWNER_I2C)).
//    Прерывание TWI не используется: обмен по флагу TWINT с таймаутом.
//    Выводы A4 (PC4, SDA) и A5 (PC5, SCL) заняты. Как GPIO или ADC — нельзя.
//    Конфликт с Wire ядра Arduino: Wire определяет ISR(TWI_vect), совместное
//    использование даёт ошибку линковки. Заявка ловит это на компиляции.
//
//  API (namespace fn, класс I2c — только статические методы)
//    struct Config
//      uint32_t clock       = 100000     частота SCL, Гц
//      bool     pullups     = true       внутренние подтяжки (~30..50 кОм)
//      uint32_t timeoutMs   = 25         таймаут одной фазы обмена
//
//    begin(const Config& = Config()) -> Result
//    begin(uint32_t clock)           -> Result
//    end()
//    ready() -> bool
//    setClock(uint32_t hz) -> Result      OutOfRange, если недостижимо
//    actualClock() -> uint32_t            фактическая частота SCL
//
//    --- контракт шины ---
//    write(const uint8_t* data, uint16_t len) -> Result   в текущую транзакцию
//    read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) -> Result
//    transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) -> Result
//    available() -> uint16_t              байт, оставшихся в requestFrom
//    flush(uint32_t timeoutMs) -> Result   Ok сразу (буфера TX нет)
//    busy() -> bool                       транзакция открыта
//
//    --- транзакции ---
//    start(uint8_t addr, bool read) -> Result   START + адрес; repeated START
//    stop()                                     STOP, освобождение шины
//    writeByte(uint8_t b) -> Result
//    readByte(bool ack) -> int16_t              -1 при ошибке
//    writeReg(uint8_t addr, uint8_t reg, const uint8_t* d, uint16_t n) -> Result
//    readReg(uint8_t addr, uint8_t reg, uint8_t* d, uint16_t n) -> Result
//    writeReg8(uint8_t addr, uint8_t reg, uint8_t v) -> Result
//    readReg8(uint8_t addr, uint8_t reg) -> int16_t
//
//    --- сервис ---
//    ping(uint8_t addr) -> bool           устройство отвечает на адрес
//    scan(uint8_t* out, uint8_t maxCount) -> uint8_t   список адресов
//    recover() -> Result                  9 тактов SCL + STOP, снятие залипания
//    lastError() -> Result                код последней операции
//    timeouts() / nackAddr() / nackData() / arbLost() -> uint16_t
//    clearStats()
//
//    class Transaction                    RAII: START в ctor, STOP в dtor
//      Transaction(addr, read = false)
//      result() -> Result                 итог START
//      write(b) / write(p, n) -> Result
//      finish(bool sendStop = true) -> Result
//
//  КОДЫ ВОЗВРАТА
//    Ok          операция выполнена
//    BadArg      nullptr, addr > 0x7F, len == 0 там, где недопустим
//    NotReady    begin() не вызывался
//    Busy        шина занята другой транзакцией
//    AddrNack    адрес не подтверждён (устройства нет или не готово)
//    DataNack    байт данных не подтверждён
//    Timeout     TWINT не выставлен за timeoutMs (SCL-stretching или обрыв)
//    Fail        потеря арбитража или неожиданный статус TWSR
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Частота задаётся через TWBR и прескалер TWPS. Прежняя версия держала
//      TWPS=0, из-за чего всё ниже 30418 Гц молча становилось 30418 Гц.
//      Теперь подбирается минимальный прескалер: диапазон 490 Гц .. 1 МГц
//      при 16 МГц. Запрос вне диапазона -> OutOfRange, настройка не меняется.
//    - Все ожидания идут по fn::expired(), а не по счётчику итераций:
//      длительность цикла зависит от оптимизации, таймаут в итерациях
//      не определён во времени.
//    - Таймаут применяется к КАЖДОЙ фазе (START, адрес, байт), не ко всей
//      транзакции. Транзакция из 32 байт может занять до 32 * timeoutMs.
//    - При таймауте модуль сбрасывает TWCR и помечает шину свободной, но
//      физически линия может остаться занятой: вызывайте recover().
//    - Внутренние подтяжки годятся только для коротких линий и одного
//      устройства. Для шины длиннее ~20 см нужны внешние 4.7 кОм (100 кГц)
//      или 2.2 кОм (400 кГц). Config::pullups=false их не отключает, если
//      они уже включены другим модулем — бит PORTx общий.
//    - Адреса 7-битные (0x08..0x77). Адреса 0x00..0x07 и 0x78..0x7F
//      зарезервированы стандартом; ping/scan их пропускают.
//    - recover() отключает TWEN, переключает A4/A5 в GPIO и выдаёт 9 тактов
//      SCL вручную (open-drain: output-low / input). Прежняя версия звала
//      Pin::low() при DDR=input — тактов не было вовсе.
//    - available() отражает остаток requestFrom, а не приёмный буфер:
//      буфера нет, чтение идёт побайтово из TWDR.
//    - Transaction в деструкторе делает STOP только если транзакция ещё
//      открыта этим объектом. Finish со sendStop=false оставляет шину
//      занятой намеренно (для repeated START следующим объектом).
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 14 байт (config 8 + состояние 2 + счётчики 8 минус выравнивание).
//    Flash: ~700 байт при полном использовании, включая recover и scan.
//    Таблиц в RAM нет. Обмен без прерываний: ISR не занимает вектор кодом.
//
//  ПРИМЕР
//    FN_CLAIM_VECTOR(Twi, FN_OWNER_I2C);
//
//    void setup() {
//        fn::I2c::begin(400000);
//        uint8_t found[8];
//        uint8_t n = fn::I2c::scan(found, 8);
//    }
//
//    // Запись регистра
//    fn::I2c::writeReg8(0x68, 0x6B, 0x00);      // MPU6050: выход из sleep
//
//    // Чтение регистров с repeated START
//    uint8_t buf[6];
//    fn::I2c::readReg(0x68, 0x3B, buf, 6);
//
//    // Транзакция вручную
//    {
//        fn::I2c::Transaction t(0x3C);
//        t.write(0x00);                        // command stream
//        t.write(0xAE);                        // display off
//    }                                          // STOP автоматически
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               core/BusContract.h, pin/Board.h, timer/Tick.h;
//               реализация — I2c.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "../core/BusContract.h"
#include "../pin/Board.h"

#include <stdint.h>

#if F_USE_I2C

#ifndef FN_I2C_TIMEOUT_MS
#define FN_I2C_TIMEOUT_MS 25
#endif

namespace fn {

class I2c {
public:
    struct Config {
        uint32_t clock;
        bool     pullups;
        uint32_t timeoutMs;

        constexpr Config(uint32_t hz = bus::kI2cStandard,
                         bool pu = true,
                         uint32_t tmo = FN_I2C_TIMEOUT_MS)
            : clock(hz), pullups(pu), timeoutMs(tmo) {}
    };

    static Result begin(const Config& cfg = Config());
    static Result begin(uint32_t clock);
    static void end();
    static bool ready();

    static Result setClock(uint32_t hz);
    static uint32_t actualClock();

    // --- контракт шины ---
    static Result write(const uint8_t* data, uint16_t len);
    static Result read(uint8_t* dst, uint16_t len, uint32_t timeoutMs);
    static Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
    static uint16_t available();
    static Result flush(uint32_t timeoutMs);
    static bool busy();

    // --- транзакции ---
    static Result start(uint8_t addr, bool read);
    static void stop();
    static Result writeByte(uint8_t b);
    static int16_t readByte(bool ack);

    static Result writeReg(uint8_t addr, uint8_t reg, const uint8_t* d, uint16_t n);
    static Result readReg(uint8_t addr, uint8_t reg, uint8_t* d, uint16_t n);
    static Result writeReg8(uint8_t addr, uint8_t reg, uint8_t v);
    static int16_t readReg8(uint8_t addr, uint8_t reg);

    // --- сервис ---
    static bool ping(uint8_t addr);
    static uint8_t scan(uint8_t* out, uint8_t maxCount);
    static Result recover();
    static Result lastError();

    static uint16_t timeouts();
    static uint16_t nackAddr();
    static uint16_t nackData();
    static uint16_t arbLost();
    static void clearStats();

private:
    I2c() = delete;
};

// RAII-транзакция: START в конструкторе, STOP в деструкторе.
class I2cTransaction {
public:
    explicit I2cTransaction(uint8_t addr, bool read = false)
        : _r(I2c::start(addr, read)), _open(ok(_r)) {}

    ~I2cTransaction() { if (_open) I2c::stop(); }

    I2cTransaction(const I2cTransaction&) = delete;
    I2cTransaction& operator=(const I2cTransaction&) = delete;

    Result result() const { return _r; }
    Result write(uint8_t b) { return I2c::writeByte(b); }
    Result write(const uint8_t* p, uint16_t n) { return I2c::write(p, n); }

    Result finish(bool sendStop = true) {
        const Result e = I2c::lastError();
        if (sendStop && _open) { I2c::stop(); _open = false; }
        return e;
    }

private:
    Result _r;
    bool _open;
};

// Совместимость с прежним именем.
using I2cScope = I2cTransaction;

}  // namespace fn

#endif  // F_USE_I2C

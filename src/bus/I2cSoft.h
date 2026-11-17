#pragma once
// ============================================================================
//  I2cSoft.h — программный I2C-мастер на любых двух выводах (bit-bang)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Вторая шина, когда аппаратный TWI занят, или выводы A4/A5 нужны под ADC.
//    Настоящий open-drain: «низкий» = выход+LOW, «высокий» = вход без подтяжки
//    (линию тянет внешний резистор). Прежняя версия звала PinRef::high(), что
//    при DDR=input лишь включало внутреннюю подтяжку ~30..50 кОм — фронт
//    получался в единицы микросекунд, на 400 кГц связи не было.
//
//  API
//    class SoftI2c                          объект, 6 байт
//      begin(sdaPin, sclPin, halfPeriodUs = 5) -> Result
//      end()
//      setHalfPeriodUs(uint8_t us)          полупериод SCL; 5 -> ~100 кГц
//      start(addr, read) -> Result
//      stop()
//      writeByte(uint8_t) -> Result
//      readByte(bool ack) -> int16_t
//      write(const uint8_t*, uint16_t) -> Result
//      read(uint8_t*, uint16_t) -> Result
//      writeReg(addr, reg, data, n) -> Result
//      readReg(addr, reg, dst, n) -> Result
//      writeReg8(addr, reg, v) -> Result
//      readReg8(addr, reg) -> int16_t
//      ping(addr) -> bool
//      lastError() -> Result
//      busy() -> bool
//      sdaPin() / sclPin() -> fn::Pin
//
//  КОДЫ ВОЗВРАТА
//    Ok / BadArg (невалидный вывод, nullptr) / AddrNack / DataNack /
//    Timeout (SCL удерживается slave дольше таймаута) / Fail (SDA залипла)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Обязательны ВНЕШНИЕ подтяжки на обеих линиях: 4.7 кОм для ~100 кГц,
//      2.2 кОм для ~400 кГц. Внутренние не годятся: модуль их не включает
//      намеренно, чтобы не мешать внешним и не портить фронт.
//    - Полупериод задаётся в микросекундах через _delay_us с КОНСТАНТНЫМ
//      аргументом внутри шаблона задержки. Прежняя версия передавала
//      переменную — avr-libc разворачивал runtime-цикл с плавающей точностью
//      и точность терялась. Здесь используется _delay_loop_1 с расчётом
//      итераций: детерминированно, без float.
//    - Реальная частота ниже расчётной из-за накладных расходов на переключение
//      DDR: при полупериоде 5 мкс получается ~70..80 кГц, а не 100.
//      Точная частота зависит от числа команд в цикле; калибруйте по осциллографу
//      или увеличивайте полупериод.
//    - Clock stretching поддержан: после отпускания SCL модуль ждёт, пока
//      линия реально поднимется, с таймаутом FN_SOFT_I2C_TIMEOUT_US.
//    - Прерывания НЕ запрещаются: любой ISR удлиняет полупериод. I2C не имеет
//      минимальной частоты, поэтому растяжение безвредно — в отличие от UART.
//      Исключение: slave с таймаутом на шину (SMBus) может отвалиться.
//    - Арбитраж не поддерживается: только один мастер на шине.
//    - sizeof(SoftI2c) == 6: два Pin (2), полупериод (1), состояние (1),
//      ошибка (1), остаток чтения (1).
//    - Один объект = одна шина. Несколько объектов на разных выводах возможны,
//      каждый занимает свои два пина.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 6 байт на объект. Flash: ~450 байт при полном использовании.
//    Векторов не занимает: обмен синхронный, ISR нет.
//
//  ПРИМЕР
//    fn::SoftI2c bus;
//    bus.begin(4, 5);                       // D4 = SDA, D5 = SCL, ~100 кГц
//    bus.writeReg8(0x3C, 0x00, 0xAE);       // SSD1306: display off
//
//    uint8_t id[2];
//    bus.readReg(0x50, 0x00, id, 2);        // EEPROM 24C02
//
//    if (!bus.ping(0x68)) { /* устройства нет */ }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h, core/Atomic.h,
//               core/BusContract.h, pin/Board.h, util/delay_basic.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"
#include "../core/Atomic.h"
#include "../core/BusContract.h"
#include "../pin/Board.h"

#include <stdint.h>
#include <util/delay_basic.h>

#if F_USE_I2C

#ifndef FN_SOFT_I2C_TIMEOUT_US
#define FN_SOFT_I2C_TIMEOUT_US 1000
#endif

namespace fn {

class SoftI2c {
public:
    SoftI2c() = default;

    Result begin(uint8_t sdaPin, uint8_t sclPin, uint8_t halfPeriodUs = 5) {
        const Pin a = boardPin(sdaPin);
        const Pin c = boardPin(sclPin);
        if (!a.valid() || !c.valid() || a == c) return Result::BadArg;
        _sda = a;
        _scl = c;
        _half = halfPeriodUs ? halfPeriodUs : 1;
        _err = Result::Ok;
        _rxLeft = 0;
        _open = false;
        // Обе линии отпущены (вход, без подтяжки — тянут внешние резисторы).
        regs::modeInput(_sda);
        regs::modeInput(_scl);
        pause();
        if (!regs::read(_sda) || !regs::read(_scl)) return fail(Result::Fail);
        return Result::Ok;
    }

    void end() {
        if (_open) stop();
        regs::modeInput(_sda);
        regs::modeInput(_scl);
        _open = false;
    }

    void setHalfPeriodUs(uint8_t us) { _half = us ? us : 1; }

    Result start(uint8_t addr, bool read) {
        if (!_sda.valid() || addr > 0x7Fu) return Result::BadArg;
        _err = Result::Ok;
        _rxLeft = 0;

        // START: SDA падает при SCL высоком. При повторном START сначала
        // отпускаем SDA и поднимаем SCL.
        releaseSda();
        pause();
        if (!releaseScl()) return fail(Result::Timeout);
        pause();
        pullSda();
        pause();
        pullScl();
        pause();
        _open = true;

        const uint8_t ab = uint8_t((addr << 1) | (read ? 1u : 0u));
        if (!sendByte(ab)) {
            stop();
            return fail(Result::AddrNack);
        }
        return Result::Ok;
    }

    void stop() {
        // STOP: SDA растёт при SCL высоком.
        pullSda();
        pause();
        if (!releaseScl()) { _err = Result::Timeout; }
        pause();
        releaseSda();
        pause();
        _open = false;
        _rxLeft = 0;
    }

    Result writeByte(uint8_t b) {
        if (!_open) return Result::NotReady;
        if (!ok(_err)) return _err;
        if (!sendByte(b)) return fail(Result::DataNack);
        return Result::Ok;
    }

    int16_t readByte(bool ack) {
        if (!_open || !ok(_err)) return -1;
        uint8_t v = 0;
        releaseSda();                       // отдать SDA slave
        for (uint8_t i = 0; i < 8; ++i) {
            pause();
            if (!releaseScl()) { fail(Result::Timeout); return -1; }
            pause();
            v = uint8_t((v << 1) | (regs::read(_sda) ? 1u : 0u));
            pullScl();
        }
        // Девятый такт: ACK от мастера (SDA low) или NACK (отпущена).
        if (ack) pullSda(); else releaseSda();
        pause();
        if (!releaseScl()) { fail(Result::Timeout); return -1; }
        pause();
        pullScl();
        releaseSda();
        if (_rxLeft) --_rxLeft;
        return int16_t(v);
    }

    Result write(const uint8_t* p, uint16_t n) {
        if (p == nullptr) return Result::BadArg;
        for (uint16_t i = 0; i < n; ++i) {
            const Result r = writeByte(p[i]);
            if (!ok(r)) return r;
        }
        return Result::Ok;
    }

    Result read(uint8_t* d, uint16_t n) {
        if (d == nullptr) return Result::BadArg;
        _rxLeft = n;
        for (uint16_t i = 0; i < n; ++i) {
            const int16_t v = readByte(i + 1u < n);
            if (v < 0) return _err;
            d[i] = uint8_t(v);
        }
        return Result::Ok;
    }

    Result writeReg(uint8_t addr, uint8_t reg, const uint8_t* d, uint16_t n) {
        Result r = start(addr, false);
        if (!ok(r)) return r;
        r = writeByte(reg);
        if (!ok(r)) { stop(); return r; }
        r = write(d, n);
        stop();
        return r;
    }

    Result readReg(uint8_t addr, uint8_t reg, uint8_t* d, uint16_t n) {
        Result r = start(addr, false);
        if (!ok(r)) return r;
        r = writeByte(reg);
        if (!ok(r)) { stop(); return r; }
        r = start(addr, true);
        if (!ok(r)) return r;
        r = read(d, n);
        stop();
        return r;
    }

    Result writeReg8(uint8_t addr, uint8_t reg, uint8_t v) {
        return writeReg(addr, reg, &v, 1);
    }

    int16_t readReg8(uint8_t addr, uint8_t reg) {
        uint8_t v = 0;
        return ok(readReg(addr, reg, &v, 1)) ? int16_t(v) : int16_t(-1);
    }

    bool ping(uint8_t addr) {
        const Result r = start(addr, false);
        stop();
        return ok(r);
    }

    Result lastError() const { return _err; }
    bool busy() const { return _open; }
    Pin sdaPin() const { return _sda; }
    Pin sclPin() const { return _scl; }

private:
    Result fail(Result r) { _err = r; return r; }

    // Пауза полупериода. _delay_loop_1: 3 такта на итерацию.
    // Итераций = half * (F_CPU / 3e6); при 16 МГц и half=5 -> 26 итераций.
    FN_INLINE void pause() const {
        const uint16_t n = uint16_t(uint32_t(_half) * (F_CPU / 3000000UL));
        if (n == 0) return;
        if (n <= 255UL) {
            _delay_loop_1(uint8_t(n));
        } else {
            // Длинные полупериоды: разбиваем на чанки по 255 итераций.
            uint16_t left = n;
            while (left > 255u) { _delay_loop_1(255); left = uint16_t(left - 255u); }
            if (left) _delay_loop_1(uint8_t(left));
        }
    }

    FN_INLINE void pullScl()    { regs::modeOutput(_scl, false); }
    FN_INLINE void pullSda()    { regs::modeOutput(_sda, false); }
    FN_INLINE void releaseSda() { regs::modeInput(_sda); }

    // Отпустить SCL и дождаться реального подъёма (clock stretching).
    bool releaseScl() {
        regs::modeInput(_scl);
        // Ждём с таймаутом: slave имеет право тянуть SCL вниз.
        uint16_t guard = FN_SOFT_I2C_TIMEOUT_US;
        while (!regs::read(_scl)) {
            if (--guard == 0) return false;
            _delay_loop_1(uint8_t(F_CPU / 3000000UL));   // ~1 мкс
        }
        return true;
    }

    // Передать байт MSB-first, вернуть true при ACK от slave.
    bool sendByte(uint8_t b) {
        for (uint8_t m = 0x80u; m; m = uint8_t(m >> 1)) {
            if (b & m) releaseSda(); else pullSda();
            pause();
            if (!releaseScl()) { fail(Result::Timeout); return false; }
            pause();
            pullScl();
        }
        releaseSda();                      // отдать SDA под ACK
        pause();
        if (!releaseScl()) { fail(Result::Timeout); return false; }
        pause();
        const bool ack = !regs::read(_sda);
        pullScl();
        pause();
        return ack;
    }

    Pin _sda{};
    Pin _scl{};
    uint8_t _half = 5;
    uint8_t _rxLeft = 0;
    Result _err = Result::Ok;
    bool _open = false;
};

}  // namespace fn

#endif  // F_USE_I2C

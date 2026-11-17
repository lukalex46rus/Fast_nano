#pragma once
// ============================================================================
//  SpiSoft.h — программный SPI-мастер на любых выводах, все четыре режима
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Вторая шина, когда аппаратный SPI занят, нужны другие выводы или частота
//    ниже 125 кГц (аппаратный минимум F_CPU/128). Поддержаны все режимы
//    CPOL/CPHA и оба порядка бит. Размер объекта 6 байт.
//
//  API
//    class SoftSpi                          объект, 6 байт
//      begin(mosi, miso, clk, Mode = M0, BitOrder = MsbFirst) -> Result
//      beginTx(mosi, clk, Mode, BitOrder) -> Result      только передача
//      beginRx(miso, clk, Mode, BitOrder) -> Result      только приём
//      end()                                освободить выводы
//      setHalfPeriodUs(uint8_t us)          0 = без задержки, максимум скорости
//      halfPeriodUs() -> uint8_t
//      nominalClockHz() -> uint32_t         1e6/(2*half), ВЕРХНЯЯ граница
//      setMode(Mode) -> Result              NotReady, если не запущен
//      setOrder(BitOrder) -> Result         NotReady, если не запущен
//      transferByte(uint8_t tx) -> int16_t  принятый байт, -1 при ошибке
//      write(const uint8_t*, uint16_t) -> Result
//      read(uint8_t*, uint16_t, uint8_t fill = 0xFF) -> Result
//      transfer(const uint8_t* tx, uint8_t* rx, uint16_t) -> Result
//      transferInPlace(uint8_t*, uint16_t) -> Result
//      transfer16(uint16_t v, bool highByteFirst = true) -> int32_t
//      ready() -> bool
//      mosiPin() / misoPin() / clkPin() -> fn::Pin
//
//  КОДЫ ВОЗВРАТА
//    Ok           операция выполнена
//    BadArg       невалидный вывод, совпадающие выводы, nullptr при len > 0
//    NotReady     begin() не вызывался (или был end())
//    Unsupported  read() на TX-only, write() на RX-only,
//                 transfer()/transferInPlace() без полного дуплекса
//
//  РАСКЛАДКА И РАЗМЕР
//    _mosi, _miso, _clk : fn::Pin по 1 байту        (3)
//    _half              : полупериод в мкс          (1)
//    _cfg               : биты 1:0 = (CPOL<<1)|CPHA,
//                         бит 2 = LSB-first         (1)
//    _flags             : бит 0 hasTx, 1 hasRx,
//                         2 ready                  (1)
//    Итого 6 байт, зафиксировано static_assert. Отдельного поля lastError нет:
//    все коды приходят в возвратах методов.
//
//  ТАЙМИНГ
//    Полупериод считается через _delay_loop_2 (4 такта на итерацию):
//    итераций = half * (F_CPU/1e6) / 4. При 16 и 8 МГц это ровно half
//    микросекунд. Прежняя версия использовала _delay_loop_1 и F_CPU/3e6,
//    что на 8 МГц давало 6 тактов вместо 8 — задержка на 25% короче заявленной.
//    Максимум итераций 1020 при half=255 и 16 МГц: влезает в uint16 аргумент
//    _delay_loop_2, разбиение на чанки не нужно.
//
//  ТОЧКИ ВЫБОРКИ (по спецификации SPI)
//    CPHA=0: MOSI выставляется -> пауза -> ВЕДУЩИЙ фронт -> выборка MISO
//            -> пауза -> замыкающий фронт (slave сдвигает следующий бит).
//    CPHA=1: ВЕДУЩИЙ фронт (slave сдвигает) -> MOSI выставляется -> пауза
//            -> ЗАМЫКАЮЩИЙ фронт -> выборка MISO -> пауза.
//    Два фронта и две паузы на бит в обоих случаях: скважность 50%,
//    по завершении байта CLK возвращается в idle-уровень.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Реальная частота НИЖЕ nominalClockHz(): к каждому полупериоду
//      добавляется время на запись MOSI, чтение MISO и цикл (~1..3 мкс).
//      При half=5 получается ~70..80 кГц, а не 100. nominalClockHz() —
//      верхняя граница, не измерение. Калибруйте по осциллографу.
//    - half=0 отключает задержку: остаются только накладные расходы,
//      частота ~150..250 кГц при 16 МГц и несимметричная скважность.
//    - Прерывания НЕ запрещаются: любой ISR удлиняет полупериод. SPI —
//      шина синхронная, slave тактируется от SCK, поэтому растяжение
//      безвредно. Исключение:外设 с таймаутом на транзакцию.
//    - Повторный begin() сначала вызывает end(): прежние выводы
//      освобождаются. Иначе брошенный CLK продолжал бы тактировать шину.
//    - end() переводит все три вывода в вход без подтяжки и обнуляет
//      дескрипторы: mosiPin()/misoPin()/clkPin() вернут fn::NoPin.
//    - CS не входит в модуль: управляйте им отдельно через BoardPin или
//      fn::regs. Один CS может обслуживать несколько шин.
//    - transfer16(v, highByteFirst) задаёт порядок БАЙТ. Порядок БИТОВ
//      внутри байта задаётся setOrder()/begin(). Это разные настройки.
//    - В режиме TX-only transferByte() возвращает 0 (принимать нечего),
//      а read() вернёт Unsupported. В режиме RX-only наоборот.
//    - Указатели на регистры и маски снимаются один раз в начале
//      transferByte: в цикле только обращения к регистрам. Вызов
//      regs::write/read/toggle на каждый бит удваивал бы время витка.
//    - Только мастер. Slave-режим требует внешнего тактирования и ISR.
//    - Векторов и таймеров не занимает. Конфликт выводов с PWM/PCINT/USART
//      того же пина — ответственность прикладного кода.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 6 байт на объект. Flash: ~400 байт при полном использовании.
//    Обмен байта: 16 фронтов, ~2 паузы по half мкс плюс ~40 тактов накладных.
//
//  ПРИМЕР
//    fn::SoftSpi bus;
//    bus.begin(4, 5, 6, fn::bus::Mode::M0);    // D4=MOSI, D5=MISO, D6=SCK
//    bus.setHalfPeriodUs(20);                  // ~25 кГц для SD-init
//
//    fn::BoardPin<7>::output();                // CS
//    fn::BoardPin<7>::low();
//    bus.transferByte(0x9F);
//    uint8_t id[3];
//    bus.read(id, 3);
//    fn::BoardPin<7>::high();
//
//    // Смена режима для другого slave на той же шине
//    bus.setMode(fn::bus::Mode::M3);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h,
//               core/BusContract.h, pin/Board.h, util/delay_basic.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"
#include "../core/BusContract.h"
#include "../pin/Board.h"

#include <stdint.h>
#include <util/delay_basic.h>

#if F_USE_SPI

namespace fn {

class SoftSpi {
public:
    using Mode     = bus::Mode;
    using BitOrder = bus::BitOrder;

    SoftSpi() = default;

    // ------------------------------ запуск ---------------------------------
    Result begin(uint8_t mosi, uint8_t miso, uint8_t clk,
                 Mode m = Mode::M0, BitOrder o = BitOrder::MsbFirst) {
        const Pin a = boardPin(mosi), b = boardPin(miso), c = boardPin(clk);
        if (!a.valid() || !b.valid() || !c.valid()) return Result::BadArg;
        if (a == b || a == c || b == c) return Result::BadArg;
        if (ready()) end();
        _mosi = a; _miso = b; _clk = c;
        _flags = uint8_t(FlagTx | FlagRx);
        return setup(m, o);
    }

    Result beginTx(uint8_t mosi, uint8_t clk,
                   Mode m = Mode::M0, BitOrder o = BitOrder::MsbFirst) {
        const Pin a = boardPin(mosi), c = boardPin(clk);
        if (!a.valid() || !c.valid() || a == c) return Result::BadArg;
        if (ready()) end();
        _mosi = a; _miso = Pin(); _clk = c;
        _flags = FlagTx;
        return setup(m, o);
    }

    Result beginRx(uint8_t miso, uint8_t clk,
                   Mode m = Mode::M0, BitOrder o = BitOrder::MsbFirst) {
        const Pin b = boardPin(miso), c = boardPin(clk);
        if (!b.valid() || !c.valid() || b == c) return Result::BadArg;
        if (ready()) end();
        _mosi = Pin(); _miso = b; _clk = c;
        _flags = FlagRx;
        return setup(m, o);
    }

    void end() {
        if (_mosi.valid()) regs::modeInput(_mosi);
        if (_miso.valid()) regs::modeInput(_miso);
        if (_clk.valid())  regs::modeInput(_clk);
        _mosi = Pin();
        _miso = Pin();
        _clk = Pin();
        _flags = 0;
    }

    // ------------------------------ настройка ------------------------------
    void setHalfPeriodUs(uint8_t us) { _half = us; }
    uint8_t halfPeriodUs() const { return _half; }

    // Верхняя граница частоты: накладные расходы делают реальную ниже.
    uint32_t nominalClockHz() const {
        return _half ? (1000000UL / (2UL * _half)) : 0;
    }

    Result setMode(Mode m) {
        if (!ready()) return Result::NotReady;
        _cfg = uint8_t((_cfg & 0x04u) | (uint8_t(m) & 0x03u));
        regs::write(_clk, idleLevel());
        return Result::Ok;
    }

    Result setOrder(BitOrder o) {
        if (!ready()) return Result::NotReady;
        _cfg = uint8_t((_cfg & 0x03u) | (o == BitOrder::LsbFirst ? 0x04u : 0u));
        return Result::Ok;
    }

    // ------------------------------ обмен ----------------------------------
    int16_t transferByte(uint8_t tx) {
        if (!ready()) return -1;

        // Адреса и маски снимаются один раз: в цикле только регистры.
        volatile uint8_t* clkTgl = regs::sfr(_clk.port(), Reg::Pin);
        if (clkTgl == nullptr) return -1;
        const uint8_t mClk = _clk.mask();

        const bool txEn = hasTx();
        const bool rxEn = hasRx();

        volatile uint8_t* out = nullptr;
        volatile uint8_t* in = nullptr;
        uint8_t mOut = 0, mIn = 0;

        if (txEn) {
            out = regs::sfr(_mosi.port(), Reg::Port);
            if (out == nullptr) return -1;
            mOut = _mosi.mask();
        }
        if (rxEn) {
            in = regs::sfr(_miso.port(), Reg::Pin);
            if (in == nullptr) return -1;
            mIn = _miso.mask();
        }

        const bool lsb = lsbFirst();
        const bool ph1 = cpha();
        uint8_t rx = 0;

        for (uint8_t i = 0; i < 8; ++i) {
            const uint8_t bitMask = lsb ? uint8_t(1u << i) : uint8_t(0x80u >> i);
            const bool bit = (tx & bitMask) != 0;

            if (!ph1) {
                // CPHA=0: выборка на ведущем фронте, сдвиг на замыкающем.
                if (txEn) *out = bit ? uint8_t(*out | mOut)
                                     : uint8_t(*out & uint8_t(~mOut));
                pause();
                *clkTgl = mClk;                       // ведущий фронт
                if (rxEn) rx = shiftIn(rx, (*in & mIn) != 0, lsb);
                pause();
                *clkTgl = mClk;                       // замыкающий фронт
            } else {
                // CPHA=1: сдвиг на ведущем фронте, выборка на замыкающем.
                *clkTgl = mClk;                       // ведущий фронт
                if (txEn) *out = bit ? uint8_t(*out | mOut)
                                     : uint8_t(*out & uint8_t(~mOut));
                pause();
                *clkTgl = mClk;                       // замыкающий фронт
                if (rxEn) rx = shiftIn(rx, (*in & mIn) != 0, lsb);
                pause();
            }
        }
        return int16_t(rx);
    }

    Result write(const uint8_t* p, uint16_t n) {
        if (!ready()) return Result::NotReady;
        if (!hasTx()) return Result::Unsupported;
        if (p == nullptr && n) return Result::BadArg;
        for (uint16_t i = 0; i < n; ++i) {
            if (transferByte(p[i]) < 0) return Result::NotReady;
        }
        return Result::Ok;
    }

    Result read(uint8_t* d, uint16_t n, uint8_t fill = 0xFF) {
        if (!ready()) return Result::NotReady;
        if (!hasRx()) return Result::Unsupported;
        if (d == nullptr && n) return Result::BadArg;
        for (uint16_t i = 0; i < n; ++i) {
            const int16_t v = transferByte(fill);
            if (v < 0) return Result::NotReady;
            d[i] = uint8_t(v);
        }
        return Result::Ok;
    }

    Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t n) {
        if (!ready()) return Result::NotReady;
        if (!hasTx() || !hasRx()) return Result::Unsupported;
        if ((tx == nullptr || rx == nullptr) && n) return Result::BadArg;
        for (uint16_t i = 0; i < n; ++i) {
            const int16_t v = transferByte(tx[i]);
            if (v < 0) return Result::NotReady;
            rx[i] = uint8_t(v);
        }
        return Result::Ok;
    }

    Result transferInPlace(uint8_t* d, uint16_t n) {
        if (!ready()) return Result::NotReady;
        if (!hasTx() || !hasRx()) return Result::Unsupported;
        if (d == nullptr && n) return Result::BadArg;
        for (uint16_t i = 0; i < n; ++i) {
            const int16_t v = transferByte(d[i]);
            if (v < 0) return Result::NotReady;
            d[i] = uint8_t(v);
        }
        return Result::Ok;
    }

    // highByteFirst — порядок БАЙТ, не битов. Порядок битов задаёт setOrder.
    int32_t transfer16(uint16_t v, bool highByteFirst = true) {
        const uint8_t hi = uint8_t(v >> 8);
        const uint8_t lo = uint8_t(v);
        int16_t a, b;
        if (highByteFirst) {
            a = transferByte(hi);
            if (a < 0) return -1;
            b = transferByte(lo);
        } else {
            a = transferByte(lo);
            if (a < 0) return -1;
            b = transferByte(hi);
        }
        if (b < 0) return -1;
        return highByteFirst ? int32_t((uint16_t(a) << 8) | uint16_t(b))
                             : int32_t((uint16_t(b) << 8) | uint16_t(a));
    }

    // ------------------------------ состояние ------------------------------
    bool ready() const { return (_flags & FlagReady) != 0; }
    Pin mosiPin() const { return _mosi; }
    Pin misoPin() const { return _miso; }
    Pin clkPin()  const { return _clk; }

private:
    static constexpr uint8_t FlagTx    = 0x01;
    static constexpr uint8_t FlagRx    = 0x02;
    static constexpr uint8_t FlagReady = 0x04;

    bool hasTx() const { return (_flags & FlagTx) != 0; }
    bool hasRx() const { return (_flags & FlagRx) != 0; }
    bool lsbFirst() const { return (_cfg & 0x04u) != 0; }
    bool cpha() const { return (_cfg & 0x01u) != 0; }
    bool idleLevel() const { return (_cfg & 0x02u) != 0; }

    Result setup(Mode m, BitOrder o) {
        _cfg = uint8_t((uint8_t(m) & 0x03u) | (o == BitOrder::LsbFirst ? 0x04u : 0u));
        if (hasTx()) regs::modeOutput(_mosi, false);
        if (hasRx()) regs::modeInput(_miso);
        regs::modeOutput(_clk, idleLevel());   // PORT, затем DDR — без выброса
        _flags = uint8_t(_flags | FlagReady);
        return Result::Ok;
    }

    FN_INLINE static uint8_t shiftIn(uint8_t acc, bool bit, bool lsb) {
        if (lsb) return uint8_t((acc >> 1) | (bit ? 0x80u : 0u));
        return uint8_t((acc << 1) | (bit ? 1u : 0u));
    }

    // Полупериод: _delay_loop_2 = 4 такта на итерацию.
    // half * (F_CPU/1e6) / 4 даёт ровно half мкс при 16 и 8 МГц.
    // Максимум 1020 итераций (half=255, 16 МГц) — влезает в uint16.
    FN_INLINE void pause() const {
        if (_half == 0) return;
        const uint32_t cycles = uint32_t(_half) * (F_CPU / 1000000UL);
        uint32_t n = cycles / 4UL;
        if (n == 0) n = 1;                     // _delay_loop_2(0) = 65536 итераций
        _delay_loop_2(uint16_t(n));
    }

    Pin _mosi{};
    Pin _miso{};
    Pin _clk{};
    uint8_t _half = 0;
    uint8_t _cfg = 0;      // биты 1:0 = (CPOL<<1)|CPHA, бит 2 = LSB-first
    uint8_t _flags = 0;    // hasTx / hasRx / ready
};

static_assert(sizeof(SoftSpi) == 6,
              "fn::SoftSpi: ожидалось 6 байт (выравнивание на AVR = 1)");

}  // namespace fn

#endif  // F_USE_SPI
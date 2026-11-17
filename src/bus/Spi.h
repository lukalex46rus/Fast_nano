#pragma once
// ============================================================================
//  Spi.h — аппаратный SPI-мастер: full-duplex, выбор делителя, CS-обёртки
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственный владелец SPI-модуля. Режим мастера, все четыре режима
//    CPOL/CPHA, MSB/LSB-first. Основная операция — transfer (full-duplex):
//    каждый байт одновременно передаётся и принимается. Соответствует
//    контракту core/BusContract.h.
//
//  ВЛАДЕНИЕ
//    Вектор SPI_STC заявляется модулем (FN_CLAIM_VECTOR(SpiStc, FN_OWNER_SPI)).
//    Прерывание не используется: обмен по флагу SPIF с таймаутом.
//    Выводы: D11 (PB3, MOSI), D12 (PB2? нет — PB4, MISO), D13 (PB5, SCK),
//            D10 (PB2, SS). Точная карта: MOSI=PB3, MISO=PB4, SCK=PB5, SS=PB2.
//    Конфликты: D11 против OC2A (PWM Timer2), D10 против OC1B (PWM Timer1),
//               D13 против светодиода на плате.
//
//  API (namespace fn, класс Spi — только статические методы)
//    using Mode     = bus::Mode;        M0/M1/M2/M3 = (CPOL<<1)|CPHA
//    using BitOrder = bus::BitOrder;    MsbFirst / LsbFirst
//
//    struct Config
//      uint32_t clock     = 4000000    желаемая частота SCK, Гц
//      BitOrder order     = MsbFirst
//      Mode     mode      = M0
//      uint32_t timeoutMs = 10         таймаут одной операции обмена
//      bool     ownSs     = true       занимать D10 как выход (требование мастера)
//
//    begin(const Config& = Config()) -> Result
//    begin(uint32_t clock)           -> Result
//    end()
//    ready() -> bool
//
//    setClock(uint32_t hz) -> Result   OutOfRange, если ниже минимума
//    actualClock() -> uint32_t         фактическая частота SCK
//    setMode(Mode) / setOrder(BitOrder)
//
//    --- контракт шины ---
//    write(const uint8_t* data, uint16_t len) -> Result
//    read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) -> Result
//    transfer(const uint8_t* tx, uint8_t* rx, uint16_t len) -> Result
//    available() -> uint16_t           всегда 0 (буфера нет)
//    flush(uint32_t timeoutMs) -> Result  Ok сразу
//    busy() -> bool                    идёт обмен
//
//    --- побайтовый и блочный обмен ---
//    transferByte(uint8_t tx) -> int16_t   принятый байт, -1 при ошибке
//    transferInPlace(uint8_t* data, uint16_t len) -> Result
//    transfer16(uint16_t v, bool msbFirst = true) -> int32_t
//    lastError() -> Result
//    timeouts() -> uint16_t
//    clearStats()
//
//    template <uint8_t CsPin, bool ActiveLow = true> class Device
//      begin() / select() / deselect()
//      transferByte(tx) -> int16_t
//      write(p, n) / read(p, n, tmo) / transfer(tx, rx, n) -> Result
//      class Scoped                      RAII: select в ctor, deselect в dtor
//
//  КОДЫ ВОЗВРАТА
//    Ok          операция выполнена
//    BadArg      nullptr при ненулевой длине
//    NotReady    begin() не вызывался
//    OutOfRange  запрошенная частота ниже минимальной (F_CPU/128)
//    Timeout     SPIF не выставлен за timeoutMs
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Делитель подбирается СВЕРХУ вниз: фактическая частота всегда <= запроса.
//      Прежняя версия округляла вверх и для запроса 30 кГц давала 125 кГц
//      (в 4.17 раза быстрее) — отказ связи с медленными外设 без диагностики.
//      Проверено численно: 100 кГц -> 125 кГц, 62.5 кГц -> 125 кГц.
//    - Минимум аппаратной частоты: F_CPU/128 = 125 кГц при 16 МГц. Запрос ниже
//      возвращает OutOfRange и НЕ меняет настройку. Нужна медленная шина —
//      используйте SoftSpi (программный, любая частота).
//    - Максимум: F_CPU/2 = 8 МГц. Запрос выше клампится к 8 МГц (Result::Ok).
//    - Доступные делители: 2, 4, 8, 16, 32, 64, 128. Промежуточных нет:
//      запрос 3 МГц даст 2 МГц, а не 3.
//    - SS (D10) обязан быть выходом в режиме мастера: низкий уровень на нём
//      аппаратно сбрасывает MSTR и переводит модуль в slave. При ownSs=false
//      вызывающий гарантирует, что D10 настроен как выход и удерживается HIGH.
//    - end() переводит выводы в вход без подтяжки. Прежнее состояние (например
//      PWM на D10/D11) НЕ восстанавливается: сохраните его сами через
//      regs::save()/restore() или Pwm::begin() заново.
//    - Все ожидания по fn::expired(), не по счётчику итераций. Таймаут
//      применяется к каждому байту, не ко всей посылке.
//    - Флаг WCOL (запись во время передачи) не возникает: каждая запись
//      предваряется ожиданием SPIF. Отдельная проверка не нужна.
//    - Буфера нет: available() всегда 0, flush() немедленно Ok. Приём
//      синхронный, байт читается сразу после передачи.
//    - Slave-режим не реализован: типовая задача — мастер.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 12 байт (config 8 + состояние 2 + счётчик 2).
//    Flash: ~450 байт при полном использовании. Таблиц в RAM нет.
//    Обмен байта: ~18 тактов накладных + 8 периодов SCK.
//
//  ПРИМЕР
//    FN_CLAIM_VECTOR(SpiStc, FN_OWNER_SPI);
//
//    void setup() {
//        fn::Spi::begin(4000000);                 // 4 МГц, mode 0, MSB
//        fn::Spi::Device<10> sd;                  // CS на D10, active low
//        sd.begin();
//        uint8_t cmd[6] = {0x40,0,0,0,0,0x95};
//        {
//            fn::Spi::Device<10>::Scoped cs;      // select .. deselect
//            sd.write(cmd, 6);
//        }
//    }
//
//    // Full-duplex обмен
//    uint8_t tx[4] = {0x9F, 0, 0, 0}, rx[4];
//    fn::Spi::transfer(tx, rx, 4);                // JEDEC ID flash-памяти
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               core/BusContract.h, pin/Board.h, pin/Pin.h, timer/Tick.h;
//               реализация — Spi.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "../core/BusContract.h"
#include "../pin/Board.h"
#include "../pin/Pin.h"

#include <stdint.h>

#if F_USE_SPI

#ifndef FN_SPI_TIMEOUT_MS
#define FN_SPI_TIMEOUT_MS 10
#endif

namespace fn {

class Spi {
public:
    using Mode     = bus::Mode;
    using BitOrder = bus::BitOrder;

    struct Config {
        uint32_t clock;
        BitOrder order;
        Mode     mode;
        uint32_t timeoutMs;
        bool     ownSs;

        constexpr Config(uint32_t hz = bus::kSpiDefault,
                         BitOrder o = BitOrder::MsbFirst,
                         Mode m = Mode::M0,
                         uint32_t tmo = FN_SPI_TIMEOUT_MS,
                         bool ss = true)
            : clock(hz), order(o), mode(m), timeoutMs(tmo), ownSs(ss) {}
    };

    static Result begin(const Config& cfg = Config());
    static Result begin(uint32_t clock);
    static void end();
    static bool ready();

    static Result setClock(uint32_t hz);
    static uint32_t actualClock();
    static void setMode(Mode m);
    static void setOrder(BitOrder o);

    // --- контракт шины ---
    static Result write(const uint8_t* data, uint16_t len);
    static Result read(uint8_t* dst, uint16_t len, uint32_t timeoutMs);
    static Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
    static uint16_t available();
    static Result flush(uint32_t timeoutMs);
    static bool busy();

    // --- обмен ---
    static int16_t transferByte(uint8_t tx);
    static Result transferInPlace(uint8_t* data, uint16_t len);
    static int32_t transfer16(uint16_t v, bool msbFirst = true);

    static Result lastError();
    static uint16_t timeouts();
    static void clearStats();

private:
    Spi() = delete;
};

// Привязка CS к константному выводу. Все операции — со select/deselect.
template <uint8_t CsPin, bool ActiveLow = true>
class SpiDevice {
    static_assert(pinValid(CsPin), "SpiDevice: номер вывода вне 0..19");
    using Cs = BoardPin<CsPin>;

public:
    static void begin() { Cs::output(); deselect(); }

    FN_INLINE static void select() {
        if (ActiveLow) Cs::low(); else Cs::high();
    }
    FN_INLINE static void deselect() {
        if (ActiveLow) Cs::high(); else Cs::low();
    }

    static int16_t transferByte(uint8_t tx) {
        select();
        const int16_t r = Spi::transferByte(tx);
        deselect();
        return r;
    }

    static Result write(const uint8_t* p, uint16_t n) {
        select();
        const Result r = Spi::write(p, n);
        deselect();
        return r;
    }

    static Result read(uint8_t* p, uint16_t n, uint32_t tmo = FN_SPI_TIMEOUT_MS) {
        select();
        const Result r = Spi::read(p, n, tmo);
        deselect();
        return r;
    }

    static Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t n) {
        select();
        const Result r = Spi::transfer(tx, rx, n);
        deselect();
        return r;
    }

    // RAII: держит CS активным на всю область видимости.
    class Scoped {
    public:
        Scoped() { select(); }
        ~Scoped() { deselect(); }
        Scoped(const Scoped&) = delete;
        Scoped& operator=(const Scoped&) = delete;
    };
};

}  // namespace fn

#endif  // F_USE_SPI

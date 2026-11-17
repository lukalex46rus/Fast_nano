#pragma once
// ============================================================================
//  Uart.h — аппарататный USART0: приём в кольцевой буфер, передача с таймаутом
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственный владелец USART0. Приём через ISR в кольцевой буфер (SPSC),
//    передача блокирующая с таймаутом. Print/Stream не тянет: форматирование
//    делает прикладной код. Соответствует контракту core/BusContract.h.
//
//  ВЛАДЕНИЕ
//    Векторы USART_RX и USART_UDRE заявляются этим модулем (FN_CLAIM_VECTOR).
//    Выводы D0 (PD0, RX) и D1 (PD1, TX) заняты: как GPIO использовать нельзя.
//    Конфликт с SoftwareSerial / Serial ядра Arduino: одновременное
//    использование невозможно, ядро определяет те же ISR.
//
//  API (namespace fn, класс Uart — только статические методы)
//    struct Config
//      uint32_t baud        = 9600
//      Parity   parity      = None
//      StopBits stop        = One
//      bool     doubleSpeed = true      U2X: вдвое меньше ошибка на высоких baud
//      bool     rxEnable    = true
//      bool     txEnable    = true
//
//    begin(const Config& = Config()) -> Result
//    begin(uint32_t baud)            -> Result   сокращение для 8N1
//    end()
//    ready() -> bool
//
//    write(const uint8_t* data, uint16_t len) -> Result
//    writeByte(uint8_t) -> Result
//    read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) -> Result
//    transfer(const uint8_t*, uint8_t*, uint16_t) -> Result  Unsupported
//    available() -> uint16_t                  байт в приёмном буфере
//    flush(uint32_t timeoutMs) -> Result       дождаться опустошения TX
//    busy() -> bool                            идёт передача
//
//    readByte() -> int16_t                     -1 если пусто (без ожидания)
//    peekByte() -> int16_t                     -1 если пусто, не извлекает
//    clearRx()                                 сбросить приёмный буфер
//
//    Статистика и диагностика
//    dropped() -> uint16_t                     потеряно из-за переполнения буфера
//    overruns() -> uint16_t                    аппаратных DOR (байт не прочитан)
//    frameErrors() -> uint16_t                 FE (несовпадение стоп-бита)
//    parityErrors() -> uint16_t                UPE
//    clearStats()
//    actualBaud() -> uint32_t                  фактическая скорость после деления
//    baudErrorPermille() -> int16_t            отклонение от заданной, ‰
//
//    Прямой доступ к регистрам (для особых случаев)
//    enableRxIrq() / disableRxIrq()
//    enableTxIrq() / disableTxIrq()            UDRE, для своей TX-машины
//
//  КОНФИГУРАЦИЯ
//    FN_UART_RX_SIZE   размер приёмного буфера, степень двойки (по умолч. 64)
//    FN_UART_TX_TIMEOUT_MS  таймаут ожидания UDRE по умолчанию (по умолч. 100)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Приём ВСЕГДА через ISR: байт читается из UDR0 в обработчике, иначе
//      аппаратный буфер из двух байт переполняется за 2 мс на 9600.
//      Прежняя версия давала enableRxIrq() без обработчика — уход в
//      __bad_interrupt и сброс.
//    - Размер буфера: 64 байта = 66 мс запаса на 9600, 5.5 мс на 115200.
//      При тяжёлом main берите 128. Индексы 8-битные при N <= 128, обмен
//      ISR/main без блокировки (SPSC).
//    - Скорость выше 115200 при 16 МГц имеет ошибку > 2%: 230400 даёт +2.1%
//      в U2X и -3.5% без него. Работоспособность зависит от партнёра.
//      begin() возвращает Result::OutOfRange при ошибке > 3% (порог
//      FN_UART_MAX_ERR_PERMILLE), но настройку всё равно применяет —
//      решение остаётся за вызывающим.
//    - write() блокирует до готовности передатчика с таймаутом. Без таймаута
//      (прежняя версия) при неинициализированном UART был вечный цикл.
//    - flush(0) проверяет один раз и возвращает Timeout, если TX не завершён.
//      Бесконечного ожидания нет намеренно.
//    - transfer() возвращает Unsupported: USART full-duplex, но обмен
//      «послал-получил» здесь не определён. Для протоколов используйте
//      write() + read().
//    - Парсит пакетный протокол прикладной код: fn::PacketParser поверх
//      read()/available(). Модуль кадров не знает.
//    - Коллбэк из ISR не предусмотрен: буфер и есть точка обмена. Нужен
//      обработчик по факту приёма — опрашивайте available() в задаче.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: FN_UART_RX_SIZE + 2 (индексы) + 2 (dropped) + 8 (счётчики ошибок)
//          + 4 (config) ≈ 80 байт при буфере 64.
//    Flash: ~600 байт вместе с ISR и расчётом UBRR. Таблиц скоростей нет.
//    ISR приёма: ~30 тактов (чтение UDR0, флаги, push).
//
//  ПРИМЕР
//    #include "bus/Uart.h"
//    FN_CLAIM_VECTOR(UsartRx, FN_OWNER_UART);
//
//    void setup() {
//        fn::Uart::begin(115200);
//        fn::bus::writeLine<fn::Uart>(FN_PSTR("boot ok"));
//    }
//    void loop() {
//        uint8_t buf[16];
//        if (fn::Uart::available() >= 4) {
//            fn::Uart::read(buf, 4, 10);
//        }
//        if (fn::Uart::dropped()) fn::Uart::clearStats();
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               core/BusContract.h, bus/Buffer.h; реализация — Uart.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"
#include "../core/BusContract.h"
#include "Buffer.h"

#include <stdint.h>

#if F_USE_UART

#ifndef FN_UART_TX_TIMEOUT_MS
#define FN_UART_TX_TIMEOUT_MS 100
#endif

#ifndef FN_UART_MAX_ERR_PERMILLE
#define FN_UART_MAX_ERR_PERMILLE 30
#endif

namespace fn {

class Uart {
public:
    using Parity   = bus::Parity;
    using StopBits = bus::StopBits;

    struct Config {
        uint32_t baud;
        Parity   parity;
        StopBits stop;
        bool     doubleSpeed;
        bool     rxEnable;
        bool     txEnable;

        constexpr Config(uint32_t b = bus::kBaudDefault,
                         Parity p = Parity::None,
                         StopBits s = StopBits::One,
                         bool u2x = true,
                         bool rx = true,
                         bool tx = true)
            : baud(b), parity(p), stop(s), doubleSpeed(u2x), rxEnable(rx), txEnable(tx) {}
    };

    static Result begin(const Config& cfg = Config());
    static Result begin(uint32_t baud);
    static void end();
    static bool ready();

    // --- контракт шины ---
    static Result write(const uint8_t* data, uint16_t len);
    static Result writeByte(uint8_t b);
    static Result read(uint8_t* dst, uint16_t len, uint32_t timeoutMs);
    static Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);
    static uint16_t available();
    static Result flush(uint32_t timeoutMs);
    static bool busy();

    // --- побайтовый доступ ---
    static int16_t readByte();
    static int16_t peekByte();
    static void clearRx();

    // --- диагностика ---
    static uint16_t dropped();
    static uint16_t overruns();
    static uint16_t frameErrors();
    static uint16_t parityErrors();
    static void clearStats();
    static uint32_t actualBaud();
    static int16_t baudErrorPermille();

    // --- прямой доступ ---
    static void enableRxIrq();
    static void disableRxIrq();
    static void enableTxIrq();
    static void disableTxIrq();

private:
    Uart() = delete;
};

}  // namespace fn

#endif  // F_USE_UART

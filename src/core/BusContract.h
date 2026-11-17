#pragma once
// ============================================================================
//  BusContract.h — единый контракт шин и общие хелперы передачи
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Фиксирует форму API для Uart / Spi / I2c (и soft-вариантов), чтобы три
//    шины не получили три несовместимых интерфейса. Виртуальных функций нет:
//    vtable на AVR уезжает в RAM, а полиморфизм здесь нужен статический.
//    Общие операции (запись байта, вывод из PROGMEM, ожидание с таймаутом)
//    живут здесь один раз вместо копии в каждой шине.
//
//  КОНТРАКТ ШИНЫ (что обязан предоставить модуль)
//    using Config = ...                     своя структура настроек
//    static Result begin(const Config&)     инициализация; Ok / BadArg / Busy
//    static void   end()                    деинициализация, освобождение ресурса
//    static bool   ready()                  шина инициализирована
//
//    static Result write(const uint8_t* data, uint16_t len)
//    static Result read(uint8_t* dst, uint16_t len, uint32_t timeoutMs)
//    static Result transfer(const uint8_t* tx, uint8_t* rx, uint16_t len)
//    static uint16_t available()            сколько байт доступно для чтения
//    static Result flush(uint32_t timeoutMs) дождаться опустошения TX
//    static bool   busy()                   идёт передача
//
//    Для шин без приёма (SPI master write-only) read возвращает Unsupported.
//    Для шин без буфера (SPI, I2c) available() возвращает 0, flush — Ok сразу.
//
//  API (namespace fn::bus)
//    enum class Parity : uint8_t { None, Even, Odd }
//    enum class StopBits : uint8_t { One, Two }
//    enum class BitOrder : uint8_t { MsbFirst, LsbFirst }
//    enum class Mode : uint8_t { M0, M1, M2, M3 }        режимы SPI (CPOL/CPHA)
//
//    constexpr uint32_t kBaudDefault = 9600
//    constexpr uint32_t kI2cStandard = 100000
//    constexpr uint32_t kI2cFast     = 400000
//    constexpr uint32_t kSpiDefault  = 4000000
//
//    waitUntil(pred, timeoutMs) -> Result   Ok / Timeout; pred() -> bool
//    waitUntilUs(pred, timeoutUs) -> Result то же с мкс-разрешением
//
//    template <typename Bus> writeByte(uint8_t) -> Result
//    template <typename Bus> writeP(PGM_P) -> Result
//    template <typename Bus> writeP(PGM_P, uint16_t len) -> Result
//    template <typename Bus> writeStr(const char*) -> Result
//    template <typename Bus> writeLine(PGM_P) -> Result   строка + CRLF
//    template <typename Bus> transferFill(uint8_t* rx, uint16_t, uint8_t fill)
//      Bus — сама шина (fn::Uart, fn::Spi, fn::I2c), не отдельный тег.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Все таймауты в миллисекундах через fn::expired(), а не сравнением
//      millis() > deadline: последнее ломается на переполнении через 49.7 суток.
//    - waitUntil крутит цикл без yield: вызывающий отвечает за то, что pred
//      когда-нибудь станет истинным либо таймаут истечёт. Для длинных ожиданий
//      (> 10 мс) кормите WDT внутри pred.
//    - timeoutMs == 0 означает «проверить один раз и вернуться»: удобно для
//      неблокирующего опроса. Бесконечного ожидания нет намеренно.
//    - writeP читает строку из PROGMEM побайтово и передаёт по одному байту.
//      Для шин с блочной передачей выгоднее скопировать в RAM-буфер и вызвать
//      write(): экономия на вызовах, но расход стека. Выбор за шиной.
//    - Хелперы принимают параметром шаблона саму шину: все шины статические,
//      отдельный тег-тип не нужен. Синтаксис: fn::bus::writeP<fn::Uart>(...).
//    - Контракт не навязывает наличие буфера: UART его имеет, SPI/I2c — нет.
//      available()/flush() у последних вырождены, но присутствуют для
//      единообразия вызывающего кода.
//    - Коды ошибок — только fn::Result. Свой enum с Ok/Timeout/Busy запрещён:
//      дублирование словаря ломает единообразие обработки.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM и flash при неиспользовании: только enum, constexpr и
//    шаблоны. waitUntil инлайнится в место вызова (~20 байт).
//
//  ПРИМЕР
//    // Реализация шины следует контракту:
//    fn::Result r = fn::Uart::begin({9600});
//    fn::bus::writeLine<fn::Uart>(FN_PSTR("boot ok"));
//
//    // Ожидание с таймаутом вместо ручного цикла:
//    r = fn::bus::waitUntil([]{ return fn::Uart::available() >= 4; }, 100);
//    if (!fn::ok(r)) return r;
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Compiler.h, core/Types.h,
//               core/Progmem.h, timer/Tick.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "Progmem.h"
#include "../timer/Tick.h"

#include <stdint.h>
#include <avr/pgmspace.h>

namespace fn {
namespace bus {

// ---------------------------- общие параметры ------------------------------
enum class Parity : uint8_t { None = 0, Even = 1, Odd = 2 };
enum class StopBits : uint8_t { One = 0, Two = 1 };
enum class BitOrder : uint8_t { MsbFirst = 0, LsbFirst = 1 };

// Режимы SPI: индекс = (CPOL << 1) | CPHA.
enum class Mode : uint8_t { M0 = 0, M1 = 1, M2 = 2, M3 = 3 };

constexpr uint32_t kBaudDefault = 9600UL;
constexpr uint32_t kI2cStandard = 100000UL;
constexpr uint32_t kI2cFast     = 400000UL;
constexpr uint32_t kSpiDefault  = 4000000UL;

// ---------------------------- ожидание с таймаутом -------------------------
// pred() -> bool. timeoutMs == 0: одна проверка без ожидания.
template <typename Pred>
inline Result waitUntil(Pred pred, uint32_t timeoutMs) {
    const uint32_t t0 = ::fn::millis();
    for (;;) {
        if (pred()) return Result::Ok;
        if (elapsed(t0, ::fn::millis()) >= timeoutMs) return Result::Timeout;
    }
}

template <typename Pred>
inline Result waitUntilUs(Pred pred, uint32_t timeoutUs) {
    const uint32_t t0 = ::fn::micros();
    for (;;) {
        if (pred()) return Result::Ok;
        if (elapsed(t0, ::fn::micros()) >= timeoutUs) return Result::Timeout;
    }
}

// ---------------------------- запись ---------------------------------------
// Параметр шаблона — сама шина (класс со статическими методами по контракту).
// Отдельный тег-тип не нужен: fn::bus::writeP<fn::Uart>(FN_PSTR("hi")).
template <typename Bus>
inline Result writeByte(uint8_t b) {
    return Bus::write(&b, 1);
}

template <typename Bus>
inline Result writeP(PGM_P s, uint16_t len) {
    if (s == nullptr) return Result::BadArg;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    const uint8_t* end = p + len;
    for (; p != end; ++p) {
        const uint8_t b = pgm_read_byte(p);
        const Result r = Bus::write(&b, 1);
        if (!ok(r)) return r;
    }
    return Result::Ok;
}

// Строка из PROGMEM до терминатора. Терминатор не передаётся.
template <typename Bus>
inline Result writeP(PGM_P s) {
    if (s == nullptr) return Result::BadArg;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
    for (;;) {
        const uint8_t b = pgm_read_byte(p++);
        if (b == 0) return Result::Ok;
        const Result r = Bus::write(&b, 1);
        if (!ok(r)) return r;
    }
}

template <typename Bus>
inline Result writeStr(const char* s) {
    if (s == nullptr) return Result::BadArg;
    while (*s != '\0') {
        const uint8_t b = static_cast<uint8_t>(*s++);
        const Result r = Bus::write(&b, 1);
        if (!ok(r)) return r;
    }
    return Result::Ok;
}

template <typename Bus>
inline Result writeLine(PGM_P s) {
    const Result r = writeP<Bus>(s);
    if (!ok(r)) return r;
    static const char crlf[] FN_PROGMEM = "\r\n";
    return writeP<Bus>(crlf, 2);
}

// Передача с заполнителем: tx == nullptr означает «слать fill».
// Нужно для SPI-чтения, где мастер обязан тактировать шину.
template <typename Bus>
inline Result transferFill(uint8_t* rx, uint16_t len, uint8_t fill = 0xFF) {
    if (rx == nullptr) return Result::BadArg;
    for (uint16_t i = 0; i < len; ++i) {
        uint8_t out = fill;
        const Result r = Bus::transfer(&out, &rx[i], 1);
        if (!ok(r)) return r;
    }
    return Result::Ok;
}

}  // namespace bus
}  // namespace fn

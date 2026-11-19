// ============================================================================
//  Eeprom.cpp — доступ к EEPROM поверх avr-libc, с границами и учётом износа
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация Eeprom. Тайминг записи (EEMPE -> EEPE в пределах 4 тактов)
//    и ожидание SELFPRGEN делегированы avr-libc: это手写 ассемблер, который
//    делает правильно то, что ручной C-код делает на границе допуска.
//    Свой код отвечает за границы, коды результата, update-семантику и счётчик.
//
//  ПОЧЕМУ НЕ СВОИ РЕГИСТРЫ
//    Прежняя версия писала EECR |= _BV(EEMPE); EECR |= _BV(EEPE) — две sbi
//    по 2 такта, то есть ровно на пределе 4-тактового окна. Любое изменение
//    кода генерации (другой -O, LTO, обёртка) сдвигает тайминг и запись
//    молча не происходит. avr-libc гарантирует последовательность.
//
//  ЗАЩИТА ОТ ISR
//    Ожидание готовности (eeprom_busy_wait, до 3.4 мс) выполняется с
//    разрешёнными прерываниями. Под AtomicBlock — только обращение к
//    EEAR/EEDR: между установкой адреса и чтением данных не должен
//    вклиниться ISR, который тоже трогает EEPROM. Тайминг EEMPE->EEPE
//    защищает avr-libc. Блочные read/write идут через avr-libc целиком:
//    конкуренцию с ISR обеспечивает вызывающий.
//
//  UPDATE-СЕМАНТИКА
//    update читает байт, сравнивает и пишет только при отличии. Ресурс
//    EEPROM — 100k циклов на байт, поэтому для настроек и счётчиков это
//    единственный правильный путь. Блочный update идёт побайтово: только
//    так можно посчитать число реально записанных байт.
//
//  ОГРАНИЧЕНИЯ
//    - Запись байта ~3.4 мс, блокирующая; прерывания разрешены во время ожидания.
//    - При активном WDT длинные блоки надо разбивать и кормить таймер.
//    - Файл должен лежать в src/store/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Eeprom.h, core/Atomic.h, core/Crc.h, avr/eeprom.h, avr/io.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_EEPROM

#include "Eeprom.h"

#include <avr/io.h>
#include <avr/eeprom.h>

namespace fn {
namespace eeprom_detail {

Result last = Result::Ok;

#if FN_EEPROM_STATS
uint32_t writtenCount = 0;
#endif

inline Result fail(Result r) { last = r; return r; }
inline Result done() { last = Result::Ok; return Result::Ok; }

inline void countWrite() {
#if FN_EEPROM_STATS
    ++writtenCount;
#endif
}

// Запись одного байта без проверки границ (границы проверены вызывающим).
// Ожидание предыдущей записи (~3.4 мс) идёт С РАЗРЕШЁННЫМИ прерываниями:
// под AtomicBlock только окно EEAR/EEDR, которое avr-libc защищает сам.
// Иначе блочный update держал бы cli по 3.4 мс на байт — UART терял байты,
// millis() пропускал тики (G-3).
void writeRaw(uint16_t addr, uint8_t v) {
    eeprom_busy_wait();                 // вне критической секции
    {
        AtomicBlock lock;
        eeprom_write_byte(reinterpret_cast<uint8_t*>(addr), v);
    }
    countWrite();
}

inline uint8_t readRaw(uint16_t addr) {
    eeprom_busy_wait();                 // чтение тоже ждёт EEPE
    AtomicBlock lock;
    return eeprom_read_byte(reinterpret_cast<const uint8_t*>(addr));
}

}  // namespace eeprom_detail

// ============================ готовность ===================================
bool Eeprom::isReady() { return (EECR & (1u << EEPE)) == 0; }

void Eeprom::waitReady() { eeprom_busy_wait(); }

// ============================ байт =========================================
uint8_t Eeprom::read(uint16_t addr) {
    if (!validRange(addr)) {
        eeprom_detail::fail(Result::OutOfRange);
        return 0xFF;
    }
    eeprom_detail::done();
    return eeprom_detail::readRaw(addr);
}

Result Eeprom::read(uint16_t addr, uint8_t& out) {
    if (!validRange(addr)) return eeprom_detail::fail(Result::OutOfRange);
    out = eeprom_detail::readRaw(addr);
    return eeprom_detail::done();
}

Result Eeprom::write(uint16_t addr, uint8_t v) {
    if (!validRange(addr)) return eeprom_detail::fail(Result::OutOfRange);
    eeprom_detail::writeRaw(addr, v);
    return eeprom_detail::done();
}

Result Eeprom::update(uint16_t addr, uint8_t v) {
    if (!validRange(addr)) return eeprom_detail::fail(Result::OutOfRange);
    if (eeprom_detail::readRaw(addr) != v) eeprom_detail::writeRaw(addr, v);
    return eeprom_detail::done();
}

// ============================ блок =========================================
Result Eeprom::read(uint16_t addr, void* dst, uint16_t len) {
    if (dst == nullptr && len) return eeprom_detail::fail(Result::BadArg);
    if (len == 0) return eeprom_detail::done();
    if (!validRange(addr, len)) return eeprom_detail::fail(Result::OutOfRange);
    eeprom_read_block(dst, reinterpret_cast<const void*>(addr), len);
    return eeprom_detail::done();
}

Result Eeprom::write(uint16_t addr, const void* src, uint16_t len) {
    if (src == nullptr && len) return eeprom_detail::fail(Result::BadArg);
    if (len == 0) return eeprom_detail::done();
    if (!validRange(addr, len)) return eeprom_detail::fail(Result::OutOfRange);
    eeprom_write_block(src, reinterpret_cast<void*>(addr), len);
#if FN_EEPROM_STATS
    eeprom_detail::writtenCount += len;
#endif
    return eeprom_detail::done();
}

Result Eeprom::update(uint16_t addr, const void* src, uint16_t len, uint16_t* changed) {
    if (changed != nullptr) *changed = 0;
    if (src == nullptr && len) return eeprom_detail::fail(Result::BadArg);
    if (len == 0) return eeprom_detail::done();
    if (!validRange(addr, len)) return eeprom_detail::fail(Result::OutOfRange);

    const uint8_t* p = static_cast<const uint8_t*>(src);
    const uint8_t* end = p + len;
    uint16_t a = addr;
    uint16_t n = 0;
    for (; p != end; ++p, ++a) {
        if (eeprom_detail::readRaw(a) != *p) {
            eeprom_detail::writeRaw(a, *p);
            ++n;
        }
    }
    if (changed != nullptr) *changed = n;
    return eeprom_detail::done();
}

// ============================ диагностика ==================================
Result Eeprom::lastError() { return eeprom_detail::last; }

uint32_t Eeprom::written() {
#if FN_EEPROM_STATS
    AtomicBlock lock;
    return eeprom_detail::writtenCount;
#else
    return 0;
#endif
}

void Eeprom::clearStats() {
#if FN_EEPROM_STATS
    AtomicBlock lock;
    eeprom_detail::writtenCount = 0;
#endif
}

// ============================ legacy CRC ===================================
uint8_t Eeprom::crc8(const void* buf, uint16_t len) {
    return crc::crc8_poly31(buf, len);
}

uint16_t Eeprom::crc16(const void* buf, uint16_t len) {
    return crc::crc16_ccitt(buf, len);
}

}  // namespace fn

#endif  // F_USE_EEPROM

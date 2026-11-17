#pragma once
// ============================================================================
//  Eeprom.h — низкоуровневый доступ к EEPROM (1024 байта на ATmega328P)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Байтовый и блочный доступ к энергонезависимой памяти с проверкой границ,
//    кодами результата и счётчиком износа. Регистровые последовательности
//    делегированы avr-libc (ассемблер, корректный тайминг EEMPE/EEPE),
//    поверх них — границы, диагностика и update-семантика.
//
//  API (namespace fn, класс Eeprom — только статические методы)
//    constexpr size() -> uint16_t             1024 (fn::EepromSize)
//    validRange(addr, len) -> bool            без переполнения
//    isReady() -> bool                        EEPROM не занята записью
//    waitReady()                              ожидание с ограничением
//
//    --- байт ---
//    read(addr) -> uint8_t                    0xFF при OutOfRange
//    read(addr, uint8_t& out) -> Result
//    write(addr, v) -> Result                 ~3.4 мс, всегда пишет
//    update(addr, v) -> Result                пишет только если отличается
//
//    --- блок ---
//    read(addr, dst, len) -> Result
//    write(addr, src, len) -> Result
//    update(addr, src, len, uint16_t* changed = nullptr) -> Result
//                                             changed: сколько байт записано
//
//    --- диагностика ---
//    lastError() -> Result
//    written() -> uint32_t                    всего байт записано за сессию
//    clearStats()
//
//    --- legacy CRC-алиасы (делегаты в fn::crc) ---
//    crc8(buf, len) -> uint8_t                CRC-8/NRSC-5 (poly 0x31)
//    crc16(buf, len) -> uint16_t              CRC-16/CCITT-FALSE
//
//  КОНФИГУРАЦИЯ
//    FN_EEPROM_STATS  1 (по умолчанию) — счётчик записанных байт (4 байта SRAM);
//                     0 — счётчика нет, written() возвращает 0
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Границы проверяются ВСЕГДА. Прежняя версия писала EEAR = addr, а на
//      328p EEAR 10-битный: адрес 2000 превращался в 976 и портил чужие данные
//      без единого признака ошибки. Теперь OutOfRange и никакой записи.
//    - Ресурс EEPROM: 100 000 циклов записи на байт. write() пишет всегда,
//      update() — только при отличии. Для счётчиков и настроек используйте
//      update(): разница в износе на порядки.
//    - Запись одного байта занимает ~3.4 мс и блокирует вызывающего. Блочная
//      запись N байт — N * 3.4 мс: 1024 байта = 3.5 с. При активном WDT
//      (15 мс...8 с) это сброс. Кормите WDT между порциями или пишите блоками
//      по 8..16 байт. Прерывания между байтами НЕ запрещаются.
//    - Байтовые операции обёрнуты в AtomicBlock: адрес EEAR и бит EERE
//      неделимы относительно ISR, который тоже обращается к EEPROM.
//      Блочные read/write используют avr-libc и защищают только тайминг
//      записи; если ваш ISR лезет в EEPROM, оберните блок в AtomicBlock
//      сами (ценой задержки прерываний на всё время операции).
//    - Чтение не изнашивает память и занимает ~4 такта плюс ожидание записи.
//    - erased-состояние EEPROM: 0xFF. read() вне диапазона возвращает 0xFF и
//      выставляет lastError() = OutOfRange — отличайте по коду, не по значению.
//    - CRC-алиасы сохранены для совместимости со старым кодом; новый код
//      должен звать fn::crc напрямую.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 1 байт (lastError) + 4 байта счётчика при FN_EEPROM_STATS.
//    Flash: ~250 байт плюс функции avr-libc (~80 байт). Таблиц нет.
//
//  ПРИМЕР
//    uint8_t cfg[16];
//    fn::Eeprom::read(0, cfg, sizeof(cfg));
//    cfg[3] = 42;
//    fn::Eeprom::update(0, cfg, sizeof(cfg));      // запишет только байт 3
//
//    if (fn::Eeprom::written() > 90000) { /* пора менять МК или схему износа */ }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Crc.h,
//               avr/eeprom.h; реализация — Eeprom.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Crc.h"

#include <stdint.h>

#if F_USE_EEPROM

#ifndef FN_EEPROM_STATS
#define FN_EEPROM_STATS 1
#endif

namespace fn {

class Eeprom {
public:
    static constexpr uint16_t size() { return EepromSize; }

    static bool validRange(uint16_t addr, uint16_t len = 1) {
        return (len <= size()) && (addr <= uint16_t(size() - len));
    }

    static bool isReady();
    static void waitReady();

    // --- байт ---
    static uint8_t read(uint16_t addr);
    static Result read(uint16_t addr, uint8_t& out);
    static Result write(uint16_t addr, uint8_t v);
    static Result update(uint16_t addr, uint8_t v);

    // --- блок ---
    static Result read(uint16_t addr, void* dst, uint16_t len);
    static Result write(uint16_t addr, const void* src, uint16_t len);
    static Result update(uint16_t addr, const void* src, uint16_t len,
                         uint16_t* changed = nullptr);

    // --- диагностика ---
    static Result lastError();
    static uint32_t written();
    static void clearStats();

    // --- legacy CRC ---
    static uint8_t crc8(const void* buf, uint16_t len);
    static uint16_t crc16(const void* buf, uint16_t len);

private:
    Eeprom() = delete;
};

}  // namespace fn

#endif  // F_USE_EEPROM

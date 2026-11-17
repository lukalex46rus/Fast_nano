#pragma once
// ============================================================================
//  EepromStore.h — версионируемое хранилище структуры в EEPROM
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Хранит одну структуру T вместе с заголовком (CRC16 + size + version).
//    При begin() проверяет целостность: совпало — читает, не совпало —
//    записывает значения по умолчанию. Запись двухфазная (данные, затем
//    заголовок), поэтому сбой питания в середине оставляет блок невалидным,
//    а не полуправдивым.
//
//  ФОРМАТ БЛОКА (совместим с прежней версией, существующие данные читаются)
//    смещение 0: uint16_t crc     CRC-16/CCITT-FALSE, init 0xFFFF
//    смещение 2: uint16_t size    sizeof(T)
//    смещение 4: uint8_t  ver     версия структуры
//    смещение 5: T data
//    CRC считается по: size_lo, size_hi, ver, затем все байты data.
//    Итого blockSize() = 5 + sizeof(T).
//
//  API
//    template <typename T> class EepromStore
//      enum class Status : uint8_t { None, Read, Written, Defaults, Error }
//      EepromStore(addr, ver = 1)
//      begin(const T& defaults) -> Status   Read | Defaults | Error
//      commit(bool force = false) -> Status Written | None | Error
//      write(force = false) -> Status       legacy-алиас commit
//      tick() -> Status                     пишет только если помечено грязным
//      markDirty() / dirty() -> bool
//      reset() -> Status                    инвалидирует блок (1 байт записи)
//      data() / data() const -> T&
//      blockSize() / nextAddr() -> uint16_t
//      lastError() -> Result
//      crc() -> uint16_t                    CRC текущего образа в RAM
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - T обязан быть trivially copyable (static_assert). Структура с
//      указателем, ссылкой или vtable сохранится как мусор и прочитается
//      как мусор: адрес во flash/RAM не переживает перезапуск.
//    - tick() НЕ пересчитывает CRC каждый вызов. Прежняя версия делала это
//      всегда: для 64-байт структуры ~512 итераций CRC ≈ 200 мкс на каждой
//      итерации loop(). Теперь tick() смотрит только флаг dirty; CRC
//      считается один раз в commit(). Меняете data() — зовите markDirty().
//    - Запись занимает (5 + sizeof(T)) * 3.4 мс. Для T = 64 байт это 235 мс:
//      при активном WDT с периодом меньше 250 мс будет сброс. Кормите WDT
//      до и после commit() либо дробите структуру на несколько хранилищ.
//    - Износ: 100k циклов на байт. commit() без force пишет только при
//      отличии CRC, но заголовок перезаписывается целиком каждый раз.
//      Для частых сохранений используйте несколько адресов по кругу
//      (wear leveling) — этот модуль одним блоком не занимается.
//    - reset() пишет один байт 0xFF в начало заголовка: CRC перестаёт
//      сходиться, следующий begin() вернёт Defaults. Дешевле полного стирания.
//    - Порядок записи: сначала данные, потом заголовок. При сбое питания
//      между фазами заголовок останется старым и CRC не сойдётся — блок
//      будет отвергнут, а не принят с半 новыми данными.
//    - Версия ver参与了 CRC, поэтому смена версии автоматически
//      инвалидирует старый блок: begin() запишет новые defaults.
//    - Границы EEPROM проверяются через Eeprom::validRange; объект, не
//      влезающий в 1024 байта, вернёт Error из begin() и ничего не запишет.
//    - sizeof(EepromStore<T>) = sizeof(T) + 8 (addr 2, ver 1, flags 1, crc 2,
//      lastError 1, выравнивание 1). Образ данных лежит в RAM постоянно.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: sizeof(T) + 8 на объект. Flash: ~350 байт при полном использовании,
//    CRC берётся из общего движка fn::crc (своей копии цикла больше нет).
//
//  ПРИМЕР
//    struct Config { uint16_t steps; uint8_t mode; uint8_t pad[5]; };
//    fn::EepromStore<Config> store(0, 2);
//
//    void setup() {
//        Config def{1000, 1, {0}};
//        auto s = store.begin(def);
//        // s == Read: восстановлено; Defaults: записаны умолчания
//    }
//    void loop() {
//        if (userChangedSomething) {
//            store.data().mode = 3;
//            store.markDirty();
//        }
//        store.tick();          // пишет только при dirty, CRC не гоняет зря
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Compiler.h, core/Crc.h,
//               store/Eeprom.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Compiler.h"
#include "../core/Crc.h"
#include "Eeprom.h"

#include <stdint.h>

#if F_USE_EEPROM

namespace fn {

template <typename T>
class EepromStore {
    static_assert(__is_trivially_copyable(T),
                  "EepromStore<T>: T должен быть trivially copyable (без указателей и vtable)");

public:
    enum class Status : uint8_t {
        None,      ///< ничего не делалось / изменений нет
        Read,      ///< блок валиден, данные восстановлены
        Written,   ///< блок записан
        Defaults,  ///< блок был невалиден, записаны умолчания
        Error      ///< не влезает в EEPROM или ошибка доступа
    };

    EepromStore(uint16_t addr, uint8_t ver = 1)
        : _addr(addr), _ver(ver), _flags(0), _crc(0xFFFF), _err(Result::Ok), _data{} {}

    Status begin(const T& defaults) {
        if (!Eeprom::validRange(_addr, blockSize())) {
            _err = Result::OutOfRange;
            return Status::Error;
        }

        Header h;
        Result r = Eeprom::read(_addr, &h, sizeof(h));
        if (!ok(r)) { _err = r; return Status::Error; }

        if (h.size == sizeof(T) && h.ver == _ver && storedCrc(h) == h.crc) {
            r = Eeprom::read(uint16_t(_addr + sizeof(Header)), &_data, sizeof(T));
            if (!ok(r)) { _err = r; return Status::Error; }
            _crc = h.crc;
            _flags = 0;
            _err = Result::Ok;
            return Status::Read;
        }

        _data = defaults;
        _flags = FlagDirty;
        const Status s = commit(true);
        return (s == Status::Written) ? Status::Defaults : Status::Error;
    }

    Status commit(bool force = false) {
        if (!Eeprom::validRange(_addr, blockSize())) {
            _err = Result::OutOfRange;
            return Status::Error;
        }

        const uint16_t c = computeCrc();
        if (!force && c == _crc) {
            _flags = uint8_t(_flags & uint8_t(~FlagDirty));
            _err = Result::Ok;
            return Status::None;
        }

        // Данные сначала, заголовок потом: сбой питания оставит блок невалидным.
        Result r = Eeprom::write(uint16_t(_addr + sizeof(Header)), &_data, sizeof(T));
        if (!ok(r)) { _err = r; return Status::Error; }

        Header h;
        h.crc = c;
        h.size = uint16_t(sizeof(T));
        h.ver = _ver;
        r = Eeprom::write(_addr, &h, sizeof(h));
        if (!ok(r)) { _err = r; return Status::Error; }

        _crc = c;
        _flags = uint8_t(_flags & uint8_t(~FlagDirty));
        _err = Result::Ok;
        return Status::Written;
    }

    Status write(bool force = false) { return commit(force); }   // legacy

    // Дешёвый опрос: CRC не считается, пока данные не помечены грязными.
    Status tick() {
        if ((_flags & FlagDirty) == 0) return Status::None;
        return commit(false);
    }

    void markDirty() { _flags = uint8_t(_flags | FlagDirty); }
    bool dirty() const { return (_flags & FlagDirty) != 0; }

    Status reset() {
        const Result r = Eeprom::write(_addr, uint8_t(0xFF));
        if (!ok(r)) { _err = r; return Status::Error; }
        _crc = 0xFFFF;
        _err = Result::Ok;
        return Status::Written;
    }

    T&       data()       { return _data; }
    const T& data() const { return _data; }

    uint16_t blockSize() const { return uint16_t(sizeof(Header) + sizeof(T)); }
    uint16_t nextAddr()  const { return uint16_t(_addr + blockSize()); }
    Result   lastError() const { return _err; }
    uint16_t crc() const { return computeCrc(); }

private:
    struct FN_PACKED Header {
        uint16_t crc;
        uint16_t size;
        uint8_t  ver;
    };
    static_assert(sizeof(Header) == 5, "EepromStore: заголовок должен быть 5 байт");

    static constexpr uint8_t FlagDirty = 0x01;

    // Формат CRC сохранён прежним: size_lo, size_hi, ver, data.
    uint16_t computeCrc() const {
        uint16_t c = 0xFFFF;
        c = crc::crc16_ccitt_update(c, uint8_t(sizeof(T)));
        c = crc::crc16_ccitt_update(c, uint8_t(sizeof(T) >> 8));
        c = crc::crc16_ccitt_update(c, _ver);
        return crc::crc16_ccitt_update(c, &_data, uint16_t(sizeof(T)));
    }

    uint16_t storedCrc(const Header& h) const {
        uint16_t c = 0xFFFF;
        c = crc::crc16_ccitt_update(c, uint8_t(h.size));
        c = crc::crc16_ccitt_update(c, uint8_t(h.size >> 8));
        c = crc::crc16_ccitt_update(c, h.ver);
        const uint16_t base = uint16_t(_addr + sizeof(Header));
        for (uint16_t i = 0; i < h.size; ++i) {
            c = crc::crc16_ccitt_update(c, Eeprom::read(uint16_t(base + i)));
        }
        return c;
    }

    uint16_t _addr;
    uint8_t  _ver;
    uint8_t  _flags;
    uint16_t _crc;
    Result   _err;
    T        _data;
};

}  // namespace fn

#endif  // F_USE_EEPROM

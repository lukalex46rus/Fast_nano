#pragma once
// ============================================================================
//  Crc.h — CRC-8 / CRC-16 / CRC-32: единый движок, потоковый и блочный расчёт
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Контрольные суммы для протоколов, EEPROM и пакетного обмена. Один
//    шаблонный движок вместо одиннадцати копий одного и того же цикла:
//    полином, ширина и направление задаются параметрами шаблона, мёртвая
//    ветка вырезается компилятором.
//
//  АЛГОРИТМЫ (готовые алиасы движка)
//    Crc8Smbus   uint8_t  poly 0x07        MSB  init 0x00   CRC-8/SMBUS
//    Crc8Maxim   uint8_t  poly 0x8C (refl) LSB  init 0x00   CRC-8/MAXIM-DOW
//    Crc8Nrsc5   uint8_t  poly 0x31        MSB  init 0xFF   CRC-8/NRSC-5
//    Crc16Ccitt  uint16_t poly 0x1021      MSB  init 0xFFFF CRC-16/CCITT-FALSE
//    Crc16Modbus uint16_t poly 0xA001(refl)LSB  init 0xFFFF CRC-16/MODBUS
//    Crc32Std    uint32_t poly 0xEDB88320  LSB  init/xor 0xFFFFFFFF
//
//  API — движок
//    E::step(crc, b)            один байт
//    E::buf(crc, p, n)          блок из RAM;  p == nullptr -> crc без изменений
//    E::bufP(crc, p, n)         блок из PROGMEM
//    E::value_type              тип регистра
//
//  API — потоковый объект (снимает проблему финального XOR)
//    Stream<E, Init, XorOut = 0>
//      add(b) / add(p, n) / addP(p, n)
//      raw()   -> регистр без финального XOR
//      value() -> готовая контрольная сумма (регистр ^ XorOut)
//      reset()
//
//  API — функции с прежними именами
//    crc8_update / crc8 / crc8_P                       init 0x00
//    crc8_maxim_update / crc8_maxim / crc8_maxim_P     init 0x00  (ИСПРАВЛЕНО)
//    crc8_poly31_update / crc8_poly31 / crc8_poly31_P  init 0xFF  (_P добавлен)
//    crc16_ccitt_update / crc16_ccitt / crc16_ccitt_P  init 0xFFFF
//    crc16_xmodem / crc16_xmodem_P                     init 0x0000
//    crc16_modbus_update / crc16_modbus / crc16_modbus_P init 0xFFFF
//    crc32_update / crc32 / crc32_P                    init 0xFFFFFFFF
//    crc32_final(c) -> c ^ 0xFFFFFFFF
//    crc16(...) — legacy-алиас crc16_ccitt
//
//  КОНТРАКТ ПОТОКОВОГО РАСЧЁТА (важно)
//    *_update() возвращают СЫРОЙ регистр, без финального XOR. Для CRC-32
//    финальный XOR обязателен, и его делает либо crc32()/crc32_P(), либо
//    crc32_final() в конце цепочки, либо Stream::value(). Смешивать нельзя:
//      правильно: c = crc32_update(0xFFFFFFFF, p1, n1);
//                 c = crc32_update(c, p2, n2);
//                 sum = crc32_final(c);
//      неправильно: sum = crc32_update(crc32(p1,n1), p2, n2);
//    Остальные алгоритмы финального XOR не имеют, у них raw == value.
//
//  ОГРАНИЧЕНИЯ
//    - Расчёт побитовый, таблиц нет. Обоснование: nibble-таблица ускорила бы
//      расчёт примерно вчетверо, но стоит 16/32/64 байта flash на алгоритм
//      (176 байт на все шесть) и зависит от полинома, то есть ломает единый
//      движок. Для пакетов до 64 байт побитовый расчёт занимает ~240 мкс
//      на 16 МГц — при приоритете flash это правильный размен.
//    - nullptr во всех блочных функциях безопасен: возвращается crc без
//      изменений. len == 0 — то же самое.
//    - len имеет тип uint16_t: максимум 65535 байт за вызов.
//    - Знаковые типы регистром быть не могут: сдвиги знаковых дали бы UB.
//
//  ПРОВЕРОЧНЫЕ ЗНАЧЕНИЯ (строка "123456789", 9 байт)
//    crc8        -> 0xF4
//    crc8_maxim  -> 0xA1   (при init=0xFF получалось 0x0B — это НЕ стандарт)
//    crc8_poly31 -> 0xF7
//    crc16_ccitt -> 0x29B1
//    crc16_modbus-> 0x4B37
//    crc32       -> 0xCBF43926
//    Значения проверены побитовым моделированием с явными refin/refout.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт RAM и таблиц. ~50-80 тактов на байт. Каждый инстанцированный
//    движок — своя копия цикла (~30-60 байт flash); неиспользуемые алиасы
//    не инстанцируются и не стоят ничего.
//
//  ПРИМЕР
//    uint8_t pkt[8] = {...};
//    uint16_t m = fn::crc::crc16_modbus(pkt, 6);
//    pkt[6] = uint8_t(m); pkt[7] = uint8_t(m >> 8);
//
//    fn::crc::Stream<fn::crc::Crc32Std, 0xFFFFFFFFUL, 0xFFFFFFFFUL> acc;
//    acc.add(header, 4);
//    acc.addP(payloadInFlash, len);
//    uint32_t sum = acc.value();
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, avr/pgmspace.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>
#include <avr/pgmspace.h>

#if F_USE_CRC

namespace fn {
namespace crc {
namespace detail {

// Единственная реализация CRC. Reflected — константа шаблона, поэтому
// неиспользуемая ветка вырезается оптимизатором (if constexpr не нужен).
template <typename T, T Poly, bool Reflected>
struct Engine {
    using value_type = T;

    static constexpr uint8_t width = ::fn::detail::WidthOf<T>::value;
    static constexpr T topBit = T(T(1) << (width - 1u));

    static T step(T c, uint8_t b) {
        if (Reflected) {
            c = T(c ^ T(b));
            for (uint8_t k = 0; k < 8; ++k) {
                c = (c & T(1)) ? T(T(c >> 1) ^ Poly) : T(c >> 1);
            }
        } else {
            c = T(c ^ T(T(b) << (width - 8u)));
            for (uint8_t k = 0; k < 8; ++k) {
                c = (c & topBit) ? T(T(c << 1) ^ Poly) : T(c << 1);
            }
        }
        return c;
    }

    static T buf(T c, const void* p, uint16_t n) {
        if (p == nullptr) return c;
        const uint8_t* q = static_cast<const uint8_t*>(p);
        const uint8_t* end = q + n;
        for (; q != end; ++q) c = step(c, *q);
        return c;
    }

    static T bufP(T c, const void* p, uint16_t n) {
        if (p == nullptr) return c;
        const uint8_t* q = static_cast<const uint8_t*>(p);
        const uint8_t* end = q + n;
        for (; q != end; ++q) c = step(c, pgm_read_byte(q));
        return c;
    }
};

}  // namespace detail

// ---------------------------- готовые алгоритмы ----------------------------
using Crc8Smbus   = detail::Engine<uint8_t, 0x07, false>;
using Crc8Maxim   = detail::Engine<uint8_t, 0x8C, true>;
using Crc8Nrsc5   = detail::Engine<uint8_t, 0x31, false>;
using Crc16Ccitt  = detail::Engine<uint16_t, 0x1021, false>;
using Crc16Modbus = detail::Engine<uint16_t, 0xA001, true>;
using Crc32Std    = detail::Engine<uint32_t, 0xEDB88320UL, true>;

// ---------------------------- потоковый объект -----------------------------
template <typename E, uint32_t Init, uint32_t XorOut = 0>
class Stream {
public:
    using value_type = typename E::value_type;

    Stream() : _c(value_type(Init)) {}

    void reset() { _c = value_type(Init); }
    void add(uint8_t b) { _c = E::step(_c, b); }    
    void add(const void* p, uint16_t n) { _c = E::buf(_c, p, n); }
    void addP(const void* p, uint16_t n) { _c = E::bufP(_c, p, n); }

    value_type raw() const { return _c; }
    value_type value() const { return value_type(_c ^ value_type(XorOut)); }

private:
    value_type _c;
};

// ---------------------------- CRC-8 / SMBUS --------------------------------
inline uint8_t crc8_update(uint8_t c, uint8_t b) { return Crc8Smbus::step(c, b); }
inline uint8_t crc8_update(uint8_t c, const void* p, uint16_t n) { return Crc8Smbus::buf(c, p, n); }
inline uint8_t crc8(const void* p, uint16_t n, uint8_t init = 0x00) { return Crc8Smbus::buf(init, p, n); }
inline uint8_t crc8_P(const void* p, uint16_t n, uint8_t init = 0x00) { return Crc8Smbus::bufP(init, p, n); }

// ---------------------------- CRC-8 / MAXIM-DOW ----------------------------
// init = 0x00, финального XOR нет. Совместимо с DS18B20 / DS2401.
inline uint8_t crc8_maxim_update(uint8_t c, uint8_t b) { return Crc8Maxim::step(c, b); }
inline uint8_t crc8_maxim_update(uint8_t c, const void* p, uint16_t n) { return Crc8Maxim::buf(c, p, n); }
inline uint8_t crc8_maxim(const void* p, uint16_t n, uint8_t init = 0x00) { return Crc8Maxim::buf(init, p, n); }
inline uint8_t crc8_maxim_P(const void* p, uint16_t n, uint8_t init = 0x00) { return Crc8Maxim::bufP(init, p, n); }

// ---------------------------- CRC-8 / NRSC-5 -------------------------------
// Исторический полином 0x31 MSB-first, применялся в Eeprom.
inline uint8_t crc8_poly31_update(uint8_t c, uint8_t b) { return Crc8Nrsc5::step(c, b); }
inline uint8_t crc8_poly31_update(uint8_t c, const void* p, uint16_t n) { return Crc8Nrsc5::buf(c, p, n); }
inline uint8_t crc8_poly31(const void* p, uint16_t n, uint8_t init = 0xFF) { return Crc8Nrsc5::buf(init, p, n); }
inline uint8_t crc8_poly31_P(const void* p, uint16_t n, uint8_t init = 0xFF) { return Crc8Nrsc5::bufP(init, p, n); }

// ---------------------------- CRC-16 / CCITT-FALSE -------------------------
inline uint16_t crc16_ccitt_update(uint16_t c, uint8_t b) { return Crc16Ccitt::step(c, b); }
inline uint16_t crc16_ccitt_update(uint16_t c, const void* p, uint16_t n) { return Crc16Ccitt::buf(c, p, n); }
inline uint16_t crc16_ccitt(const void* p, uint16_t n, uint16_t init = 0xFFFF) { return Crc16Ccitt::buf(init, p, n); }
inline uint16_t crc16_ccitt_P(const void* p, uint16_t n, uint16_t init = 0xFFFF) { return Crc16Ccitt::bufP(init, p, n); }

inline uint16_t crc16_xmodem(const void* p, uint16_t n, uint16_t init = 0x0000) { return Crc16Ccitt::buf(init, p, n); }
inline uint16_t crc16_xmodem_P(const void* p, uint16_t n, uint16_t init = 0x0000) { return Crc16Ccitt::bufP(init, p, n); }

// Legacy-алиас для миграции со старого Eeprom::crc16.
inline uint16_t crc16(const void* p, uint16_t n, uint16_t init = 0xFFFF) { return crc16_ccitt(p, n, init); }

// ---------------------------- CRC-16 / MODBUS ------------------------------
inline uint16_t crc16_modbus_update(uint16_t c, uint8_t b) { return Crc16Modbus::step(c, b); }
inline uint16_t crc16_modbus_update(uint16_t c, const void* p, uint16_t n) { return Crc16Modbus::buf(c, p, n); }
inline uint16_t crc16_modbus(const void* p, uint16_t n, uint16_t init = 0xFFFF) { return Crc16Modbus::buf(init, p, n); }
inline uint16_t crc16_modbus_P(const void* p, uint16_t n, uint16_t init = 0xFFFF) { return Crc16Modbus::bufP(init, p, n); }

// ---------------------------- CRC-32 ---------------------------------------
inline uint32_t crc32_update(uint32_t c, uint8_t b) { return Crc32Std::step(c, b); }
inline uint32_t crc32_update(uint32_t c, const void* p, uint16_t n) { return Crc32Std::buf(c, p, n); }
inline constexpr uint32_t crc32_final(uint32_t c) { return c ^ 0xFFFFFFFFUL; }

inline uint32_t crc32(const void* p, uint16_t n, uint32_t init = 0xFFFFFFFFUL) {
    return crc32_final(Crc32Std::buf(init, p, n));
}
inline uint32_t crc32_P(const void* p, uint16_t n, uint32_t init = 0xFFFFFFFFUL) {
    return crc32_final(Crc32Std::bufP(init, p, n));
}

}  // namespace crc
}  // namespace fn

#endif  // F_USE_CRC

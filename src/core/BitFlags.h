#pragma once
// ============================================================================
//  BitFlags.h — флаги и поля в одном целом (uint8_t / uint16_t / uint32_t)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Тонкая обёртка (sizeof == sizeof(T)) над целым для работы с масками:
//    состояние автомата, флаги устройств, регистры конфигурации. В отличие
//    от BitPack не хранит массив и не проверяет индексы — только маски.
//
//  API
//    BitFlags<T> f(начальное_значение = 0)   constexpr-конструктор
//    f.flags                                 прямой доступ к значению
//    set(mask) / clear(mask) / toggle(mask) / write(mask, v)
//    setAll() / clearAll()
//    writeMasked(mask, bits)                 биты УЖЕ на своих местах
//    writeBits(mask, bits)                   legacy-алиас writeMasked
//    writeField(mask, value)                 значение в младших битах, со сдвигом
//    read(mask)      -> T                    биты маски без сдвига
//    readField(mask) -> T                    поле, сдвинутое к младшим битам
//    isSet(mask) / all(mask)   -> bool       все биты маски установлены
//    isClear(mask) / none(mask)-> bool       все биты маски сброшены
//    any(mask)                 -> bool       хотя бы один бит установлен
//    all() / none() / any()    -> bool       по всему значению
//    compare(mask, v)          -> bool       поле равно v (v обрезается маской)
//    value() / setValue(v)
//    operator|=, &=, ^= (по маске T), operator|, &, ^, ~ (по BitFlags)
//    operator==, !=, explicit operator bool
//    enumMask<U>(e) / enumMask8(e)           enum / enum class -> маска
//    BitFlags8 / BitFlags16 / BitFlags32     готовые алиасы
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - T только беззнаковый (static_assert): знаковые сдвиги и ~ дали бы UB
//      и сюрпризы при расширении до int.
//    - Все параметры типа T, не шаблоны: маска приводится вызывающим.
//      Маска шире T обрезается — ответственность вызывающего.
//    - Дополнение выполняется в T (~T(mask)), 16-битные промежуточные
//      операции не возникают.
//    - compare маскирует и значение, и аргумент: compare(0x0F, 0x1F) == true.
//    - Класс не агрегат (C++11), но constexpr-конструктор позволяет
//      BitFlags8 f{5}; и constexpr BitFlags8 g(5);
//    - Атомарности нет: для регистров и разделяемых с ISR переменных —
//      fn::AtomicBlock либо fn::FastPin из Registers.h.
//
//  СТОИМОСТЬ (AVR)
//    sizeof == sizeof(T), все методы inline. Для uint8_t каждая операция —
//    1-3 машинные команды. writeField/readField используют bitf* со свёрткой
//    ctz при константной маске.
//
//  ПРИМЕР
//    enum : uint8_t { F_BUSY = 0x01, F_ERR = 0x02, F_READY = 0x04 };
//    fn::BitFlags8 st;
//    st.set(F_READY);
//    st.write(F_BUSY, true);
//    if (st.isSet(F_READY) && st.isClear(F_ERR)) { ... }
//
//    constexpr uint8_t M_MODE = fn::maskRange8(2, 0);
//    st.writeField(M_MODE, 5);
//    uint8_t mode = st.readField(M_MODE);      // 5
//
//    enum class Ev : uint8_t { Rx = 0x10, Tx = 0x20 };
//    st.set(fn::enumMask<uint8_t>(Ev::Rx));
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, BitMask.h, BitOps.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "BitMask.h"
#include "BitOps.h"

#include <stdint.h>

#if F_USE_BIT_FLAGS

namespace fn {

template <typename E>
inline constexpr uint8_t enumMask8(E e) { return uint8_t(e); }

template <typename U, typename E>
inline constexpr U enumMask(E e) { return U(e); }

template <typename T>
struct BitFlags {
    static_assert(detail::IsBitType<T>::value,
                  "fn::BitFlags<T>: T должен быть uint8_t, uint16_t или uint32_t");

    T flags;

    constexpr BitFlags(T v = 0) : flags(v) {}

    // ------------------------------ запись ---------------------------------
    void set(T mask)    { flags = T(flags | mask); }
    void clear(T mask)  { flags = T(flags & T(~mask)); }
    void toggle(T mask) { flags = T(flags ^ mask); }
    void write(T mask, bool v) { v ? set(mask) : clear(mask); }

    void setAll()   { flags = T(~T(0)); }
    void clearAll() { flags = T(0); }

    void writeMasked(T mask, T bits) { flags = T((flags & T(~mask)) | T(bits & mask)); }
    void writeBits(T mask, T bits)   { writeMasked(mask, bits); }  // legacy
    void writeField(T mask, T value) { flags = bitfSet(flags, value, mask); }

    // ------------------------------ чтение ---------------------------------
    T read(T mask)      const { return T(flags & mask); }
    T readField(T mask) const { return bitfGet(flags, mask); }

    bool isSet(T mask)   const { return T(flags & mask) == mask; }
    bool isClear(T mask) const { return T(flags & mask) == T(0); }
    bool any(T mask)     const { return T(flags & mask) != T(0); }
    bool all(T mask)     const { return isSet(mask); }
    bool none(T mask)    const { return isClear(mask); }

    bool all()  const { return flags == T(~T(0)); }
    bool none() const { return flags == T(0); }
    bool any()  const { return flags != T(0); }

    bool compare(T mask, T v) const { return T(flags & mask) == T(v & mask); }

    T    value() const { return flags; }
    void setValue(T v) { flags = v; }

    // ------------------------------ операторы ------------------------------
    BitFlags& operator|=(T mask) { set(mask); return *this; }
    BitFlags& operator&=(T mask) { flags = T(flags & mask); return *this; }
    BitFlags& operator^=(T mask) { toggle(mask); return *this; }

    BitFlags operator|(const BitFlags& o) const { return BitFlags(T(flags | o.flags)); }
    BitFlags operator&(const BitFlags& o) const { return BitFlags(T(flags & o.flags)); }
    BitFlags operator^(const BitFlags& o) const { return BitFlags(T(flags ^ o.flags)); }
    BitFlags operator~() const { return BitFlags(T(~flags)); }

    bool operator==(const BitFlags& o) const { return flags == o.flags; }
    bool operator!=(const BitFlags& o) const { return flags != o.flags; }
    explicit operator bool() const { return flags != T(0); }
};

using BitFlags8  = BitFlags<uint8_t>;
using BitFlags16 = BitFlags<uint16_t>;
using BitFlags32 = BitFlags<uint32_t>;

}  // namespace fn

#endif  // F_USE_BIT_FLAGS

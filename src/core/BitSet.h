#pragma once
// ============================================================================
//  BitSet.h — битовые множества с операторами: BitSet<N> и BitSetWide
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Алгебра множеств поверх BitPack / BitPackWide. Весь прикладной API
//    (set/clear/count/any/all/none/поиск/поля/сдвиги) наследуется из ядра
//    BitPack.h; здесь добавлены только операторы. Общий код операторов вынесен
//    в CRTP-примеси, поэтому статический и динамический варианты не дублируют
//    реализацию.
//
//  API (дополнительно к BitPack.h)
//    BitSet<N> и BitSetWide:
//      operator|=, operator&=, operator^=   поэлементно, на месте
//      difference(o)                        this = this \ o
//      operator==, operator!=               из ядра, с учётом инварианта хвоста
//      operator[](i)                        чтение бита (запись — set/write)
//      invert()                             инверсия на месте (из ядра)
//    Только BitSet<N> (владеет хранилищем):
//      operator~() const                    новый набор-инверсия
//      operator|, operator&, operator^      новый набор-результат
//    Конструкторы BitSetWide:
//      BitSetWide() / BitSetWide(buf, bits, clear = true)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Унарный ~ у BitSet<N> НЕ мутирует операнд (в старой версии мутировал).
//      Для инверсии на месте есть invert().
//    - BitSetWide — вид на чужой буфер, поэтому операторы, возвращающие
//      значение (~ | & ^), для него НАМЕРЕННО не определены: копия объекта
//      разделяет буфер, и «безобидное» b = ~a испортило бы a. Используйте
//      invert(), |=, &=, ^=, difference().
//    - Операнды должны иметь одинаковый amount(); для BitSet<N> это всегда так.
//      При разных размерах побайтовая операция идёт по размеру левого операнда.
//    - Инвариант хвоста сохраняется всеми операторами (normalize() в конце).
//    - Атомарности нет: при работе с ISR оборачивайте в fn::AtomicBlock.
//
//  СТОИМОСТЬ (AVR)
//    Примеси пустые, EBO: sizeof(BitSet<N>) == bytesCount,
//    sizeof(BitSetWide) == 7. Операторы по значению создают копию
//    (bytesCount байт на стеке) — в горячих циклах предпочитайте |= / invert().
//
//  ПРИМЕР
//    fn::BitSet<16> a, b;
//    a.set(0); a.set(5);
//    b.set(5); b.set(9);
//    a |= b;                        // {0,5,9}
//    fn::BitSet<16> c = a & b;      // {5,9}
//    fn::BitSet<16> d = ~a;         // a не изменился
//    uint16_t n = d.count();
//
//    uint8_t buf[4];
//    fn::BitSetWide w(buf, 32);
//    w.setAll();
//    w.invert();                    // вместо ~w
//
//  ЗАВИСИМОСТИ: BitPack.h
// ============================================================================

#include "BitPack.h"

#if F_USE_BIT_SET

namespace fn {
namespace detail {

// Операторы «на месте» — общие для владеющих и невладеющих множеств.
template <class D>
class BitAlgebraRef {
protected:
    D&       self()       { return static_cast<D&>(*this); }
    const D& self() const { return static_cast<const D&>(*this); }

public:
    D& operator|=(const D& o) {
        uint8_t* a = self().data();
        const uint8_t* b = o.data();
        if (a == nullptr || b == nullptr) return self();
        const uint8_t* end = a + self().sizeBytes();
        for (; a != end; ++a, ++b) *a = uint8_t(*a | *b);
        self().normalize();
        return self();
    }

    D& operator&=(const D& o) {
        uint8_t* a = self().data();
        const uint8_t* b = o.data();
        if (a == nullptr || b == nullptr) return self();
        const uint8_t* end = a + self().sizeBytes();
        for (; a != end; ++a, ++b) *a = uint8_t(*a & *b);
        self().normalize();
        return self();
    }

    D& operator^=(const D& o) {
        uint8_t* a = self().data();
        const uint8_t* b = o.data();
        if (a == nullptr || b == nullptr) return self();
        const uint8_t* end = a + self().sizeBytes();
        for (; a != end; ++a, ++b) *a = uint8_t(*a ^ *b);
        self().normalize();
        return self();
    }

    D& difference(const D& o) {
        uint8_t* a = self().data();
        const uint8_t* b = o.data();
        if (a == nullptr || b == nullptr) return self();
        const uint8_t* end = a + self().sizeBytes();
        for (; a != end; ++a, ++b) *a = uint8_t(*a & uint8_t(~*b));
        self().normalize();
        return self();
    }
};

// Операторы «по значению» — только для множеств, владеющих хранилищем.
template <class D>
class BitAlgebraValue : public BitAlgebraRef<D> {
protected:
    const D& self() const { return static_cast<const D&>(*this); }

public:
    D operator~() const { D r(self()); r.invert(); return r; }
    D operator|(const D& o) const { D r(self()); r |= o; return r; }
    D operator&(const D& o) const { D r(self()); r &= o; return r; }
    D operator^(const D& o) const { D r(self()); r ^= o; return r; }
};

}  // namespace detail

// --------------------------- статическое множество -------------------------
template <uint16_t N>
class BitSet : public BitPack<N>, public detail::BitAlgebraValue<BitSet<N>> {
public:
    BitSet() = default;
};

// ------------------------ множество на внешнем буфере ----------------------
class BitSetWide : public BitPackWide, public detail::BitAlgebraRef<BitSetWide> {
public:
    BitSetWide() = default;
    BitSetWide(uint8_t* buf, uint16_t bits, bool clear = true)
        : BitPackWide(buf, bits, clear) {}
};

}  // namespace fn

#endif  // F_USE_BIT_SET

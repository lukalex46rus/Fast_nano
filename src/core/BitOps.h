#pragma once
// ============================================================================
//  BitOps.h — битовые поля в скалярном значении (упаковка по маске)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Извлечение и сборка полей произвольной ширины и позиции по маске. Сдвиг
//    вычисляется из маски (ctz), позиции не хардкодятся. Ширина операций
//    берётся из типа значения: uint8_t остаётся 8-битным и не раздувается
//    до 32 бит — главная экономия flash на AVR.
//
//  API (T — любой целый 8/16/32; вычисления в беззнаковом аналоге UBits<T>)
//    bitfGet(value, mask)        -> T     поле, сдвинутое к младшим битам
//    bitfGetS(value, mask)       -> T     то же со знаковым расширением
//    bitfMake(field, mask)       -> T     поле, расставленное по маске
//    bitfSet(value, field, mask) -> T     записать поле в значение
//    bitfToggle(value, mask)     -> T     инвертировать все биты маски
//    bitfAll(value, mask)        -> bool  все биты маски установлены
//    bitfAny(value, mask)        -> bool  хотя бы один бит установлен
//    bitfNone(value, mask)       -> bool  ни одного бита не установлено
//    bitfWidth(mask)             -> uint8_t  ширина поля в битах
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - mask == 0: Get/GetS/Make -> 0, Set/Toggle возвращают value без
//      изменений, All -> false, Any -> false, None -> true. UB нет.
//    - field в Make/Set обрезается по маске: соседние биты не портятся.
//    - Маска может быть несмежной (0x81): поле соберётся из разрозненных
//      бит. Для bitfGetS это бессмысленно — используйте смежные маски.
//    - Тип deduce'ится по первому аргументу; маска и поле — невыводимые
//      параметры, поэтому литералы приводятся автоматически:
//      bitfGet(PORTB, 0x06) валиден.
//    - 64-битные типы не поддерживаются: подстановка UBits даст ошибку
//      компиляции (намеренно, на AVR это дорогие библиотечные вызовы).
//
//  СТОИМОСТЬ (AVR)
//    Константная маска -> ctz свёрнут, остаются 1-2 машинные операции.
//    Переменная маска -> цепочка тестов ctz, libgcc не тянется.
//
//  ПРИМЕР
//    constexpr uint8_t M_MODE = fn::maskRange8(1, 0);
//    constexpr uint8_t M_EN   = fn::bitMask8(3);
//    constexpr uint8_t M_DIV  = fn::maskRange8(7, 4);
//    uint8_t reg = 0;
//    reg = fn::bitfSet(reg, 2, M_MODE);
//    reg = fn::bitfSet(reg, 1, M_EN);
//    reg = fn::bitfSet(reg, 9, M_DIV);
//    uint8_t mode = fn::bitfGet(reg, M_MODE);   // 2
//    bool en = fn::bitfAll(reg, M_EN);          // true
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, BitMask.h, BitScan.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "BitMask.h"
#include "BitScan.h"

#include <stdint.h>

#if F_USE_BIT_OPS

namespace fn {
namespace detail {

// Знаковое расширение младших w бит значения v (v уже обрезано по w).
// Трюк (v ^ sign) - sign в беззнаковой арифметике: без сдвигов на ширину типа.
template <typename U>
inline constexpr U signExtend(U v, uint8_t w) {
    return (w == 0 || w >= sizeof(U) * 8u)
               ? v
               : U(U(v ^ U(U(1) << (w - 1))) - U(U(1) << (w - 1)));
}

// Единственная реализация операций над полями. U = uint8_t/uint16_t/uint32_t.
template <typename U>
struct Field {
    static constexpr U get(U v, U m) { return m ? U((v & m) >> ctzOf(m)) : U(0); }
    static constexpr U make(U f, U m) { return m ? U(U(f << ctzOf(m)) & m) : U(0); }
    static constexpr U set(U v, U f, U m) { return U((v & U(~m)) | make(f, m)); }
    static constexpr U toggle(U v, U m) { return U(v ^ m); }
    static constexpr bool all(U v, U m) { return U(v & m) == m; }
    static constexpr bool any(U v, U m) { return U(v & m) != U(0); }
    static constexpr bool none(U v, U m) { return U(v & m) == U(0); }
    static constexpr U getS(U v, U m) {
        return m ? signExtend(get(v, m), uint8_t(bitWidthOf(m) - ctzOf(m))) : U(0);
    }
};

}  // namespace detail

template <typename T>
inline constexpr T bitfGet(T value, typename detail::Id<T>::type mask) {
    return T(detail::Field<UBits<T>>::get(UBits<T>(value), UBits<T>(mask)));
}

template <typename T>
inline constexpr T bitfGetS(T value, typename detail::Id<T>::type mask) {
    return T(detail::Field<UBits<T>>::getS(UBits<T>(value), UBits<T>(mask)));
}

template <typename T>
inline constexpr T bitfMake(T field, typename detail::Id<T>::type mask) {
    return T(detail::Field<UBits<T>>::make(UBits<T>(field), UBits<T>(mask)));
}

template <typename T>
inline constexpr T bitfSet(T value,
                           typename detail::Id<T>::type field,
                           typename detail::Id<T>::type mask) {
    return T(detail::Field<UBits<T>>::set(UBits<T>(value), UBits<T>(field), UBits<T>(mask)));
}

template <typename T>
inline constexpr T bitfToggle(T value, typename detail::Id<T>::type mask) {
    return T(detail::Field<UBits<T>>::toggle(UBits<T>(value), UBits<T>(mask)));
}

template <typename T>
inline constexpr bool bitfAll(T value, typename detail::Id<T>::type mask) {
    return detail::Field<UBits<T>>::all(UBits<T>(value), UBits<T>(mask));
}

template <typename T>
inline constexpr bool bitfAny(T value, typename detail::Id<T>::type mask) {
    return detail::Field<UBits<T>>::any(UBits<T>(value), UBits<T>(mask));
}

template <typename T>
inline constexpr bool bitfNone(T value, typename detail::Id<T>::type mask) {
    return detail::Field<UBits<T>>::none(UBits<T>(value), UBits<T>(mask));
}

template <typename T>
inline constexpr uint8_t bitfWidth(T mask) {
    return mask ? uint8_t(bitWidthOf(UBits<T>(mask)) - ctzOf(UBits<T>(mask))) : 0;
}

}  // namespace fn

#endif  // F_USE_BIT_OPS

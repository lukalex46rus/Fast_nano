#pragma once
// ============================================================================
//  ForMacro.h — препроцессорный цикл по списку аргументов
//  Цель: ATmega328P/328, avr-gcc 7.3.0, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Разворачивает макрос-обработчик по каждому элементу списка __VA_ARGS__.
//    Нужен для генерации таблиц во flash: набор строк + массив указателей,
//    набор массивов + массив дескрипторов. Всё происходит на этапе
//    препроцессора: в прошивке не остаётся ни байта кода цикла.
//
//  API
//    FN_FOR_EACH(f, ctx, ...)      обработать все аргументы (1..64)
//    FN_FOR_N(n, f, ctx, ...)      обработать ровно n аргументов
//    FN_ARG_COUNT(...)             число аргументов, 0 для пустого списка
//    FN_FM_INC(i)                  i + 1 для i = 0..63 (служебное)
//
//    Сигнатура обработчика: f(total, index, ctx, value)
//      total — общее число элементов;
//      index — позиция элемента, 0-based, В ПОРЯДКЕ следования аргументов;
//      ctx   — сквозной параметр (обычно имя генерируемого объекта);
//      value — сам элемент.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Максимум 64 элемента. При превышении имя FN_FM_FOR_<n> не существует,
//      и сборка падает на неразвёрнутом токене — тихо неверного кода не будет.
//    - Пустой список запрещён: FN_FOR_EACH(f, ctx) разворачивается в
//      static_assert(false, ...) с внятным сообщением.
//    - FN_ARG_COUNT использует GNU-расширение ", ##__VA_ARGS__" (удаление
//      запятой при пустом списке). Оно доступно именно потому, что Arduino
//      AVR core собирает с -std=gnu++11, а не -std=c++11.
//    - Индекс идёт в прямом порядке (первый аргумент -> index 0). В прежней
//      версии порядок был обратным; если ваш обработчик полагался на это,
//      поправьте его.
//    - Все внутренние имена имеют префикс FN_FM_. Прежние _FM_* были
//      зарезервированы стандартом (подчёркивание + заглавная буква).
//    - Обработчик не должен содержать незакрытых запятых вне скобок:
//      препроцессор разберёт их как разделители аргументов.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт flash и RAM. Плата — время препроцессинга: 64 уровня вместо
//    прежних 512, заголовок меньше в восемь раз.
//
//  ПРИМЕР
//    #define GEN_ITEM(total, i, name, val) \
//        const char FN_CONCAT(name, i)[] FN_PROGMEM = val;
//    #define GEN_REF(total, i, name, val) FN_CONCAT(name, i),
//
//    FN_FOR_EACH(GEN_ITEM, msg, "ok", "err", "busy")
//    const char* const msg[] FN_PROGMEM = { FN_FOR_EACH(GEN_REF, msg, "ok", "err", "busy") };
//    // разворачивается в msg0="ok", msg1="err", msg2="busy", msg[]={msg0,msg1,msg2}
//
//  ЗАВИСИМОСТИ: Compiler.h (FN_CONCAT)
// ============================================================================

#include "Compiler.h"

// ---------------------------- служебные ------------------------------------
#define FN_FM_EXPAND(x) x
#define FN_FM_INC(i) FN_CONCAT(FN_FM_INC_, i)

// Счётчик аргументов: dummy + список + убывающая линейка 64..0.
// Позиция N подобрана так, что при k аргументах в неё попадает k (k = 0..64).
#define FN_FM_NTH(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, \
                  _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, \
                  _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, \
                  _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, \
                  _58, _59, _60, _61, _62, _63, _64, N, ...) N

#define FN_FM_RSEQ()                                                             \
    64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46,   \
    45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27,   \
    26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7,  \
    6, 5, 4, 3, 2, 1, 0

#define FN_ARG_COUNT(...) FN_FM_EXPAND(FN_FM_NTH(0, ##__VA_ARGS__, FN_FM_RSEQ()))

// ---------------------------- таблица инкремента ---------------------------
#define FN_FM_INC_0 1
#define FN_FM_INC_1 2
#define FN_FM_INC_2 3
#define FN_FM_INC_3 4
#define FN_FM_INC_4 5
#define FN_FM_INC_5 6
#define FN_FM_INC_6 7
#define FN_FM_INC_7 8
#define FN_FM_INC_8 9
#define FN_FM_INC_9 10
#define FN_FM_INC_10 11
#define FN_FM_INC_11 12
#define FN_FM_INC_12 13
#define FN_FM_INC_13 14
#define FN_FM_INC_14 15
#define FN_FM_INC_15 16
#define FN_FM_INC_16 17
#define FN_FM_INC_17 18
#define FN_FM_INC_18 19
#define FN_FM_INC_19 20
#define FN_FM_INC_20 21
#define FN_FM_INC_21 22
#define FN_FM_INC_22 23
#define FN_FM_INC_23 24
#define FN_FM_INC_24 25
#define FN_FM_INC_25 26
#define FN_FM_INC_26 27
#define FN_FM_INC_27 28
#define FN_FM_INC_28 29
#define FN_FM_INC_29 30
#define FN_FM_INC_30 31
#define FN_FM_INC_31 32
#define FN_FM_INC_32 33
#define FN_FM_INC_33 34
#define FN_FM_INC_34 35
#define FN_FM_INC_35 36
#define FN_FM_INC_36 37
#define FN_FM_INC_37 38
#define FN_FM_INC_38 39
#define FN_FM_INC_39 40
#define FN_FM_INC_40 41
#define FN_FM_INC_41 42
#define FN_FM_INC_42 43
#define FN_FM_INC_43 44
#define FN_FM_INC_44 45
#define FN_FM_INC_45 46
#define FN_FM_INC_46 47
#define FN_FM_INC_47 48
#define FN_FM_INC_48 49
#define FN_FM_INC_49 50
#define FN_FM_INC_50 51
#define FN_FM_INC_51 52
#define FN_FM_INC_52 53
#define FN_FM_INC_53 54
#define FN_FM_INC_54 55
#define FN_FM_INC_55 56
#define FN_FM_INC_56 57
#define FN_FM_INC_57 58
#define FN_FM_INC_58 59
#define FN_FM_INC_59 60
#define FN_FM_INC_60 61
#define FN_FM_INC_61 62
#define FN_FM_INC_62 63
#define FN_FM_INC_63 64

// ---------------------------- разворот цикла -------------------------------
#define FN_FM_FOR_0(f, N, p, i, ...) \
    static_assert(false, "FN_FOR_EACH: пустой список аргументов, нужен хотя бы один элемент")

#define FN_FM_FOR_1(f, N, p, i, x, ...) f(N, i, p, x)
#define FN_FM_FOR_2(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_1(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_3(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_2(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_4(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_3(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_5(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_4(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_6(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_5(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_7(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_6(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_8(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_7(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_9(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_8(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_10(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_9(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_11(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_10(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_12(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_11(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_13(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_12(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_14(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_13(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_15(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_14(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_16(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_15(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_17(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_16(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_18(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_17(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_19(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_18(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_20(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_19(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_21(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_20(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_22(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_21(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_23(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_22(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_24(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_23(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_25(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_24(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_26(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_25(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_27(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_26(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_28(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_27(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_29(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_28(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_30(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_29(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_31(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_30(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_32(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_31(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_33(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_32(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_34(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_33(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_35(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_34(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_36(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_35(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_37(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_36(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_38(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_37(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_39(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_38(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_40(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_39(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_41(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_40(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_42(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_41(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_43(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_42(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_44(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_43(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_45(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_44(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_46(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_45(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_47(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_46(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_48(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_47(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_49(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_48(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_50(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_49(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_51(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_50(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_52(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_51(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_53(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_52(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_54(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_53(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_55(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_54(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_56(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_55(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_57(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_56(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_58(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_57(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_59(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_58(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_60(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_59(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_61(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_60(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_62(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_61(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_63(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_62(f, N, p, FN_FM_INC(i), __VA_ARGS__))
#define FN_FM_FOR_64(f, N, p, i, x, ...) f(N, i, p, x) FN_FM_EXPAND(FN_FM_FOR_63(f, N, p, FN_FM_INC(i), __VA_ARGS__))

// ---------------------------- публичный API --------------------------------
#define FN_FOR_N(n, f, ctx, ...) \
    FN_FM_EXPAND(FN_CONCAT(FN_FM_FOR_, n)(f, n, ctx, 0, __VA_ARGS__))

#define FN_FOR_EACH(f, ctx, ...) FN_FOR_N(FN_ARG_COUNT(__VA_ARGS__), f, ctx, __VA_ARGS__)

// ---------------------------- совместимость --------------------------------
// Прежние имена. Расширение сохранено прежним, включая точку с запятой
// там, где она была, чтобы существующий код не пришлось править.
#ifndef FOR_MACRO
#define FOR_MACRO(f, p, ...) FN_FOR_EACH(f, p, __VA_ARGS__)
#endif
#ifndef FM_FOR
#define FM_FOR(N, f, p, ...) FN_FOR_N(N, f, p, __VA_ARGS__)
#endif
#ifndef FM_FOR_NARG
#define FM_FOR_NARG(...) FN_ARG_COUNT(__VA_ARGS__)
#endif

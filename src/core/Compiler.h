#pragma once
// ============================================================================
//  Compiler.h — атрибуты компилятора, барьеры, секции, препроцессор
//  Цель: ATmega328P/328, avr-gcc 7.3.0, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Тонкие обёртки над GNU-атрибутами и встроенными функциями. Задача —
//    не писать __attribute__ по всему коду и держать все платформенные
//    особенности в одном файле.
//
//  API
//   Inlining и оптимизация
//      FN_INLINE            inline + always_inline (гарантированное встраивание)
//      FN_NOINLINE          запрет встраивания
//      FN_LIKELY(x)         подсказка: условие обычно истинно
//      FN_UNLIKELY(x)       подсказка: условие обычно ложно
//      FN_UNUSED(x)         подавить warning о неиспользуемой переменной
//      FN_UNREACHABLE()     сообщить оптимизатору о недостижимой точке
//      FN_ASSUME(cond)      недостижимость при нарушении условия
//    Память и секции
//      FN_BARRIER()         барьер оптимизатора (не машинная инструкция)
//      FN_PROGMEM           разместить объект во flash
//      FN_PSTR(s)           строковый литерал во flash (PGM_P)
//      FN_SECTION(name)     положить объект в заданную секцию
//      FN_NOINIT            .noinit: переживает сброс, не обнуляется crt0
//      FN_EEMEM             секция EEPROM
//      FN_USED              не выбрасывать объект, даже если ссылок нет
//      FN_WEAK              слабое определение (переопределяемое пользователем)
//      FN_PACKED            структура без выравнивания
//      FN_ALIGNED(n)        выравнивание на n байт
//      FN_NAKED             функция без пролога/эпилога (только asm)
//    Диагностика
//      FN_DEPRECATED        пометить устаревшим
//      FN_TODO(msg)         #warning с текстом (не ошибка сборки)
//      FN_PRINTF(f, a)      проверка формата printf-подобной функции
//    Препроцессор
//      FN_CONCAT(a, b)      склейка токенов с предварительным раскрытием
//      FN_STR(x)            строка из раскрытого значения макроса
//      FN_ARRAY_SIZE(a)     число элементов C-массива (constexpr)
//      FN_OFFSET_OF(t, m)   смещение поля в байтах
//      FN_BYTES_TO_WORDS(n) ceil(n/2), для расчёта размеров буферов
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - FN_INLINE при -Os иногда УВЕЛИЧИВАЕТ flash: принудительное встраивание
//      крупной функции в нескольких местах хуже одного вызова. Ставить только
//      на короткие (1-5 инструкций) функции.
//    - FN_BARRIER() запрещает компилятору переставлять обращения к памяти,
//      но не генерирует инструкций. Для volatile-регистров не нужен.
//    - FN_NOINIT: переменная НЕ обнуляется при старте и содержит мусор после
//      подачи питания. Обязательны сигнатура и контрольная сумма.
//    - FN_NAKED: тело только на ассемблере, локальные переменные и вызовы
//      функций недопустимы.
//    - FN_ARRAY_SIZE работает только с массивами, не с указателями: для
//      указателя static_assert внутри упадёт на этапе компиляции.
//
//  ПРИМЕР
//    FN_NOINIT static uint8_t bootCount;         // переживает сброс
//    static const char msg[] FN_PROGMEM = "OK";  // строка во flash
//    Uart::print(FN_PSTR("start "));             // литерал во flash
//    #define PIN_NAME(i) FN_CONCAT(LED_, i)      // LED_0, LED_1, ...
//    FN_BARRIER();                               // граница переупорядочивания
//
//  ЗАВИСИМОСТИ: Config.h, stdint.h, stddef.h, avr/pgmspace.h
// ============================================================================

#include "Config.h"

#include <stdint.h>
#include <stddef.h>
#include <avr/pgmspace.h>

// ------------------------- встраивание и оптимизация -----------------------
#define FN_INLINE inline __attribute__((always_inline))
#define FN_NOINLINE __attribute__((noinline))
#define FN_LIKELY(x) __builtin_expect(!!(x), 1)
#define FN_UNLIKELY(x) __builtin_expect(!!(x), 0)
#define FN_UNUSED(x) (void)(x)
#define FN_UNREACHABLE() __builtin_unreachable()
#define FN_ASSUME(cond) \
    do { if (!(cond)) __builtin_unreachable(); } while (0)

// ------------------------------- память и секции ---------------------------
#define FN_BARRIER() __asm__ __volatile__("" ::: "memory")
#define FN_PROGMEM PROGMEM
#define FN_PSTR(s) PSTR(s)
#define FN_SECTION(name) __attribute__((section(name)))
#define FN_NOINIT __attribute__((section(".noinit")))
#define FN_EEMEM EEMEM
#define FN_USED __attribute__((used))
#define FN_WEAK __attribute__((weak))
#define FN_PACKED __attribute__((packed))
#define FN_ALIGNED(n) __attribute__((aligned(n)))
#define FN_NAKED __attribute__((naked))

// ------------------------------- диагностика -------------------------------
#define FN_DEPRECATED __attribute__((deprecated))
#define FN_TODO(msg) _Pragma(#msg)
#define FN_PRINTF(fmtIdx, argIdx) __attribute__((format(printf, fmtIdx, argIdx)))

// ------------------------------- препроцессор ------------------------------
#define FN_CONCAT_IMPL(a, b) a##b
#define FN_CONCAT(a, b) FN_CONCAT_IMPL(a, b)
#define FN_STR_IMPL(x) #x
#define FN_STR(x) FN_STR_IMPL(x)

#define FN_OFFSET_OF(t, m) offsetof(t, m)
#define FN_BYTES_TO_WORDS(n) (((n) + 1u) / 2u)

namespace fn {

// Число элементов C-массива. Для указателя не скомпилируется — это намеренно.
template <typename T, size_t N>
inline constexpr size_t arraySize(const T (&)[N]) {
    return N;
}
}  // namespace fn

#define FN_ARRAY_SIZE(a) (::fn::arraySize(a))

#pragma once
// ============================================================================
//  Progmem.h — данные во flash: чтение, строки, массивы, списки, макросы
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственный модуль работы с PROGMEM (бывшие Progmem.h и PgmUtils.h
//    слиты: две параллельные реализации строк и массивов больше не нужны).
//    На AVR обычный указатель на данные во flash разыменовывать нельзя —
//    только pgm_read_* / memcpy_P. Модуль прячет это за типами.
//
//  API — чтение (namespace fn)
//    pgmRead(p)          типизированное чтение: uint8_t/int8_t/char/
//                        uint16_t/int16_t/uint32_t/int32_t/float и
//                        универсальный шаблон для структур и union
//    pgmReadBytes(dst, src, n)   memcpy_P с проверкой nullptr
//    pgmReadPtr(p)       указатель из таблицы указателей во flash
//
//  API — контейнеры (namespace fn::pgm)
//    PgmString                     строка во flash, 2 байта
//      constexpr PgmString() / PgmString(PGM_P)
//      valid(), ptr(), length()
//      at(i), operator[](i)        с проверкой границ -> '\0'
//      read(i)                     без проверки, для горячих циклов
//      indexOf(c, from = 0) -> int16_t (-1 если не найдено)
//      equals(ram) / equalsP(flash) / startsWith(ram) / startsWithP(flash)
//      compare(ram) -> int8_t      знак как у strcmp
//      copyTo(dst, size) -> uint16_t   strlcpy_P, возвращает длину строки
//      explicit operator PGM_P()
//
//    Array<T>                      массив во flash, 4 байта (ptr + len)
//      constexpr Array() / Array(ptr, n)
//      valid(), ptr(), size()
//      at(i), operator[](i)        с проверкой границ -> T{}
//      read(i)                     без проверки
//      copyTo(dst, maxCount) -> uint16_t   число скопированных элементов
//
//    ArrayDesc<T>                  дескриптор {ptr, len} для списков, 4 байта
//    ArrayList<T>                  список массивов, 4 байта
//      size(), at(i) -> Array<T>   ДЛИНА СОХРАНЯЕТСЯ (в старой версии терялась)
//    StringList                    список строк, 4 байта
//      size(), at(i) -> PgmString
//
//    makeArray(a)                  Array<T> из C-массива, длина выводится
//    arrayAt(a, i)                 чтение C-массива во flash без объекта
//
//  API — печать (namespace fn::pgm, работает с любым Print-подобным типом)
//    print(out, PGM_P) / print(out, PgmString)
//    println(out, PGM_P) / println(out, PgmString)
//    FN_FPSTR(p)                   приведение к const __FlashStringHelper*
//
//  МАКРОСЫ (F_USE_PGM_UTILS)
//    FN_PGM_VALUE(T, name, val)        одиночное значение
//    FN_PGM_STRUCT(T, name, ...)       структура/класс с инициализатором
//    FN_PGM_STRING(name, "text")       строка
//    FN_PGM_ARRAY(T, name, ...)        массив
//    FN_PGM_STRING_LIST(name, ...)     строки + таблица указателей name
//    FN_PGM_STRING_LIST_OBJ(name, ...) то же + объект StringList name
//    FN_PGM_ARRAY_OBJ(T, name, ...)    массив + объект Array<T> name
//    FN_PGM_ARRAY_DESC(T, name, arr)   дескриптор для списка массивов
//    FN_PGM_ARRAY_LIST(T, name, ...)   таблица дескрипторов
//    FN_PGM_ARRAY_LIST_OBJ(T, name, ...) то же + объект ArrayList<T>
//    Макросы НЕ заканчиваются точкой с запятой — ставьте её сами.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Все преобразования explicit. В прежней версии два неявных оператора
//      (PGM_P и __FlashStringHelper*) давали ambiguous-перегрузку на
//      Serial.print(). Теперь печатайте fn::pgm::print(Serial, s) либо
//      Serial.print(FN_FPSTR(s.ptr())).
//    - Наследования от Printable нет: оно стоило vptr в каждом объекте
//      (2 байта) плюс vtable в RAM. Печать — свободные функции.
//    - Конструкторы constexpr: глобальные объекты инициализируются статически,
//      без вызовов из __do_global_ctors.
//    - PgmString::length() каждый раз вызывает strlen_P — O(n). Кэша нет
//      намеренно: он удвоил бы размер объекта и убрал constexpr.
//      В горячих циклах берите read(i) с внешним счётчиком или copyTo().
//    - at()/operator[] у строки проверяют границу через length(), то есть
//      тоже O(n). Для последовательного обхода — read(i) или copyTo().
//    - Небезопасного copyTo без размера больше нет. Копирование всегда
//      ограничено размером приёмника и завершается нулём.
//    - copyTo использует strlcpy_P, а не strncpy_P: последний добивал буфер
//      нулями до конца (для строки из 5 символов в буфере 64 байта — 63 записи).
//    - Макросы порождают const-объекты: internal linkage, в каждом TU своя
//      копия. Предназначены для одного .cpp/.ino, не для заголовков.
//    - Список строк/массивов ограничен 64 элементами (лимит ForMacro.h).
//    - __FlashStringHelper объявлен предварительно, Arduino.h не требуется:
//      ядро остаётся на чистом avr-libc.
//
//  СТОИМОСТЬ (AVR)
//    PgmString — 2 байта, Array/ArrayList/StringList — 4 байта на объект.
//    Сами данные во flash, в SRAM не копируются. Неиспользуемые методы
//    не инстанцируются.
//
//  ПРИМЕР
//    FN_PGM_STRING_LIST_OBJ(menu, "Run", "Stop", "Setup");
//    FN_PGM_ARRAY_OBJ(uint8_t, modes, 1, 2, 4, 8);
//
//    void setup() {
//        Serial.begin(9600);
//        for (uint16_t i = 0; i < menu.size(); ++i)
//            fn::pgm::println(Serial, menu[i]);
//        uint8_t m = modes[2];                 // 4
//    }
//
//    char buf[16];
//    menu[0].copyTo(buf, sizeof(buf));         // "Run", гарантированный '\0'
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, ForMacro.h, avr/pgmspace.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "ForMacro.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>

// Предварительное объявление вместо Arduino.h: совместимо с определением
// из WString.h, ядро остаётся независимым от Arduino.
class __FlashStringHelper;

#ifndef FN_FPSTR
#define FN_FPSTR(p) (reinterpret_cast<const __FlashStringHelper*>(p))
#endif

#if F_USE_PROGMEM

namespace fn {

// ============================ типизированное чтение ========================
inline uint8_t  pgmRead(const uint8_t* p)  { return pgm_read_byte(p); }
inline int8_t   pgmRead(const int8_t* p)   { return int8_t(pgm_read_byte(p)); }
inline char     pgmRead(const char* p)     { return char(pgm_read_byte(p)); }
inline uint16_t pgmRead(const uint16_t* p) { return pgm_read_word(p); }
inline int16_t  pgmRead(const int16_t* p)  { return int16_t(pgm_read_word(p)); }
inline uint32_t pgmRead(const uint32_t* p) { return pgm_read_dword(p); }
inline int32_t  pgmRead(const int32_t* p)  { return int32_t(pgm_read_dword(p)); }
inline float    pgmRead(const float* p)    { return pgm_read_float(p); }

// Универсальный вариант: структуры, union, 64-битные типы.
template <typename T>
inline T pgmRead(const T* p) {
    T v;
    memcpy_P(&v, p, sizeof(T));
    return v;
}

inline void pgmReadBytes(void* dst, const void* src, uint16_t n) {
    if (dst == nullptr || src == nullptr) return;
    memcpy_P(dst, src, n);
}

inline const void* pgmReadPtr(const void* const* p) {
    return reinterpret_cast<const void*>(pgm_read_word(p));
}

namespace pgm {

// ============================ строка во flash ==============================
class PgmString {
public:
    constexpr PgmString() : _s(nullptr) {}
    constexpr PgmString(PGM_P s) : _s(s) {}

    constexpr bool valid() const { return _s != nullptr; }
    constexpr PGM_P ptr() const { return _s; }
    explicit constexpr operator PGM_P() const { return _s; }

    uint16_t length() const { return _s ? uint16_t(strlen_P(_s)) : 0; }

    // Без проверки: для горятых циклов, когда индекс заведомо корректен.
    char read(uint16_t i) const { return char(pgm_read_byte(_s + i)); }

    char at(uint16_t i) const {
        if (_s == nullptr || i >= length()) return '\0';
        return read(i);
    }
    char operator[](uint16_t i) const { return at(i); }

    int16_t indexOf(char c, uint16_t from = 0) const {
        if (_s == nullptr) return -1;
        const uint16_t n = length();
        for (uint16_t i = from; i < n; ++i) {
            if (read(i) == c) return int16_t(i);
        }
        return -1;
    }

    bool equals(const char* ram) const {
        if (_s == nullptr || ram == nullptr) return false;
        return strcmp_P(ram, _s) == 0;
    }

    bool equalsP(PGM_P other) const {
        if (_s == nullptr || other == nullptr) return false;
        uint16_t i = 0;
        for (;;) {
            const char a = read(i);
            const char b = char(pgm_read_byte(other + i));
            if (a != b) return false;
            if (a == '\0') return true;
            ++i;
        }
    }

    bool startsWith(const char* ram) const {
        if (_s == nullptr || ram == nullptr) return false;
        uint16_t i = 0;
        while (ram[i] != '\0') {
            if (read(i) != ram[i]) return false;
            ++i;
        }
        return true;
    }

    bool startsWithP(PGM_P other) const {
        if (_s == nullptr || other == nullptr) return false;
        uint16_t i = 0;
        for (;;) {
            const char b = char(pgm_read_byte(other + i));
            if (b == '\0') return true;
            if (read(i) != b) return false;
            ++i;
        }
    }

    // Знак как у strcmp: <0, 0, >0. Нормализовано в int8_t.
    int8_t compare(const char* ram) const {
        if (_s == nullptr || ram == nullptr) return 0;
        const int r = strcmp_P(ram, _s);
        return int8_t((r < 0) ? -1 : ((r > 0) ? 1 : 0));
    }

    // Копирует не более size-1 символов, всегда завершает нулём.
    // Возвращает длину исходной строки (как strlcpy): если >= size, была обрезка.
    uint16_t copyTo(char* dst, uint16_t size) const {
        if (dst == nullptr || size == 0) return 0;
        if (_s == nullptr) { dst[0] = '\0'; return 0; }
        return uint16_t(strlcpy_P(dst, _s, size));
    }

private:
    PGM_P _s;
};

// ============================ массив во flash ==============================
template <typename T>
class Array {
public:
    constexpr Array() : _p(nullptr), _n(0) {}
    constexpr Array(const T* p, uint16_t n) : _p(p), _n(n) {}

    constexpr bool valid() const { return _p != nullptr; }
    constexpr const T* ptr() const { return _p; }
    constexpr uint16_t size() const { return _n; }

    T read(uint16_t i) const { return pgmRead(_p + i); }

    T at(uint16_t i) const {
        if (_p == nullptr || i >= _n) return T{};
        return read(i);
    }
    T operator[](uint16_t i) const { return at(i); }

    uint16_t copyTo(T* dst, uint16_t maxCount) const {
        if (dst == nullptr || _p == nullptr) return 0;
        const uint16_t n = (maxCount < _n) ? maxCount : _n;
        memcpy_P(dst, _p, size_t(n) * sizeof(T));
        return n;
    }

private:
    const T* _p;
    uint16_t _n;
};

// Дескриптор элемента списка массивов: хранится во flash, 4 байта.
template <typename T>
struct ArrayDesc {
    const T* p;
    uint16_t n;
};

// ============================ список массивов ==============================
template <typename T>
class ArrayList {
public:
    constexpr ArrayList() : _d(nullptr), _n(0) {}
    constexpr ArrayList(const ArrayDesc<T>* d, uint16_t n) : _d(d), _n(n) {}

    constexpr bool valid() const { return _d != nullptr; }
    constexpr uint16_t size() const { return _n; }

    // Длина элемента сохраняется — в отличие от прежней реализации.
    Array<T> at(uint16_t i) const {
        if (_d == nullptr || i >= _n) return Array<T>();
        const ArrayDesc<T> d = pgmRead(_d + i);
        return Array<T>(d.p, d.n);
    }
    Array<T> operator[](uint16_t i) const { return at(i); }

private:
    const ArrayDesc<T>* _d;
    uint16_t _n;
};

// ============================ список строк =================================
class StringList {
public:
    constexpr StringList() : _a(nullptr), _n(0) {}
    constexpr StringList(const char* const* a, uint16_t n) : _a(a), _n(n) {}

    constexpr bool valid() const { return _a != nullptr; }
    constexpr uint16_t size() const { return _n; }

    PgmString at(uint16_t i) const {
        if (_a == nullptr || i >= _n) return PgmString();
        return PgmString(reinterpret_cast<PGM_P>(pgm_read_word(_a + i)));
    }
    PgmString operator[](uint16_t i) const { return at(i); }

private:
    const char* const* _a;
    uint16_t _n;
};

// ============================ helpers без объектов =========================
template <typename T, uint16_t N>
inline constexpr Array<T> makeArray(const T (&a)[N]) {
    return Array<T>(a, N);
}

template <typename T, uint16_t N>
inline T arrayAt(const T (&a)[N], uint16_t i) {
    return (i < N) ? pgmRead(&a[i]) : T{};
}

// ============================ печать =======================================
// Работает с любым типом, у которого есть print(const __FlashStringHelper*).
// Arduino.h не нужен: тип объявлен предварительно выше.
template <typename Printer>
inline size_t print(Printer& out, PGM_P s) {
    return (s != nullptr) ? out.print(FN_FPSTR(s)) : 0;
}

template <typename Printer>
inline size_t print(Printer& out, const PgmString& s) {
    return print(out, s.ptr());
}

template <typename Printer>
inline size_t println(Printer& out, PGM_P s) {
    const size_t n = print(out, s);
    return n + out.println();
}

template <typename Printer>
inline size_t println(Printer& out, const PgmString& s) {
    return println(out, s.ptr());
}

}  // namespace pgm

// ============================ совместимость ================================
using ProgmemString = pgm::PgmString;

// Прежний ProgmemArray<T,N>: длина в шаблоне, объект 2 байта.
template <typename T, uint16_t N>
class ProgmemArray : public pgm::Array<T> {
public:
    explicit constexpr ProgmemArray(const T* p) : pgm::Array<T>(p, N) {}
};

}  // namespace fn

// ============================ макросы ======================================
#if F_USE_PGM_UTILS

#define FN_PGM_VALUE(T, name, val) const T name FN_PROGMEM = val
#define FN_PGM_STRUCT(T, name, ...) const T name FN_PROGMEM = { __VA_ARGS__ }
#define FN_PGM_STRING(name, s) const char name[] FN_PROGMEM = s
#define FN_PGM_ARRAY(T, name, ...) const T name[] FN_PROGMEM = { __VA_ARGS__ }

// Генераторы для FN_FOR_EACH: f(total, index, ctx, value)
#define FN_PGM_GEN_STR(total, i, name, s) \
    const char FN_CONCAT(name, i)[] FN_PROGMEM = s;
#define FN_PGM_GEN_REF(total, i, name, s) FN_CONCAT(name, i),

#define FN_PGM_STRING_LIST(name, ...)                       \
    FN_FOR_EACH(FN_PGM_GEN_STR, name, __VA_ARGS__)          \
    const char* const name[] FN_PROGMEM = { FN_FOR_EACH(FN_PGM_GEN_REF, name, __VA_ARGS__) }

#define FN_PGM_STRING_LIST_OBJ(name, ...)                   \
    FN_PGM_STRING_LIST(FN_CONCAT(name, _list), __VA_ARGS__) \
    constexpr ::fn::pgm::StringList name(FN_CONCAT(name, _list), \
                                         FN_ARRAY_SIZE(FN_CONCAT(name, _list)))

#define FN_PGM_ARRAY_OBJ(T, name, ...)                      \
    FN_PGM_ARRAY(T, FN_CONCAT(name, _arr), __VA_ARGS__)     \
    constexpr ::fn::pgm::Array<T> name(FN_CONCAT(name, _arr), \
                                       FN_ARRAY_SIZE(FN_CONCAT(name, _arr)))

#define FN_PGM_ARRAY_DESC(T, name, arr) { arr, FN_ARRAY_SIZE(arr) }

#define FN_PGM_ARRAY_LIST(T, name, ...) \
    const ::fn::pgm::ArrayDesc<T> name[] FN_PROGMEM = { __VA_ARGS__ }

#define FN_PGM_ARRAY_LIST_OBJ(T, name, ...)                 \
    FN_PGM_ARRAY_LIST(T, FN_CONCAT(name, _list), __VA_ARGS__) \
    constexpr ::fn::pgm::ArrayList<T> name(FN_CONCAT(name, _list), \
                                           FN_ARRAY_SIZE(FN_CONCAT(name, _list)))

// ------------------------- совместимость (legacy) --------------------------
// Прежние имена сохранены вместе с точкой с запятой внутри макроса, чтобы
// существующий код продолжал собираться. Новый код пишите на FN_PGM_*.
#ifndef PGM_VAL
#define PGM_VAL(T, name, val) FN_PGM_VALUE(T, name, val);
#endif
#ifndef PGM_STRUCT
#define PGM_STRUCT(T, name, ...) FN_PGM_STRUCT(T, name, __VA_ARGS__);
#endif
#ifndef PGM_STR
#define PGM_STR(name, s) FN_PGM_STRING(name, s);
#endif
#ifndef PGM_ARRAY
#define PGM_ARRAY(T, name, ...) FN_PGM_ARRAY(T, name, __VA_ARGS__);
#endif
#ifndef PGM_STR_LIST
#define PGM_STR_LIST(name, ...) FN_PGM_STRING_LIST(name, __VA_ARGS__);
#endif
#ifndef PGM_STR_LIST_OBJ
#define PGM_STR_LIST_OBJ(name, ...) FN_PGM_STRING_LIST_OBJ(name, __VA_ARGS__);
#endif
#ifndef PGM_ARRAY_OBJ
#define PGM_ARRAY_OBJ(T, name, ...) FN_PGM_ARRAY_OBJ(T, name, __VA_ARGS__);
#endif
#ifndef MAKE_STR_LIST
#define MAKE_STR_LIST(name) \
    ::fn::pgm::StringList(name, FN_ARRAY_SIZE(name));
#endif
#ifndef MAKE_ARRAY
#define MAKE_ARRAY(T, name) \
    ::fn::pgm::Array<T>(name, FN_ARRAY_SIZE(name));
#endif
#ifndef FSTR
#define FSTR const __FlashStringHelper*
#endif
#ifndef FPSTR
#define FPSTR(x) FN_FPSTR(x)
#endif

#endif  // F_USE_PGM_UTILS

#endif  // F_USE_PROGMEM

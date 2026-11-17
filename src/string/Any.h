#pragma once
// ============================================================================
//  Any.h — типизированная ссылка на значение без динамической памяти
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    «Любая ссылка»: тип + указатель + размер. Скаляры до 32 бит хранятся
//    ВНУТРИ объекта, строки и большие числа — по указателю. Позволяет писать
//    обобщённый код (таблицы параметров, меню, конфигураторы) без шаблонов
//    на каждый тип и без virtual.
//
//  КОНФИГУРАЦИЯ
//    FN_ANY_64BIT   0 (по умолчанию) — вычисления в int32_t/uint32_t.
//                   Нет toInt64/toUint64, нет конструкторов long long*,
//                   приёмники размером 8 байт отвергаются.
//                   Экономия ~0.8..1.1 КБ flash: не тянутся __floatdisf,
//                   __floatundisf, __fixsfdi, __fixunssfdi, __divdi3.
//                 1 — полный API с 64-битными значениями по указателю.
//    FN_ANY_PRINT   1 — printTo(Print&); требует Arduino.h.
//    F_USE_STRINGN  — интеграция со StringN<N>.
//
//  ТИПЫ (AnyType, порядок стабилен)
//    None, Bool, Int, Uint, Float, Str, CStr, FStr, Bin
//
//  API
//    Конструкторы по значению: bool, char, signed/unsigned char, short,
//      unsigned short, int, unsigned int, long, unsigned long, float, double.
//      Значение копируется внутрь объекта.
//    Конструкторы по указателю: те же типы + (при FN_ANY_64BIT) long long*.
//      Ссылка на внешнюю переменную, чтение и запись на месте.
//    Строки: Any(const char*), Any(const char*, len), Any(char*, cap, len),
//      Any(char (&arr)[N]), Any(const __FlashStringHelper*).
//    Фабрики: Any::str / cstr / fstr / bin.
//    Состояние: valid(), operator bool, type(), size(), data(),
//      isStr/isInt/isFloat/isNumeric.
//    Конвертация: writeTo(Any&) / writeToSame(Any&) / readFrom / readFromSame.
//    Разбор: readFromStr(const char*) — десятичное целое или float.
//    Экспорт: c_str(), charAt(i), copyTo(dst, capacity).
//    Числа: toInt() -> int32_t, toUint() -> uint32_t, toFloat() -> float;
//      при FN_ANY_64BIT дополнительно toInt64()/toUint64().
//    Печать: printTo(Print&) при FN_ANY_PRINT.
//    StringN: toString(StringN<N>&, dec).
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - sizeof(Any) == 9 байт: указатель 2 + union 4 + size 2 + тип 1.
//      Зафиксировано static_assert.
//    - Any НЕ владеет буферами. Объект, на который ссылается Any, должен
//      жить дольше самого Any. Строковый литерал безопасен (статическое
//      хранилище), локальный массив — нет.
//    - Копирование запрещено: указатель на внутренний скаляр стал бы висячим.
//      Перемещение разрешено и перепривязывает указатель на свой union.
//    - Any(char) — это CStr длины 1, а не число. Завершающий ноль лежит
//      во втором байте union (_storage.c[1]); прежняя версия хранила один
//      символ без нуля, и c_str() уходил за границу.
//    - Сужающая запись проверяет диапазон приёмника: 1000 в uint8_t вернёт
//      false, а не запишет 232. Прежняя версия проверяла только против
//      ±2^31 и молча усекала до размера приёмника.
//    - Float -> Int отвергает NaN и выход за диапазон приёмника.
//    - При FN_ANY_64BIT = 0 приёмник размером 8 байт отвергается всегда;
//      строковый разбор ограничен uint32 (как и в прежней версии).
//    - Str-приёмник всегда получает завершающий ноль; writeTo возвращает
//      true только при полной копии (без обрезки).
//    - Разбор строк: десятичный, без экспоненты и суффиксов. Лишний символ
//      после числа -> false.
//    - printTo зависит от Arduino.h; включается флагом, а не #ifdef ARDUINO,
//      чтобы конфигурация была одинаковой во всех единицах трансляции.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 9 байт на объект. Flash: ~0.7..0.9 КБ при FN_ANY_64BIT = 0,
//    ~1.5..2 КБ при = 1. Неиспользуемые методы вырезаются линкером.
//
//  ПРИМЕР
//    uint8_t mode = 3;
//    fn::Any a(&mode);
//    a.readFromStr("42");            // mode == 42
//    a.readFromStr("300");           // false, mode не изменён
//
//    char buf[16] = "hi";
//    fn::Any s(buf, sizeof(buf), 2);
//    fn::Any(FN_PSTR(" world")).writeTo(s);   // buf == "hi world"? нет:
//                                             // writeTo перезаписывает с начала
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, string.h, avr/pgmspace.h;
//               опционально Arduino.h (FN_ANY_PRINT), StringN.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <avr/pgmspace.h>

#if F_USE_ANY

#ifndef FN_ANY_64BIT
#define FN_ANY_64BIT 0
#endif

#ifndef FN_ANY_PRINT
#define FN_ANY_PRINT 1
#endif

#if FN_ANY_PRINT
#include <Arduino.h>
#endif

#if F_USE_STRINGN
#include "StringN.h"
#endif

namespace fn {

enum class AnyType : uint8_t {
    None = 0, Bool, Int, Uint, Float, Str, CStr, FStr, Bin
};

class Any {
public:
    Any() = default;
    ~Any() = default;

    Any(const Any&) = delete;
    Any& operator=(const Any&) = delete;
    Any(Any&& other);
    Any& operator=(Any&& other);

    Any(decltype(nullptr)) : _p(nullptr), _size(0), _type(AnyType::None) {}
    Any(AnyType type, const void* p, uint16_t size = 0)
        : _p(p), _size(size), _type(type) {}

    // ---- скаляры по значению ----
    Any(bool v) : Any(AnyType::Bool, &_storage.b, uint16_t(sizeof(bool))) { _storage.b = v; }

    // Символ — строка из одного символа, с завершающим нулём в c[1].
    Any(char v) : Any(AnyType::CStr, _storage.c, 1) { _storage.c[0] = v; _storage.c[1] = '\0'; }

    Any(signed char v) : Any(AnyType::Int, &_storage.sc, uint16_t(sizeof(signed char))) { _storage.sc = v; }
    Any(unsigned char v) : Any(AnyType::Uint, &_storage.uc, uint16_t(sizeof(unsigned char))) { _storage.uc = v; }
    Any(short v) : Any(AnyType::Int, &_storage.s, uint16_t(sizeof(short))) { _storage.s = v; }
    Any(unsigned short v) : Any(AnyType::Uint, &_storage.us, uint16_t(sizeof(unsigned short))) { _storage.us = v; }
    Any(int v) : Any(AnyType::Int, &_storage.i, uint16_t(sizeof(int))) { _storage.i = v; }
    Any(unsigned int v) : Any(AnyType::Uint, &_storage.ui, uint16_t(sizeof(unsigned int))) { _storage.ui = v; }
    Any(long v) : Any(AnyType::Int, &_storage.l, uint16_t(sizeof(long))) { _storage.l = v; }
    Any(unsigned long v) : Any(AnyType::Uint, &_storage.ul, uint16_t(sizeof(unsigned long))) { _storage.ul = v; }
    Any(float v) : Any(AnyType::Float, &_storage.f, uint16_t(sizeof(float))) { _storage.f = v; }
    Any(double v) : Any(float(v)) {}

    // ---- ссылки на числа ----
    Any(bool* p) : Any(AnyType::Bool, p, uint16_t(sizeof(bool))) {}
    Any(signed char* p) : Any(AnyType::Int, p, uint16_t(sizeof(signed char))) {}
    Any(unsigned char* p) : Any(AnyType::Uint, p, uint16_t(sizeof(unsigned char))) {}
    Any(short* p) : Any(AnyType::Int, p, uint16_t(sizeof(short))) {}
    Any(unsigned short* p) : Any(AnyType::Uint, p, uint16_t(sizeof(unsigned short))) {}
    Any(int* p) : Any(AnyType::Int, p, uint16_t(sizeof(int))) {}
    Any(unsigned int* p) : Any(AnyType::Uint, p, uint16_t(sizeof(unsigned int))) {}
    Any(long* p) : Any(AnyType::Int, p, uint16_t(sizeof(long))) {}
    Any(unsigned long* p) : Any(AnyType::Uint, p, uint16_t(sizeof(unsigned long))) {}
    Any(float* p) : Any(AnyType::Float, p, uint16_t(sizeof(float))) {}

#if FN_ANY_64BIT
    Any(long long* p) : Any(AnyType::Int, p, uint16_t(sizeof(long long))) {}
    Any(unsigned long long* p) : Any(AnyType::Uint, p, uint16_t(sizeof(unsigned long long))) {}
#endif

    // ---- строки ----
    Any(const char* p) : _p(p), _size(0), _type(AnyType::None) {
        if (!p) return;
        _size = uint16_t(strlen(p));
        _type = AnyType::CStr;
    }
    Any(const char* p, uint16_t len) : Any(AnyType::CStr, p, len) {}

    Any(char* p, size_t capacity, uint16_t len = 0)
        : _p(nullptr), _size(0), _type(AnyType::None) {
        if (!p || capacity == 0) return;
        const uint16_t cap = uint16_t(capacity);
        if (len > uint16_t(cap - 1)) len = uint16_t(cap - 1);
        p[len] = '\0';
        _p = p;
        _storage.capacity = cap;
        _size = len;
        _type = AnyType::Str;
    }

    template <size_t N>
    Any(char (&arr)[N]) : Any(arr, N, strnlenLocal(arr, N)) {}

#if FN_ANY_PRINT
    Any(const __FlashStringHelper* p)
        : Any(AnyType::FStr, p,
              p ? uint16_t(strlen_P(reinterpret_cast<PGM_P>(p))) : uint16_t(0)) {}
#endif

    // ---- фабрики ----
    static Any str(char* p, size_t capacity, uint16_t len = 0) { return Any(p, capacity, len); }
    static Any cstr(const char* p, uint16_t len) { return Any(p, len); }
    static Any fstr(PGM_P p, uint16_t len = 0) {
        if (!p) return Any();
        return Any(AnyType::FStr, p, len ? len : uint16_t(strlen_P(p)));
    }
    static Any bin(void* p, uint16_t size) { return Any(AnyType::Bin, p, size); }
    template <typename T> static Any bin(T* p) { return Any(AnyType::Bin, p, uint16_t(sizeof(T))); }

#if F_USE_STRINGN
    template <uint16_t N>
    Any(StringN<N>* p) : _p(nullptr), _size(0), _type(AnyType::None) {
        if (!p) return;
        _p = p->c_str();
        _storage.capacity = uint16_t(p->capacity()) + 1u;
        _size = p->length();
        _type = AnyType::Str;
    }
    template <uint16_t N>
    Any(const StringN<N>& s) : Any(AnyType::CStr, s.c_str(), s.length()) {}
#endif

    // ---- состояние ----
    explicit operator bool() const { return valid(); }
    bool valid() const { return _p != nullptr && _type != AnyType::None; }
    AnyType type() const { return _type; }
    uint16_t size() const { return _size; }
    const void* data() const { return _p; }

    bool isStr() const {
        return _type == AnyType::Str || _type == AnyType::CStr || _type == AnyType::FStr;
    }
    bool isInt() const { return _type == AnyType::Int || _type == AnyType::Uint; }
    bool isFloat() const { return _type == AnyType::Float; }
    bool isNumeric() const {
        return _type == AnyType::Bool || isInt() || isFloat();
    }

    // ---- конвертация ----
    bool writeTo(Any& dst) const;
    bool writeTo(Any&& dst) const { return writeTo(dst); }
    bool writeToSame(Any& dst) const;
    bool writeToSame(Any&& dst) const { return writeToSame(dst); }
    bool readFrom(const Any& src) { return src.writeTo(*this); }
    bool readFromSame(const Any& src) { return src.writeToSame(*this); }
    bool readFromStr(const char* s);

    // ---- экспорт ----
    const char* c_str() const {
        if (_type == AnyType::Str || _type == AnyType::CStr) {
            return _p ? static_cast<const char*>(_p) : "";
        }
        return "";
    }

    char charAt(uint16_t i) const {
        if (!_p || i >= _size) return '\0';
        if (_type == AnyType::FStr) {
            return char(pgm_read_byte(static_cast<const char*>(_p) + i));
        }
        if (_type == AnyType::Str || _type == AnyType::CStr) {
            return static_cast<const char*>(_p)[i];
        }
        return '\0';
    }

    bool copyTo(char* dst, uint16_t capacity) const {
        if (!dst || capacity == 0 || !_p || !isStr()) return false;
        uint16_t wrote = _size;
        if (wrote >= capacity) wrote = uint16_t(capacity - 1);
        if (wrote) {
            if (_type == AnyType::FStr) memcpy_P(dst, _p, wrote);
            else memmove(dst, _p, wrote);
        }
        dst[wrote] = '\0';
        return wrote == _size;
    }

    // ---- числа ----
    int32_t toInt() const;
    uint32_t toUint() const;
    float toFloat() const;

#if FN_ANY_64BIT
    int64_t toInt64() const;
    uint64_t toUint64() const;
#endif

#if FN_ANY_PRINT
    void printTo(Print& p) const;
#endif

#if F_USE_STRINGN
    template <uint16_t N>
    bool toString(StringN<N>& out, uint8_t dec = 2) const;
#endif

private:
    bool inlinePtr(const void* p) const {
        if (!p) return false;
        const uint8_t* base = reinterpret_cast<const uint8_t*>(&_storage);
        const uint8_t* q = static_cast<const uint8_t*>(p);
        return q >= base && q < base + sizeof(_storage);
    }

    void invalidate() { _p = nullptr; _size = 0; _type = AnyType::None; }
    void* ptr() const { return const_cast<void*>(_p); }

    template <typename T> T read() const {
        T v = T();
        if (_p) memcpy(&v, _p, sizeof(T));
        return v;
    }

    static uint16_t strnlenLocal(const char* s, size_t maxLen) {
        uint16_t n = 0;
        while (size_t(n) < maxLen && s[n] != '\0') ++n;
        return n;
    }

    // Запись с проверкой диапазона приёмника. size = 1/2/4 (и 8 при 64BIT).
    static bool writeSigned(int32_t v, uint16_t size, void* dst);
    static bool writeUnsigned(uint32_t v, uint16_t size, void* dst);
    static bool fitsSigned(int32_t v, uint16_t size);
    static bool fitsUnsigned(uint32_t v, uint16_t size);
    static bool writeFloat(float v, void* dst);
    static bool copyStr(const void* src, uint16_t len, bool pgm, Any& dst);
    static bool parseInteger(const char* s, int32_t& value);
    static bool parseFloat(const char* s, float& value);

    const void* _p = nullptr;
    union Storage {
        uint16_t capacity;
        bool b;
        char c[2];              // символ + завершающий ноль
        signed char sc;
        unsigned char uc;
        short s;
        unsigned short us;
        int i;
        unsigned int ui;
        long l;
        unsigned long ul;
        float f;
        Storage() : ul(0) {}    // весь union обнулён
    } _storage;
    uint16_t _size = 0;
    AnyType _type = AnyType::None;
};

static_assert(sizeof(Any) == 9, "fn::Any: ожидалось 9 байт (выравнивание на AVR = 1)");

// ============================ перемещение ==================================
inline Any::Any(Any&& other) : _p(other._p), _size(other._size), _type(other._type) {
    if (other.inlinePtr(other._p)) {
        const uint8_t* ob = reinterpret_cast<const uint8_t*>(&other._storage);
        const uint8_t* src = static_cast<const uint8_t*>(other._p);
        memcpy(&_storage, &other._storage, sizeof(_storage));
        _p = reinterpret_cast<const uint8_t*>(&_storage) + (src - ob);
    } else {
        _storage.capacity = other._storage.capacity;
    }
    other.invalidate();
}

inline Any& Any::operator=(Any&& other) {
    if (this != &other) {
        _p = other._p;
        _size = other._size;
        _type = other._type;
        if (other.inlinePtr(other._p)) {
            const uint8_t* ob = reinterpret_cast<const uint8_t*>(&other._storage);
            const uint8_t* src = static_cast<const uint8_t*>(other._p);
            memcpy(&_storage, &other._storage, sizeof(_storage));
            _p = reinterpret_cast<const uint8_t*>(&_storage) + (src - ob);
        } else {
            _storage.capacity = other._storage.capacity;
        }
        other.invalidate();
    }
    return *this;
}

// ============================ диапазоны ====================================
inline bool Any::fitsSigned(int32_t v, uint16_t size) {
    switch (size) {
        case 1: return v >= -128 && v <= 127;
        case 2: return v >= -32768 && v <= 32767;
        case 4: return true;
        default: return false;      // 8 байт при FN_ANY_64BIT = 0 недоступны
    }
}

inline bool Any::fitsUnsigned(uint32_t v, uint16_t size) {
    switch (size) {
        case 1: return v <= 255u;
        case 2: return v <= 65535u;
        case 4: return true;
        default: return false;
    }
}

inline bool Any::writeSigned(int32_t v, uint16_t size, void* dst) {
    if (!dst || !fitsSigned(v, size)) return false;
    switch (size) {
        case 1: *static_cast<int8_t*>(dst) = int8_t(v); return true;
        case 2: *static_cast<int16_t*>(dst) = int16_t(v); return true;
        case 4: *static_cast<int32_t*>(dst) = v; return true;
        default: return false;
    }
}

inline bool Any::writeUnsigned(uint32_t v, uint16_t size, void* dst) {
    if (!dst || !fitsUnsigned(v, size)) return false;
    switch (size) {
        case 1: *static_cast<uint8_t*>(dst) = uint8_t(v); return true;
        case 2: *static_cast<uint16_t*>(dst) = uint16_t(v); return true;
        case 4: *static_cast<uint32_t*>(dst) = v; return true;
        default: return false;
    }
}

inline bool Any::writeFloat(float v, void* dst) {
    if (!dst) return false;
    *static_cast<float*>(dst) = v;
    return true;
}

inline bool Any::copyStr(const void* src, uint16_t len, bool pgm, Any& dst) {
    if (!src || !dst._p || dst._type != AnyType::Str) return false;
    const uint16_t cap = dst._storage.capacity;
    if (cap == 0) return false;
    char* out = static_cast<char*>(dst.ptr());
    uint16_t wrote = len;
    if (wrote >= cap) wrote = uint16_t(cap - 1);
    if (wrote) {
        if (pgm) memcpy_P(out, src, wrote);
        else memmove(out, src, wrote);
    }
    out[wrote] = '\0';
    dst._size = wrote;
    return wrote == len;
}

// ============================ разбор =======================================
inline bool Any::parseInteger(const char* s, int32_t& value) {
    if (!s) return false;
    bool neg = false;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); ++s; }
    if (*s < '0' || *s > '9') return false;

    uint32_t acc = 0;
    const uint32_t limit = neg ? 2147483648UL : 2147483647UL;
    while (*s >= '0' && *s <= '9') {
        const uint8_t d = uint8_t(*s - '0');
        if (acc > (limit - d) / 10u) return false;
        acc = acc * 10u + d;
        ++s;
    }
    if (*s != '\0') return false;
    value = neg ? int32_t(0u - acc) : int32_t(acc);
    return true;
}

inline bool Any::parseFloat(const char* s, float& value) {
    if (!s) return false;
    bool neg = false;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); ++s; }

    uint32_t whole = 0;
    bool digits = false;
    while (*s >= '0' && *s <= '9') {
        const uint8_t d = uint8_t(*s - '0');
        if (whole > (0xFFFFFFFFu - d) / 10u) return false;
        whole = whole * 10u + d;
        ++s;
        digits = true;
    }
    float v = float(whole);

    if (*s == '.') {
        ++s;
        uint32_t frac = 0;
        uint8_t fd = 0;
        while (*s >= '0' && *s <= '9') {
            if (fd < 9) {
                const uint8_t d = uint8_t(*s - '0');
                if (frac > (0xFFFFFFFFu - d) / 10u) return false;
                frac = frac * 10u + d;
                ++fd;
            }
            ++s;
            digits = true;
        }
        float mul = 1.0f;
        for (uint8_t i = 0; i < fd; ++i) mul *= 0.1f;
        v += float(frac) * mul;
    }
    if (!digits || *s != '\0') return false;
    value = neg ? -v : v;
    return true;
}

// ============================ числа ========================================
inline int32_t Any::toInt() const {
    if (!_p) return 0;
    switch (_type) {
        case AnyType::Bool: return read<bool>() ? 1 : 0;
        case AnyType::Int:
            switch (_size) {
                case 1: return int32_t(read<int8_t>());
                case 2: return int32_t(read<int16_t>());
                case 4: return read<int32_t>();
#if FN_ANY_64BIT
                case 8: return int32_t(read<int64_t>());
#endif
                default: return 0;
            }
        case AnyType::Uint:
            switch (_size) {
                case 1: return int32_t(read<uint8_t>());
                case 2: return int32_t(read<uint16_t>());
                case 4: return int32_t(read<uint32_t>());
#if FN_ANY_64BIT
                case 8: return int32_t(read<uint64_t>());
#endif
                default: return 0;
            }
        case AnyType::Float: {
            const float f = read<float>();
            if (!(f > -2147483648.0f && f < 2147483648.0f)) return 0;
            return int32_t(f);
        }
        default: return 0;
    }
}

inline uint32_t Any::toUint() const {
    if (!_p) return 0;
    switch (_type) {
        case AnyType::Bool: return read<bool>() ? 1u : 0u;
        case AnyType::Int:  return uint32_t(toInt());
        case AnyType::Uint:
            switch (_size) {
                case 1: return uint32_t(read<uint8_t>());
                case 2: return uint32_t(read<uint16_t>());
                case 4: return read<uint32_t>();
#if FN_ANY_64BIT
                case 8: return uint32_t(read<uint64_t>());
#endif
                default: return 0;
            }
        case AnyType::Float: {
            const float f = read<float>();
            if (!(f >= 0.0f && f < 4294967296.0f)) return 0;
            return uint32_t(f);
        }
        default: return 0;
    }
}

inline float Any::toFloat() const {
    if (!_p) return 0.0f;
    switch (_type) {
        case AnyType::Bool: return read<bool>() ? 1.0f : 0.0f;
        case AnyType::Int:  return float(toInt());
        case AnyType::Uint: return float(toUint());
        case AnyType::Float: return read<float>();
        default: return 0.0f;
    }
}

#if FN_ANY_64BIT
inline int64_t Any::toInt64() const {
    if (!_p) return 0;
    if (_type == AnyType::Int && _size == 8) return read<int64_t>();
    if (_type == AnyType::Uint && _size == 8) return int64_t(read<uint64_t>());
    return int64_t(toInt());
}

inline uint64_t Any::toUint64() const {
    if (!_p) return 0;
    if (_type == AnyType::Uint && _size == 8) return read<uint64_t>();
    if (_type == AnyType::Int && _size == 8) return uint64_t(read<int64_t>());
    return uint64_t(toUint());
}
#endif

// ============================ конвертация ==================================
inline bool Any::writeTo(Any& dst) const {
    if (!_p || !dst._p) return false;
    if (_type == AnyType::None || dst._type == AnyType::None) return false;

    switch (_type) {
        case AnyType::Str:
        case AnyType::CStr:
            return dst._type == AnyType::Str ? copyStr(_p, _size, false, dst) : false;
        case AnyType::FStr:
            return dst._type == AnyType::Str ? copyStr(_p, _size, true, dst) : false;

        case AnyType::Bool: {
            const bool v = read<bool>();
            if (dst._type == AnyType::Bool) { *static_cast<bool*>(dst.ptr()) = v; return true; }
            if (dst._type == AnyType::Float) return writeFloat(v ? 1.0f : 0.0f, dst.ptr());
            if (dst.isInt()) return writeSigned(v ? 1 : 0, dst._size, dst.ptr());
            return false;
        }

        case AnyType::Int: {
            const int32_t v = toInt();
            if (dst._type == AnyType::Bool) { *static_cast<bool*>(dst.ptr()) = (v != 0); return true; }
            if (dst._type == AnyType::Float) return writeFloat(float(v), dst.ptr());
            if (dst._type == AnyType::Int) return writeSigned(v, dst._size, dst.ptr());
            if (dst._type == AnyType::Uint) {
                if (v < 0) return false;
                return writeUnsigned(uint32_t(v), dst._size, dst.ptr());
            }
            return false;
        }

        case AnyType::Uint: {
            const uint32_t v = toUint();
            if (dst._type == AnyType::Bool) { *static_cast<bool*>(dst.ptr()) = (v != 0); return true; }
            if (dst._type == AnyType::Float) return writeFloat(float(v), dst.ptr());
            if (dst._type == AnyType::Uint) return writeUnsigned(v, dst._size, dst.ptr());
            if (dst._type == AnyType::Int) {
                if (v > 2147483647UL) return false;
                return writeSigned(int32_t(v), dst._size, dst.ptr());
            }
            return false;
        }

        case AnyType::Float: {
            const float v = read<float>();
            if (v != v) return false;                       // NaN
            if (dst._type == AnyType::Float) return writeFloat(v, dst.ptr());
            if (dst._type == AnyType::Int) {
                if (!(v > -2147483648.0f && v < 2147483648.0f)) return false;
                return writeSigned(int32_t(v), dst._size, dst.ptr());
            }
            if (dst._type == AnyType::Uint) {
                if (!(v >= 0.0f && v < 4294967296.0f)) return false;
                return writeUnsigned(uint32_t(v), dst._size, dst.ptr());
            }
            return false;
        }

        case AnyType::Bin:
            if (dst._type != AnyType::Bin || dst._size != _size) return false;
            memcpy(dst.ptr(), _p, _size);
            return true;

        default: return false;
    }
}

inline bool Any::writeToSame(Any& dst) const {
    if (!_p || !dst._p) return false;
    if (_type == AnyType::None || dst._type == AnyType::None) return false;

    switch (_type) {
        case AnyType::Str:
        case AnyType::CStr:
            return dst._type == AnyType::Str ? copyStr(_p, _size, false, dst) : false;
        case AnyType::Int:
        case AnyType::Uint:
            return dst.isInt() ? writeTo(dst) : false;
        case AnyType::Bool:
            if (dst._type != AnyType::Bool) return false;
            *static_cast<bool*>(dst.ptr()) = read<bool>();
            return true;
        case AnyType::Float:
            if (dst._type != AnyType::Float) return false;
            memcpy(dst.ptr(), _p, sizeof(float));
            return true;
        case AnyType::Bin:
            if (dst._type != AnyType::Bin || dst._size != _size) return false;
            memcpy(dst.ptr(), _p, _size);
            return true;
        default: return false;
    }
}

inline bool Any::readFromStr(const char* s) {
    if (!s || !_p) return false;
    if (isInt()) {
        int32_t v;
        if (!parseInteger(s, v)) return false;
        if (_type == AnyType::Uint) {
            if (v < 0) return false;
            return writeUnsigned(uint32_t(v), _size, ptr());
        }
        return writeSigned(v, _size, ptr());
    }
    if (_type == AnyType::Float) {
        float v;
        if (!parseFloat(s, v)) return false;
        return writeFloat(v, ptr());
    }
    return false;
}

// ============================ печать =======================================
#if FN_ANY_PRINT
inline void Any::printTo(Print& p) const {
    if (!_p) return;
    switch (_type) {
        case AnyType::Str:
        case AnyType::CStr:
            p.write(static_cast<const char*>(_p), _size);
            break;
        case AnyType::FStr:
            p.print(reinterpret_cast<const __FlashStringHelper*>(_p));
            break;
        case AnyType::Bool:  p.print(read<bool>() ? 1 : 0); break;
        case AnyType::Float: p.print(toFloat()); break;
        case AnyType::Int:   p.print(toInt()); break;
        case AnyType::Uint:  p.print(toUint()); break;
        case AnyType::Bin:
            for (uint16_t i = 0; i < _size; ++i) {
                if (i) p.print(' ');
                const uint8_t b = static_cast<const uint8_t*>(_p)[i];
                if (b < 0x10) p.print('0');
                p.print(b, HEX);
            }
            break;
        default: break;
    }
}
#endif

#if F_USE_STRINGN
template <uint16_t N>
inline bool Any::toString(StringN<N>& out, uint8_t dec) const {
    out.clear();
    if (!_p) return false;
    switch (_type) {
        case AnyType::Str:
        case AnyType::CStr:
            out.add(static_cast<const char*>(_p), int16_t(_size));
            return true;
        case AnyType::FStr:
            for (uint16_t i = 0; i < _size; ++i) out.add(charAt(i));
            return true;
        case AnyType::Bool:  out.add(read<bool>()); return true;
        case AnyType::Int:   out.add(toInt()); return true;
        case AnyType::Uint:  out.add(toUint()); return true;
        case AnyType::Float: out.add(toFloat(), int(dec)); return true;
        case AnyType::Bin:
            for (uint16_t i = 0; i < _size; ++i) {
                const uint8_t b = static_cast<const uint8_t*>(_p)[i];
                if (b < 0x10) out.add('0');
                out.add(b, 16);
            }
            return true;
        default: return false;
    }
}
#endif

}  // namespace fn

#endif  // F_USE_ANY
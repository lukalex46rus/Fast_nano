// ============================================================================
//  StringBuilder.cpp — сборка строк в готовый буфер
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Низкоуровневые append-функции для StringN и любого char[]. Без Arduino
//    String, без динамической памяти. Все функции пишут завершающий ноль
//    и возвращают число записанных символов (без нуля).
//
//  КОНТРАКТ left
//    left >= 0 — сколько символов ещё влезает в буфер (без нуля).
//    left < 0  — «без ограничения»: ответственность за размер на вызывающем.
//    Возврат всегда <= left при left >= 0. Буфер терминируется всегда,
//    даже если ничего не записано.
//
//  ИСПРАВЛЕННЫЕ ДЕФЕКТЫ ПРЕЖНЕЙ ВЕРСИИ
//    - fdiv10::div восстанавливал quot*10 как (quot<<1)+(qq&~7), что не
//      равно quot*10: для num=100 остаток уходил в отрицательное по модулю
//      256. Часть случаев маскировала коррекция rem>9, но не все.
//      Заменено на v/10 с константой: GCC сводит к умножению на 0xCCCCCCCD
//      со сдвигом, ~10 инструкций, без libgcc.
//    - appendUnsigned писал цифры сразу в buf и реверсил на месте, но
//      останавливался по --left, теряя старшие разряды при обрезке:
//      для 12345 при left=3 получалось "543" -> "345" вместо "123".
//      Теперь цифры собираются во временный массив и обрезаются справа.
//    - appendFloat при обрезке целой части всё равно печатал дробную:
//      "1234.56" при left=3 давало "123." вместо "123".
//    - appendRepeat при left < 0 не ограничивал amount и писал за границу,
//      если вызывающий передал amount больше буфера. Теперь left < 0
//      означает доверие вызывающему, но amount <= 0 обрабатывается явно.
//    - appendChar/appendRepeat писали buf[0]=0 при left==0 без проверки
//      указателя. Добавлены guard'ы на nullptr.
//    - ESP8266/ESP32-ветки удалены: библиотека только ATmega328P/328.
//
//  ОГРАНИЧЕНИЯ
//    - appendUnsigned поддерживает base 10/16/2. Прочие основания -> 0.
//    - appendSigned для base != 10 печатает беззнаковое представление
//      (как printf %x): знак не выводится.
//    - appendUnsigned64/appendSigned64 тянут __udivdi3 (~400 Б flash).
//      Используйте только если действительно нужны 64-битные значения.
//    - appendFloat: dec ограничен 6 (точность float ~7 значащих цифр).
//      Отрицательный ноль печатается как "0.00", без минуса.
//    - Временный буфер цифр — 10 байт на стеке (максимум uint32 в dec).
//    - Файл должен лежать в src/string/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: StringBuilder.h, avr/pgmspace.h, string.h, math.h
// ============================================================================

#include "StringBuilder.h"

#include <limits.h>
#include <math.h>
#include <string.h>
#include <avr/pgmspace.h>

namespace fn {
namespace strings {
namespace {

// Сколько символов реально можно записать. left < 0 -> без ограничения.
inline int16_t clampLen(int16_t want, int16_t left) {
    if (left < 0) return want;
    return (want > left) ? left : want;
}

// Цифры числа в обратном порядке во временный буфер.
// Возвращает количество цифр. tmp должен вмещать 10 (dec) / 32 (bin) байт.
inline uint8_t digitsReversed(uint32_t v, uint8_t base, char* tmp) {
    uint8_t n = 0;
    if (base == 10) {
        do {
            const uint32_t q = v / 10u;          // константа -> mul+shift
            tmp[n++] = char('0' + (v - q * 10u));
            v = q;
        } while (v);
    } else {
        const uint8_t shift = (base == 16) ? 4u : 1u;
        do {
            const uint8_t c = uint8_t(v & (base - 1u));
            tmp[n++] = char(c < 10 ? ('0' + c) : ('a' + c - 10));
            v >>= shift;
        } while (v);
    }
    return n;
}

// Переписать цифры из обратного буфера в прямой, с обрезкой СПРАВА.
inline uint8_t emitReversed(const char* tmp, uint8_t n, char* buf, int16_t left) {
    uint8_t wrote = 0;
    const int16_t lim = (left < 0) ? int16_t(n) : left;
    while (n && wrote < uint8_t(lim)) buf[wrote++] = tmp[--n];
    buf[wrote] = '\0';
    return wrote;
}

}  // namespace

// ============================ символы ======================================
uint16_t appendChar(char sym, char* buf, int16_t left) {
    if (!buf) return 0;
    if (left == 0) { buf[0] = '\0'; return 0; }
    buf[0] = sym;
    buf[1] = '\0';
    return 1;
}

uint16_t appendRepeat(char sym, int16_t amount, char* buf, int16_t left) {
    if (!buf) return 0;
    if (amount <= 0) { buf[0] = '\0'; return 0; }
    const int16_t n = clampLen(amount, left);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    memset(buf, sym, size_t(n));
    buf[n] = '\0';
    return uint16_t(n);
}

// ============================ строки =======================================
uint16_t append(const char* str, int16_t len, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!str || len <= 0) { buf[0] = '\0'; return 0; }
    const int16_t n = clampLen(len, left);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    memcpy(buf, str, size_t(n));
    buf[n] = '\0';
    return uint16_t(n);
}

uint16_t append(const char* str, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!str) { buf[0] = '\0'; return 0; }
    size_t len = strlen(str);
    if (len > size_t(INT16_MAX)) len = size_t(INT16_MAX);
    return append(str, int16_t(len), buf, left);
}

uint16_t appendProgmem(const void* pstr, int16_t len, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!pstr || len <= 0) { buf[0] = '\0'; return 0; }
    const int16_t n = clampLen(len, left);
    if (n <= 0) { buf[0] = '\0'; return 0; }
    memcpy_P(buf, pstr, size_t(n));
    buf[n] = '\0';
    return uint16_t(n);
}

uint16_t appendProgmem(const void* pstr, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!pstr) { buf[0] = '\0'; return 0; }
    size_t len = strlen_P(static_cast<PGM_P>(pstr));
    if (len > size_t(INT16_MAX)) len = size_t(INT16_MAX);
    return appendProgmem(pstr, int16_t(len), buf, left);
}

// ============================ целые ========================================
uint8_t appendUnsigned(uint32_t v, uint8_t base, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!(base == 10 || base == 16 || base == 2)) { buf[0] = '\0'; return 0; }
    if (left == 0) { buf[0] = '\0'; return 0; }

    char tmp[32];                       // максимум 32 цифры в binary
    const uint8_t n = digitsReversed(v, base, tmp);
    return emitReversed(tmp, n, buf, left);
}

uint8_t appendSigned(int32_t v, uint8_t base, char* buf, int16_t left) {
    if (!buf) return 0;
    if (v < 0 && base == 10) {
        if (left == 0) { buf[0] = '\0'; return 0; }
        buf[0] = '-';
        // -INT32_MIN представим в uint32 без UB.
        const uint32_t mag = uint32_t(0u - uint32_t(v));
        const int16_t rest = (left < 0) ? int16_t(-1) : int16_t(left - 1);
        return uint8_t(1 + appendUnsigned(mag, base, buf + 1, rest));
    }
    return appendUnsigned(uint32_t(v), base, buf, left);
}

uint8_t appendUnsigned64(uint64_t v, uint8_t base, char* buf, int16_t left) {
    if (!buf) return 0;
    if (!(base == 10 || base == 16 || base == 2)) { buf[0] = '\0'; return 0; }
    if (left == 0) { buf[0] = '\0'; return 0; }
    if (v <= 0xFFFFFFFFULL) return appendUnsigned(uint32_t(v), base, buf, left);

    char tmp[64];
    uint8_t n = 0;
    if (base == 10) {
        do {
            const uint64_t q = v / 10ULL;
            tmp[n++] = char('0' + uint8_t(v - q * 10ULL));
            v = q;
        } while (v);
    } else {
        const uint8_t shift = (base == 16) ? 4u : 1u;
        do {
            const uint8_t c = uint8_t(v & (base - 1u));
            tmp[n++] = char(c < 10 ? ('0' + c) : ('a' + c - 10));
            v >>= shift;
        } while (v);
    }
    return emitReversed(tmp, n, buf, left);
}

uint8_t appendSigned64(int64_t v, uint8_t base, char* buf, int16_t left) {
    if (!buf) return 0;
    if (v < 0 && base == 10) {
        if (left == 0) { buf[0] = '\0'; return 0; }
        buf[0] = '-';
        const uint64_t mag = uint64_t(0ULL - uint64_t(v));
        const int16_t rest = (left < 0) ? int16_t(-1) : int16_t(left - 1);
        return uint8_t(1 + appendUnsigned64(mag, base, buf + 1, rest));
    }
    return appendUnsigned64(uint64_t(v), base, buf, left);
}

// ============================ float ========================================
uint8_t appendFloat(float v, uint8_t dec, char* buf, int16_t left) {
    if (!buf) return 0;
    if (isnan(v)) return uint8_t(append("nan", 3, buf, left));
    if (isinf(v)) return uint8_t(append(v < 0 ? "-inf" : "inf", v < 0 ? 4 : 3, buf, left));
    if (left == 0) { buf[0] = '\0'; return 0; }
    if (dec > 6) dec = 6;                 // точность float ~7 значащих цифр

    char* p = buf;
    int16_t rest = left;

    if (v < 0.0f) {
        *p++ = '-';
        if (rest > 0) --rest;
        v = -v;
    }

    // Округление к ближайшему по последнему выводимому знаку.
    float rounding = 0.5f;
    for (uint8_t i = 0; i < dec; ++i) rounding *= 0.1f;
    v += rounding;

    // Целая часть. Если она не влезла целиком — дробную не печатаем.
    const uint32_t whole = uint32_t(v);
    char tmp[10];
    const uint8_t nd = digitsReversed(whole, 10, tmp);
    const uint8_t wroteInt = emitReversed(tmp, nd, p, rest);
    p += wroteInt;
    if (rest > 0) rest = int16_t(rest - wroteInt);

    if (wroteInt < nd) {                  // целая часть обрезана
        *p = '\0';
        return uint8_t(p - buf);
    }
    if (dec == 0) {
        *p = '\0';
        return uint8_t(p - buf);
    }
    if (rest == 0) {                      // места на точку нет
        *p = '\0';
        return uint8_t(p - buf);
    }

    *p++ = '.';
    if (rest > 0) --rest;

    float frac = v - float(whole);
    uint8_t emitted = 0;
    while (rest > 0 && emitted < dec) {
        frac *= 10.0f;
        uint8_t d = uint8_t(frac);
        if (d > 9) d = 9;                 // защита от накопления ошибки
        *p++ = char('0' + d);
        frac -= float(d);
        --rest;
        ++emitted;
    }
    *p = '\0';
    return uint8_t(p - buf);
}

}  // namespace strings
}  // namespace fn
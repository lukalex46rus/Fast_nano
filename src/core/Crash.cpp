// ============================================================================
//  Crash.cpp — .noinit-запись, захват MCUSR в .init3, останов и сброс
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    1. Захватить MCUSR и выключить WDT до инициализации .data/.bss (.init3).
//    2. Проверить запись в .noinit: счётчик загрузок, признак аварии.
//    3. Реализовать halt() и forceReset().
//
//  ПОЧЕМУ .init3, А НЕ КОНСТРУКТОР
//    Секции .initN выполняются crt0 до main() и до конструкторов глобальных
//    объектов. Порядок конструкторов между единицами трансляции не
//    гарантирован, поэтому init_priority не решает задачу. После watchdog-
//    сброса WDT остаётся включён с минимальным таймаутом: если не выключить
//    его здесь, программа уйдёт в цикл перезагрузок раньше setup().
//
//  ПОЧЕМУ АССЕМБЛЕРНЫЙ СТАБ
//    Функция в .init3 объявлена naked: без пролога/эпилога. Она делает
//    единственную операцию — call обычной C++-функции. Стек к этому моменту
//    уже инициализирован (.init2), __zero_reg__ обнулён, поэтому вызов
//    безопасен. Объявление fnCrashStartup ОБЯЗАНО предшествовать asm:
//    иначе компилятор не знает символ на момент генерации call.
//
//  ПОЧЕМУ СМЕЩЕНИЯ ПОЛЕЙ НЕ ХАРДКОДЯТСЯ
//    Прежняя версия писала MCUSR по смещению +4, а это поле code, не
//    resetFlags (офсет 3). Причина сброса терялась. Теперь все поля пишет
//    C++-функция, смещения берёт компилятор из структуры.
//
//  ОГРАНИЧЕНИЯ
//    - fnCrashStartup трогает ТОЛЬКО .noinit и регистры: .data/.bss ещё не
//      готовы. Никаких обращений к глобальным переменным с инициализатором.
//    - Запись объявлена extern "C", чтобы имя символа не было искажено.
//    - Контрольная сумма — байтовая сумма первых 11 байт: достаточно против
//      мусора после подачи питания, криптографической стойкости нет.
//    - Файл должен лежать в src/core/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Crash.h, avr/io.h, avr/wdt.h, util/atomic.h
// ============================================================================

#include "Config.h"
#include "Crash.h"

#include <avr/io.h>
#include <avr/wdt.h>
#include <util/atomic.h>

namespace fn {
namespace crash {
namespace detail {

constexpr uint8_t RecordMagic = 0xC7u;

struct Record {
    uint8_t  magic;       // 0
    uint16_t boots;       // 1..2
    uint8_t  resetFlags;  // 3
    uint8_t  code;        // 4
    uint16_t line;        // 5..6
    PGM_P    expr;        // 7..8
    PGM_P    file;        // 9..10
    uint8_t  sum;         // 11
};

static_assert(sizeof(Record) == 12,
              "fn::crash::Record: ожидалось 12 байт (выравнивание на AVR = 1)");

inline uint8_t checksum(const Record& r) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&r);
    uint8_t s = 0;
    for (uint8_t i = 0; i < uint8_t(sizeof(Record) - 1u); ++i) {
        s = uint8_t(s + p[i]);
    }
    return s;
}

}  // namespace detail

namespace {
Policy g_policy = Policy::Halt;
}  // namespace

}  // namespace crash
}  // namespace fn

// Запись переживает сброс. used — чтобы линкер не выбросил символ.
extern "C" {
FN_NOINIT FN_USED fn::crash::detail::Record fnCrashRecord;
}

// ---------------------------------------------------------------------------
//  Объявление ДО asm-стаба: иначе символ неизвестен при генерации call.
// ---------------------------------------------------------------------------
extern "C" void fnCrashStartup(void) __attribute__((noinline, used));

extern "C" void fnCrashEarlyInit(void) __attribute__((naked, used, section(".init3")));
extern "C" void fnCrashEarlyInit(void) {
    __asm__ __volatile__("call fnCrashStartup" ::: "memory");
}

extern "C" void fnCrashStartup(void) {
    using namespace fn::crash;
    using namespace fn::crash::detail;

    const uint8_t mcusr = MCUSR;
    MCUSR = 0;
    wdt_disable();                 // иначе boot loop после watchdog-сброса

    Record& r = fnCrashRecord;
    r.resetFlags = mcusr;          // смещение берёт компилятор

    const bool valid = (r.magic == RecordMagic) && (r.sum == checksum(r));
    if (!valid) {
        r.magic = RecordMagic;
        r.boots = 1;
        r.code = uint8_t(Code::None);
        r.line = 0;
    } else {
        r.boots = uint16_t(r.boots + 1u);
    }
    // Указатели на строки прошлой прошивки недостоверны.
    r.expr = nullptr;
    r.file = nullptr;
    r.sum = checksum(r);
}

namespace fn {
namespace crash {

// ---------------------------------------------------------------------------
//  Публичный API
// ---------------------------------------------------------------------------
uint16_t boots() { return fnCrashRecord.boots; }
uint8_t resetFlags() { return fnCrashRecord.resetFlags; }

void clearResetFlags() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        fnCrashRecord.resetFlags = 0;
        fnCrashRecord.sum = detail::checksum(fnCrashRecord);
    }
}

bool crashed() { return fnCrashRecord.code != uint8_t(Code::None); }
Code code() { return static_cast<Code>(fnCrashRecord.code); }
uint16_t line() { return fnCrashRecord.line; }
PGM_P expr() { return fnCrashRecord.expr; }
PGM_P file() { return fnCrashRecord.file; }

void clear() {
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        fnCrashRecord.code = uint8_t(Code::None);
        fnCrashRecord.line = 0;
        fnCrashRecord.expr = nullptr;
        fnCrashRecord.file = nullptr;
        fnCrashRecord.sum = detail::checksum(fnCrashRecord);
    }
}

void setPolicy(Policy p) { g_policy = p; }
Policy policy() { return g_policy; }

void halt() {
    cli();
    wdt_disable();
    for (;;) { FN_BARRIER(); }
}

void forceReset() {
    cli();
    wdt_enable(WDTO_15MS);
    for (;;) { FN_BARRIER(); }
}

void record(Code c, PGM_P e, PGM_P f, uint16_t ln, Policy p) {
    cli();
    detail::Record& r = fnCrashRecord;
    r.magic = detail::RecordMagic;
    r.code = uint8_t(c);
    r.line = ln;
    r.expr = e;
    r.file = f;
    r.sum = detail::checksum(r);

    if (p == Policy::Reset) forceReset();
    halt();
}

}  // namespace crash
}  // namespace fn
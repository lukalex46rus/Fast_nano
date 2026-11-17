#pragma once
// ============================================================================
//  Assert.h — проверки предусловий без затрат SRAM
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Ассерты для отладки и «страховочные» проверки для эксплуатации. Текст
//    условия и имя файла размещаются во flash через PSTR, поэтому проверка
//    не занимает ни байта SRAM — это ключевое отличие от наивной реализации,
//    где #cond и __FILE__ уезжали в .data.
//
//  API
//    FN_ASSERT(cond)             проверка условия
//    FN_ASSERT_MSG(cond, msg)    то же, но с готовым текстом во flash
//    FN_VERIFY(expr)             выражение ВЫЧИСЛЯЕТСЯ ВСЕГДА, даже в релизе;
//                                проверяется только результат
//    FN_STATIC_ASSERT(cond, msg) обёртка над static_assert
//
//    fn::AssertHandler           void(*)(PGM_P expr, PGM_P file, uint16_t line)
//    fn::setAssertHandler(h)     свой обработчик (лог, мигание, EEPROM)
//    fn::assertHandler()         текущий обработчик
//    fn::setAssertPolicy(p)      реакция, если обработчика нет
//    fn::assertPolicy()
//
//  ПОВЕДЕНИЕ ПРИ СРЫВЕ
//    1. Если задан обработчик — вызывается он. Ассерты передают ему указатели
//       во flash; печатать через FPSTR/strcpy_P, не разыменовывать напрямую.
//       Обработатель может вернуться — тогда выполнение продолжается. Это
//       осознанная лазейка для «мягких» проверок; хотите останов — вызывайте
//       fn::crash::halt() из обработчика.
//    2. Иначе — fn::crash::record(...): причина пишется в .noinit, затем
//       выполняется политика: Halt (останов с выключенным WDT) или
//       Reset (перезапуск через WDT 15 мс).
//    По умолчанию Halt — удобно отлаживать. Для эксплуатации ставьте
//    fn::setAssertPolicy(fn::crash::Policy::Reset) в setup().
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - F_USE_ASSERT = 0 (релиз): FN_ASSERT и FN_ASSERT_MSG исчезают целиком,
//      вместе с вычислением условия. Побочные эффекты в них недопустимы:
//      FN_ASSERT(init() == Ok) в релизе не вызовет init(). Для таких случаев
//      есть FN_VERIFY — он вычисляет выражение всегда.
//    - FN_ASSERT_MSG принимает строковый литерал, а не переменную: внутри
//      макроса применяется PSTR.
//    - __FILE__ в Arduino — полный путь. Одинаковые литералы склеиваются
//      -fmerge-constants при -Os, но каждый уникальный путь занимает место
//      во flash. При жёстком лимите передавайте короткий тег через
//      FN_ASSERT_MSG.
//    - Вызов из ISR допустим: запись идёт с запрещёнными прерываниями,
//      обработчик должен быть коротким и не блокирующим.
//    - При активном WDT политика Halt выключает сторожевой таймер. Если
//      programmed фьюз WDTON — выключить нельзя, будет перезапуск.
//
//  СТОИМОСТЬ (AVR)
//    Один ассерт: вычисление условия + переход + вызов (≈10-20 байт flash).
//    Строки — во flash, SRAM не расходуется. В релизе — ноль.
//
//  ПРИМЕР
//    #include "core/Assert.h"
//
//    void setup() {
//        fn::setAssertPolicy(fn::crash::Policy::Reset);
//        fn::setAssertHandler([](PGM_P e, PGM_P f, uint16_t l) {
//            Uart::print(FPSTR(f)); Uart::print(':'); Uart::print(l);
//            Uart::print(F(" "));   Uart::println(FPSTR(e));
//            fn::crash::halt();
//        });
//    }
//
//    void write(uint8_t* buf, uint16_t n) {
//        FN_ASSERT(buf != nullptr);
//        FN_ASSERT_MSG(n <= 64, "packet too long");
//        FN_VERIFY(Uart::send(buf, n) == fn::Result::Ok);  // работает и в релизе
//    }
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, Crash.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "Crash.h"

#include <stdint.h>
#include <avr/pgmspace.h>

#if F_USE_ASSERT

namespace fn {

using AssertHandler = void (*)(PGM_P expr, PGM_P file, uint16_t line);

namespace detail {
inline AssertHandler& assertHandlerRef() {
    static AssertHandler h = nullptr;  // 2 байта RAM, -fno-threadsafe-statics
    return h;
}
}  // namespace detail

inline void setAssertHandler(AssertHandler h) { detail::assertHandlerRef() = h; }
inline AssertHandler assertHandler() { return detail::assertHandlerRef(); }

inline void setAssertPolicy(crash::Policy p) { crash::setPolicy(p); }
inline crash::Policy assertPolicy() { return crash::policy(); }

// Не inline в смысле раскрытия: тело одно на программу, места вызова
// остаются короткими. inline обязателен — заголовок включается в много TU.
inline FN_NOINLINE void assertFailed(PGM_P expr, PGM_P file, uint16_t line, crash::Code code) {
    AssertHandler h = detail::assertHandlerRef();
    if (h != nullptr) {
        h(expr, file, line);
        return;
    }
    crash::record(code, expr, file, line, crash::policy());
}

}  // namespace fn

#ifndef FN_ASSERT
#define FN_ASSERT(cond)                                                     \
    do {                                                                    \
        if (FN_UNLIKELY(!(cond))) {                                         \
            ::fn::assertFailed(FN_PSTR(#cond), FN_PSTR(__FILE__), __LINE__, \
                               ::fn::crash::Code::Assert);                  \
        }                                                                   \
    } while (0)
#endif

#ifndef FN_ASSERT_MSG
#define FN_ASSERT_MSG(cond, msg)                                            \
    do {                                                                    \
        if (FN_UNLIKELY(!(cond))) {                                         \
            ::fn::assertFailed(FN_PSTR(msg), FN_PSTR(__FILE__), __LINE__,   \
                               ::fn::crash::Code::Assert);                  \
        }                                                                   \
    } while (0)
#endif

// Выражение вычисляется всегда; проверка исчезает только в релизе.
#ifndef FN_VERIFY
#define FN_VERIFY(expr)                                                     \
    do {                                                                    \
        if (FN_UNLIKELY(!(expr))) {                                         \
            ::fn::assertFailed(FN_PSTR(#expr), FN_PSTR(__FILE__), __LINE__, \
                               ::fn::crash::Code::Verify);                  \
        }                                                                   \
    } while (0)
#endif

#else  // ---------------------- F_USE_ASSERT == 0 ---------------------------

#ifndef FN_ASSERT
#define FN_ASSERT(cond) ((void)0)
#endif

#ifndef FN_ASSERT_MSG
#define FN_ASSERT_MSG(cond, msg) ((void)0)
#endif

#ifndef FN_VERIFY
#define FN_VERIFY(expr) ((void)(expr))
#endif

#endif  // F_USE_ASSERT

#ifndef FN_STATIC_ASSERT
#define FN_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#endif

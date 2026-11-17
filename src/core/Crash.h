#pragma once
// ============================================================================
//  Crash.h — состояние, переживающее сброс, и примитивы останова
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Общий узел для Assert и Reset. Держит в .noinit единственную запись,
//    которая не обнуляется при перезапуске, и захватывает MCUSR в .init3 —
//    до инициализации .data/.bss, до конструкторов и до setup(). Отсюда же
//    примитивы «остановиться» и «перезапуститься», чтобы не дублировать
//    работу с WDT в двух модулях.
//
//  ЧТО ХРАНИТСЯ (12 байт .noinit)
//    magic       сигнатура валидности
//    boots       счётчик загрузок (растёт при любом сбросе)
//    resetFlags  снимок MCUSR, сделанный до main()
//    code        причина аварии (Code)
//    line        номер строки срыва
//    expr, file  указатели во flash на текст условия и имя файла
//    sum         байтовая контрольная сумма всех полей
//
//  API (namespace fn::crash)
//    enum class Code   : uint8_t { None, Assert, Verify, Manual }
//    enum class Policy : uint8_t { Halt, Reset }
//    boots() -> uint16_t          номер текущей загрузки (1 = первое включение)
//    resetFlags() -> uint8_t      снимок MCUSR (PORF/EXTRF/BORF/WDRF)
//    clearResetFlags()            обнулить снимок (счётчик не трогать)
//    crashed() -> bool            зафиксирована авария
//    code() -> Code               причина
//    line() -> uint16_t           строка срыва
//    expr() / file() -> PGM_P     текст условия и файл (только текущая сессия)
//    clear()                      стереть запись об аварии
//    setPolicy(Policy) / policy()
//    record(Code, expr, file, line, Policy)   записать и выполнить политику
//    halt()                       cli + wdt_disable + вечный цикл
//    forceReset()                 cli + WDT 15 мс + ожидание сброса
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - .noinit не обнуляется crt0, но после подачи питания содержит мусор.
//      Валидность определяется по сигнатуре И контрольной сумме; при первом
//      старте запись инициализируется заново, boots = 1.
//    - expr()/file() обнуляются при старте: указатели во flash относятся к
//      прошивке, которая могла измениться. Читать их имеет смысл только
//      внутри обработчика аварии, до сброса. После перезапуска печатайте
//      code() и line().
//    - record() не возвращается. Policy::Halt выключает WDT — иначе «останов»
//      превратился бы в тихий перезапуск через 15 мс...8 с.
//    - При прошитом фьюзе WDTON выключить WDT невозможно: halt() приведёт к
//      перезапуску. Это ограничение железа.
//    - Захват MCUSR делается в .init3, а не в конструкторе с init_priority:
//      порядок конструкторов между единицами трансляции не гарантирован.
//      После watchdog-сброса WDT остаётся включён с минимальным таймаутом,
//      поэтому выключается там же — иначе boot loop до setup().
//    - Вызов из ISR допустим: запись делается с запрещёнными прерываниями.
//    - Запись занимает 12 байт SRAM всегда, даже при F_USE_ASSERT = 0:
//      счётчик загрузок и причина сброса нужны и в релизе.
//
//  СТОИМОСТЬ (AVR)
//    12 байт .noinit + 1 байт .bss (политика) + ~200 байт flash.
//
//  ПРИМЕР
//    void setup() {
//        Uart::begin(9600);
//        Uart::print(FN_PSTR("boot #")); Uart::println(crash::boots());
//        if (crash::crashed()) {
//            Uart::print(FN_PSTR("crash code="));
//            Uart::print(uint8_t(crash::code()));
//            Uart::print(FN_PSTR(" line="));
//            Uart::println(crash::line());
//            crash::clear();
//        }
//        crash::setPolicy(crash::Policy::Reset);   // эксплуатация: перезапуск
//    }
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h; реализация — Crash.cpp
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>
#include <avr/pgmspace.h>

namespace fn {
namespace crash {

enum class Code : uint8_t {
    None = 0,
    Assert,
    Verify,
    Manual
};

enum class Policy : uint8_t {
    Halt = 0,   ///< остановиться с выключенным WDT (отладка)
    Reset       ///< записать причину и перезапуститься (эксплуатация)
};

uint16_t boots();
uint8_t resetFlags();
void clearResetFlags();

bool crashed();
Code code();
uint16_t line();
PGM_P expr();
PGM_P file();
void clear();

void setPolicy(Policy p);
Policy policy();

[[noreturn]] void record(Code c, PGM_P e, PGM_P f, uint16_t ln, Policy p);
[[noreturn]] void halt();
[[noreturn]] void forceReset();

}  // namespace crash
}  // namespace fn
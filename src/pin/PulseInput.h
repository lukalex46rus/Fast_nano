#pragma once
// ============================================================================
//  PulseInput.h — измерение длительности импульса опросом + HC-SR04
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Блокирующее измерение длительности HIGH/LOW на любом цифровом выводе
//    опросом регистра PINx. Аналог Arduino pulseIn, но с раздельными бюджетами
//    ожидания и измерения, кодами возврата и без зависимости от Print/Stream.
//    Плюс драйвер ультразвукового дальномера HC-SR04 (trig/echo).
//
//  КОГДА ЭТОТ МОДУЛЬ, А КОГДА InputCapture
//    Разрешение ограничено micros(): 4 мкс при 16 МГц. Каждый виток цикла
//    читает PINx и вызывает micros() (~2 мкс), поэтому импульс короче ~10 мкс
//    может быть пропущен целиком. Для точности до такта (62.5 нс) и коротких
//    импульсов используйте pin/Pulse.h (аппаратный захват ICP1 = D8).
//    Опрос работает на ЛЮБОМ выводе и не занимает таймер — в этом его смысл.
//
//  ВЛАДЕНИЕ
//    Векторов и таймеров не занимает, ISR нет. Время берёт из timer/Tick
//    (micros ядра Arduino, Timer0). Конфликтов с арбитражем ресурсов нет.
//    Выводы используются как GPIO: конфликт с PWM/PCINT того же пина —
//    ответственность прикладного кода.
//
//  API (namespace fn)
//    struct PulseResult { Result status; uint32_t us; }   5 байт
//
//    class PulseInput — только статические методы, состояния нет
//      enum class Level : uint8_t { Low, High }
//      measure(Pin, Level, measureTimeoutUs, waitTimeoutUs) -> PulseResult
//      measure(uint8_t pin, Level, measureTimeoutUs, waitTimeoutUs) -> PulseResult
//      measureCurrent(Pin, Level, timeoutUs) -> PulseResult
//        измеряет ОСТАТОК уже идущего импульса, без ожидания фронта
//
//    Совместимость (0 при любой ошибке, как в исходной версии):
//      pulseIn(pin, state, timeoutUs)     -> uint32_t
//      pulseInHigh(pin, timeoutUs)        -> uint32_t
//      pulseInLow(pin, timeoutUs)         -> uint32_t
//
//    HC-SR04:
//      sonarBegin(trig, echo) -> Result           настройка выводов
//      sonarMeasure(trig, echo, uint32_t& usOut, timeoutUs) -> Result
//      sonarUs(trig, echo, timeoutUs)   -> uint32_t   0 при ошибке
//      sonarCm(trig, echo, timeoutUs)   -> uint16_t   сантиметры
//      sonarMm(trig, echo, timeoutUs)   -> uint16_t   миллиметры (точнее)
//
//  КОДЫ ВОЗВРАТА measure
//    Ok          импульс измерен, us — длительность
//    BadArg      вывод не существует (вне 0..19, PC7)
//    Timeout     не дождались НАЧАЛА импульса за waitTimeoutUs
//    OutOfRange  импульс начался, но не закончился за measureTimeoutUs
//                (слишком длинный либо линия залипла)
//    NotReady    только measureCurrent: импульс не идёт на момент входа
//
//  КОНФИГУРАЦИЯ
//    FN_PULSE_IN_TIMEOUT_US  1000000  бюджет измерения по умолчанию (1 с)
//    FN_PULSE_IN_WAIT_US     1000000  бюджет ожидания фронта по умолчанию
//    FN_SONAR_TIMEOUT_US     30000    бюджет измерения эха (~5 м)
//    FN_SONAR_WAIT_US        2000     ожидание подъёма echo после trig
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Бюджеты РАЗДЕЛЬНЫ: ожидание фронта тратит waitTimeoutUs, измерение —
//      свои measureTimeoutUs. Худший случай блокировки — их сумма, а не
//      максимум. Прежняя версия делила один бюджет между фазами ожидания,
//      из-за чего сумма и максимум совпадали случайно; теперь это явно.
//    - Все сравнения времени через fn::elapsed(): прямое micros() > deadline
//      ломается на переполнении счётчика через 71.6 минут.
//    - Разрешение 4 мкс при 16 МГц (8 мкс при 8 МГц). Погрешность ±4 мкс
//      плюс джиттер от прерываний: любой ISR внутри цикла удлиняет замер.
//      Для точных измерений запрещайте прерывания снаружи либо берите
//      InputCapture.
//    - measure() всегда измеряет СЛЕДУЮЩИЙ целый импульс: если на входе
//      уровень уже нужный, сначала дожидается его конца. Это поведение
//      Arduino pulseIn, сохранено намеренно.
//    - measureCurrent() измеряет от момента входа, а не от фронта: результат
//      занижен на уже прошедшую часть импульса. Пригоден для «сколько ещё
//      осталось», не для «какова длительность».
//    - Адрес регистра вычисляется ОДИН раз через regs::sfr(), дальше цикл
//      читает *reg & mask напрямую. Вызов regs::read(Pin) в каждом витке
//      добавлял бы проверку валидности и пересчёт адреса — это удвоило бы
//      время витка и ухудшило разрешение.
//    - sonarMeasure() сама настраивает trig (выход, LOW) и echo (вход без
//      подтяжки): вызывать sonarBegin() необязательно. Прежняя sonarUs()
//      писала в PORTx, не проверяя DDR — без sonarBegin вывод оставался
//      входом и импульс не выдавался.
//    - Триггерный импульс 10 мкс формируется _delay_us() с КОНСТАНТНЫМ
//      аргументом: avr-libc разворачивает точный цикл, прерывания не
//      запрещаются. delayMicroseconds() ядра на 2 мкс даёт ошибку порядка
//      самого интервала.
//    - HC-SR04 требует паузы ≥60 мс между замерами: иначе эхо предыдущего
//      пакета принимается за текущий. Модуль паузу не выдерживает — это
//      задача вызывающего (fn::Timeout).
//    - Пересчёт: 58 мкс/см (звук 343 м/с, туда-обратно). Зависит от
//      температуры: ~0.17%/°C. При 0 °C погрешность около 3.5%.
//    - Насыщение sonarCm/sonarMm на 0xFFFF: при дефолтном таймауте 30 мс
//      недостижимо (517 см), но при произвольном таймауте срабатывает.
//    - Модуль блокирующий. В кооперативном планировщике (logic/Task) вызов
//      с таймаутом 1 с остановит все задачи на это время. Ставьте бюджет
//      по худшему ожидаемому сигналу, не «на всякий случай побольше».
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 0 байт — состояния нет, все функции чистые.
//    Flash: ~300 байт при полном использовании (measure + sonar + пересчёты).
//    Неиспользуемые функции вырезаются линкером.
//
//  ПРИМЕР
//    // Импульс с датчика на D4, ждём до 50 мс, мерим до 10 мс
//    auto r = fn::PulseInput::measure(4, fn::PulseInput::Level::High, 10000, 50000);
//    if (fn::ok(r.status)) use(r.us);
//    else if (r.status == fn::Result::OutOfRange) { /* импульс слишком长 */ }
//    else { /* сигнала нет */ }
//
//    // HC-SR04 с паузой 60 мс
//    fn::Timeout period(60);
//    void loop() {
//        if (period.expired()) {
//            period.restart();
//            uint16_t mm = fn::sonarMm(7, 8);
//            if (mm) show(mm);
//        }
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Registers.h,
//               timer/Tick.h, pin/Board.h, util/delay.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Registers.h"
#include "../timer/Tick.h"
#include "Board.h"

#include <stdint.h>
#include <util/delay.h>

#if F_USE_PULSE_IN

#ifndef FN_PULSE_IN_TIMEOUT_US
#define FN_PULSE_IN_TIMEOUT_US 1000000UL
#endif

#ifndef FN_PULSE_IN_WAIT_US
#define FN_PULSE_IN_WAIT_US 1000000UL
#endif

#ifndef FN_SONAR_TIMEOUT_US
#define FN_SONAR_TIMEOUT_US 30000UL
#endif

#ifndef FN_SONAR_WAIT_US
#define FN_SONAR_WAIT_US 2000UL
#endif

namespace fn {

// Результат измерения: код + длительность. Отказы различимы, в отличие
// от прежнего «0 на всё».
struct PulseResult {
    Result   status;
    uint32_t us;
};

static_assert(sizeof(PulseResult) == 5,
              "fn::PulseResult: ожидалось 5 байт (выравнивание на AVR = 1)");

class PulseInput {
public:
    enum class Level : uint8_t { Low = 0, High = 1 };

    // Ожидание следующего целого импульса + измерение. Бюджеты раздельны.
    static PulseResult measure(Pin p, Level lvl,
                               uint32_t measureTimeoutUs = FN_PULSE_IN_TIMEOUT_US,
                               uint32_t waitTimeoutUs = FN_PULSE_IN_WAIT_US) {
        if (!p.valid()) return PulseResult{Result::BadArg, 0};

        // Адрес вычисляется один раз: в цикле только чтение регистра.
        volatile uint8_t* reg = regs::sfr(p.port(), Reg::Pin);
        if (reg == nullptr) return PulseResult{Result::BadArg, 0};

        const uint8_t mask = p.mask();
        const bool wantHigh = (lvl == Level::High);
        const uint32_t waitBudget = waitTimeoutUs ? waitTimeoutUs : FN_PULSE_IN_WAIT_US;
        const uint32_t measBudget = measureTimeoutUs ? measureTimeoutUs : FN_PULSE_IN_TIMEOUT_US;

        // Фаза A: дождаться фронта. Общий бюджет на «конец предыдущего»
        // и «начало следующего» — это одна операция ожидания сигнала.
        const uint32_t tw = ::fn::micros();
        while (((*reg & mask) != 0) == wantHigh) {
            if (elapsed(tw, ::fn::micros()) >= waitBudget)
                return PulseResult{Result::Timeout, 0};
        }
        while (((*reg & mask) != 0) != wantHigh) {
            if (elapsed(tw, ::fn::micros()) >= waitBudget)
                return PulseResult{Result::Timeout, 0};
        }

        // Фаза B: измерение со СВОИМ бюджетом.
        return measureLoop(reg, mask, wantHigh, measBudget);
    }

    static PulseResult measure(uint8_t pin, Level lvl,
                               uint32_t measureTimeoutUs = FN_PULSE_IN_TIMEOUT_US,
                               uint32_t waitTimeoutUs = FN_PULSE_IN_WAIT_US) {
        return measure(boardPin(pin), lvl, measureTimeoutUs, waitTimeoutUs);
    }

    // Остаток уже идущего импульса. NotReady, если уровня нет на входе.
    static PulseResult measureCurrent(Pin p, Level lvl,
                                      uint32_t timeoutUs = FN_PULSE_IN_TIMEOUT_US) {
        if (!p.valid()) return PulseResult{Result::BadArg, 0};
        volatile uint8_t* reg = regs::sfr(p.port(), Reg::Pin);
        if (reg == nullptr) return PulseResult{Result::BadArg, 0};

        const uint8_t mask = p.mask();
        const bool wantHigh = (lvl == Level::High);
        if (((*reg & mask) != 0) != wantHigh) return PulseResult{Result::NotReady, 0};

        return measureLoop(reg, mask, wantHigh,
                           timeoutUs ? timeoutUs : FN_PULSE_IN_TIMEOUT_US);
    }

private:
    PulseInput() = delete;

    // Общий цикл измерения. Метка времени берётся в том же витке, где
    // обнаружен спад: замер не получает лишнего вызова micros().
    static PulseResult measureLoop(volatile uint8_t* reg, uint8_t mask,
                                   bool wantHigh, uint32_t budget) {
        const uint32_t start = ::fn::micros();
        for (;;) {
            const bool still = (((*reg & mask) != 0) == wantHigh);
            const uint32_t now = ::fn::micros();
            if (!still) return PulseResult{Result::Ok, elapsed(start, now)};
            if (elapsed(start, now) >= budget) return PulseResult{Result::OutOfRange, 0};
        }
    }
};

// ============================ совместимость =================================
// Прежний API: 0 при любой ошибке. Новый код должен звать measure() и
// различать Timeout / OutOfRange / BadArg.
inline uint32_t pulseIn(uint8_t pin, uint8_t state,
                        uint32_t timeoutUs = FN_PULSE_IN_TIMEOUT_US) {
    const PulseResult r = PulseInput::measure(
        pin, state ? PulseInput::Level::High : PulseInput::Level::Low, timeoutUs, timeoutUs);
    return ok(r.status) ? r.us : 0;
}

inline uint32_t pulseInHigh(uint8_t pin, uint32_t timeoutUs = FN_PULSE_IN_TIMEOUT_US) {
    return pulseIn(pin, 1, timeoutUs);
}

inline uint32_t pulseInLow(uint8_t pin, uint32_t timeoutUs = FN_PULSE_IN_TIMEOUT_US) {
    return pulseIn(pin, 0, timeoutUs);
}

// ============================ HC-SR04 =======================================
inline Result sonarBegin(uint8_t trigPin, uint8_t echoPin) {
    const Pin t = boardPin(trigPin);
    const Pin e = boardPin(echoPin);
    if (!t.valid() || !e.valid() || t == e) return Result::BadArg;
    regs::modeOutput(t, false);   // сначала LOW, потом выход — без выброса
    regs::modeInput(e);           // echo push-pull, подтяжка не нужна
    return Result::Ok;
}

// Один замер. Выводы настраиваются здесь же: sonarBegin необязателен.
inline Result sonarMeasure(uint8_t trigPin, uint8_t echoPin, uint32_t& usOut,
                           uint32_t timeoutUs = FN_SONAR_TIMEOUT_US) {
    usOut = 0;
    const Pin t = boardPin(trigPin);
    const Pin e = boardPin(echoPin);
    if (!t.valid() || !e.valid() || t == e) return Result::BadArg;

    regs::modeOutput(t, false);
    regs::modeInput(e);

    // trig: LOW 2 мкс -> HIGH 10 мкс -> LOW. Константные задержки точны
    // и не запрещают прерывания.
    regs::clear(t);
    _delay_us(2);
    regs::set(t);
    _delay_us(10);
    regs::clear(t);

    const PulseResult r = PulseInput::measure(e, PulseInput::Level::High,
                                              timeoutUs, FN_SONAR_WAIT_US);
    usOut = r.us;
    return r.status;
}

inline uint32_t sonarUs(uint8_t trigPin, uint8_t echoPin,
                        uint32_t timeoutUs = FN_SONAR_TIMEOUT_US) {
    uint32_t us = 0;
    return ok(sonarMeasure(trigPin, echoPin, us, timeoutUs)) ? us : 0;
}

// Расстояние в сантиметрах: 58 мкс/см, округление к ближайшему.
inline uint16_t sonarCm(uint8_t trigPin, uint8_t echoPin,
                        uint32_t timeoutUs = FN_SONAR_TIMEOUT_US) {
    const uint32_t us = sonarUs(trigPin, echoPin, timeoutUs);
    if (us == 0) return 0;
    const uint32_t cm = (us + 29UL) / 58UL;
    return (cm > 0xFFFFUL) ? uint16_t(0xFFFFUL) : uint16_t(cm);
}

// В миллиметрах: разрешение датчика ~3 мм, сантиметры его теряют.
inline uint16_t sonarMm(uint8_t trigPin, uint8_t echoPin,
                        uint32_t timeoutUs = FN_SONAR_TIMEOUT_US) {
    const uint32_t us = sonarUs(trigPin, echoPin, timeoutUs);
    if (us == 0) return 0;
    const uint32_t mm = (us * 10UL + 29UL) / 58UL;
    return (mm > 0xFFFFUL) ? uint16_t(0xFFFFUL) : uint16_t(mm);
}

}  // namespace fn

#endif  // F_USE_PULSE_IN
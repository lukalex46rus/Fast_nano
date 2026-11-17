#pragma once
// ============================================================================
//  AnalogComparator.h — аналоговый компаратор AIN0/AIN1 с пробуждением из сна
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Сравнение двух аналоговых уровней без участия CPU. Ключевое свойство:
//    компаратор работает в PowerDown и способен разбудить МК — это единственный
//    аналоговый источник пробуждения на 328p. ADC для этого не годится:
//    в PowerDown он остановлен.
//
//  НЕЗАВИСИМОСТЬ ОТ ADC
//    Модуль гейтится собственным F_USE_AC, а не F_USE_ADC. Отключив ADC ради
//    экономии, компаратор можно оставить: он потребляет десятки микроампер
//    и будит систему по пересечению порога.
//    Единственная связь: минусовой вход можно взять с мультиплексора ADC,
//    и для этого ADC обязан быть ВЫКЛЮЧЕН (см. setNegativeMux).
//
//  ВЛАДЕНИЕ
//    Вектор ANALOG_COMP заявляется модулем. Выводы: AIN0 = D6 (PD6),
//    AIN1 = D7 (PD7). Совпадают с OC0A (PWM Timer0) и OC0B? нет: OC0B = PD5.
//    Конфликт: D6 против PWM Timer0 канал A.
//
//  API (namespace fn, класс AnalogComparator — только статические методы)
//    enum class Positive : uint8_t { Ain0, Vbg }        плюс: D6 или 1.1 В
//    enum class Negative : uint8_t { Ain1, Mux }        минус: D7 или канал ADC
//    enum class Trigger : uint8_t { Change, Falling, Rising }
//    using Callback = void (*)()
//
//    begin(Positive = Ain0, Negative = Ain1) -> Result
//    end()
//    enable() / disable() / enabled() -> bool
//
//    setPositive(Positive)
//    setNegative(Negative)
//    setNegativeMux(uint8_t channel) -> Result
//      канал 0..7 (A0..A7). Отключает ADC (ADEN=0), ставит ACME, выбирает ADMUX.
//      Возвращает Busy, если идёт конверсия ADC.
//    restoreAdc()                          вернуть ADEN, снять ACME
//
//    read() -> bool                        текущий результат сравнения (ACO)
//    attach(Callback, Trigger = Change) -> Result
//    detach() / isAttached()
//
//    enableInputCapture(bool)              ACIC: компаратор запускает захват Timer1
//    disableDigitalInput(bool ain0, bool ain1)   DIDR1: отключить цифровые буферы
//
//    lastError() -> Result
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - setNegative(Mux) БЕЗ выключения ADC не работает: по даташиту ACME
//      действует только при ADEN=0. Прежняя версия ставила ACME и не трогала
//      ADEN — компаратор продолжал сравнивать с AIN1, молча. Используйте
//      setNegativeMux(), он делает всё правильно и запоминает состояние ADC.
//    - После смены входов результат ACO устанавливается не мгновенно:
//      задержка компаратора порядка 1 мкс. Читайте read() не сразу после
//      setPositive/setNegative, либо дождитесь прерывания.
//    - Trigger::Change срабатывает на оба фронта; Falling/Rising — на один.
//      Прерывание по уровню отсутствует (в отличие от INT0/INT1).
//    - При использовании Vbg как плюса и AIN1 как минуса получается пороговый
//      детектор 1.1 В без внешних компонентов. Точность порога — разброс Vbg
//      ±10% по даташиту.
//    - enableInputCapture(true) связывает выход компаратора с ICP1: Timer1
//      захватывает момент пересечения порога с точностью до такта. Конфликтует
//      с InputCapture (pin/Pulse), который использует ICP1 как внешний вход:
//      одновременно либо внешний захват, либо компараторный.
//    - disableDigitalInput обязателен при работе с аналоговыми уровнями:
//      цифровой буфер на среднем уровне потребляет ток и вносит шум.
//    - Коллбэк вызывается ИЗ ISR: короткий, без delay/Serial.
//    - Пробуждение из PowerDown: настройте attach(...), затем Power::setMode
//      (PowerDown) и Power::sleepOnce(). Компаратор тактируется независимо.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 4 байта (коллбэк 2, lastError 1, сохранённый ADEN 1).
//    Flash: ~250 байт при полном использовании. Таблиц нет.
//
//  ПРИМЕР
//    FN_CLAIM_VECTOR(AnalogComp, FN_OWNER_ADC);
//
//    // Пороговый детектор 1.1 В на D7, пробуждение из сна
//    fn::AnalogComparator::begin(fn::AnalogComparator::Positive::Vbg,
//                                fn::AnalogComparator::Negative::Ain1);
//    fn::AnalogComparator::disableDigitalInput(false, true);
//    fn::AnalogComparator::attach(onCross, fn::AnalogComparator::Trigger::Rising);
//    fn::Power::setMode(fn::Power::Mode::PowerDown);
//    fn::Power::sleepOnce();
//
//    // Минус с канала A3 (ADC выключается автоматически)
//    fn::AnalogComparator::setNegativeMux(3);
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h;
//               реализация — AnalogComparator.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"

#include <stdint.h>

#if F_USE_AC

namespace fn {

class AnalogComparator {
public:
    enum class Positive : uint8_t { Ain0 = 0, Vbg = 1 };
    enum class Negative : uint8_t { Ain1 = 0, Mux = 1 };
    enum class Trigger : uint8_t { Change = 0, Falling = 2, Rising = 3 };

    using Callback = void (*)();

    static Result begin(Positive p = Positive::Ain0, Negative n = Negative::Ain1);
    static void end();

    static void enable();
    static void disable();
    static bool enabled();

    static void setPositive(Positive p);
    static void setNegative(Negative n);
    static Result setNegativeMux(uint8_t channel);
    static void restoreAdc();

    static bool read();

    static Result attach(Callback cb, Trigger t = Trigger::Change);
    static void detach();
    static bool isAttached();

    static void enableInputCapture(bool on);
    static void disableDigitalInput(bool ain0, bool ain1);

    static Result lastError();

private:
    AnalogComparator() = delete;
};

}  // namespace fn

#endif  // F_USE_AC

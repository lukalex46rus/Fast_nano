#pragma once
// ============================================================================
//  Adc.h — АЦП ATmega328P: 10 бит, 8 каналов, термодатчик, Vbg, Vcc
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственный владелец ADC. Блокирующее и асинхронное измерение, автозапуск
//    по событию, режим подавления шума, отключение цифровых входных буферов,
//    измерение напряжения питания через внутренний источник 1.1 В.
//
//  ВЛАДЕНИЕ
//    Вектор ADC заявляется модулем (FN_CLAIM_VECTOR(Adc, FN_OWNER_ADC)).
//    Конфликт с analogRead() ядра Arduino: обе стороны пишут ADMUX/ADCSRA.
//    Смешивать нельзя — analogRead меняетreference и multiplexer молча.
//    Выводы A0..A5 (PC0..PC5) при использовании заняты; A6/A7 — только ADC.
//
//  API (namespace fn, класс Adc — только статические методы)
//    enum class Mux : uint8_t
//      A0..A7, TempSensor, Vbg, Gnd
//    enum class Ref : uint8_t { Aref, Avcc, Internal }   Internal = 1.1 В
//    enum class Prescaler : uint8_t { P2=1, P4, P8, P16, P32, P64, P128 }
//    enum class Trigger : uint8_t
//      FreeRun, AnalogComp, Int0, Timer0CompA, Timer0Ovf,
//      Timer1CompB, Timer1Ovf, Timer1Capt
//    enum class Res : uint8_t { Bits8, Bits10 }
//
//    begin(Ref = Avcc, Prescaler = P128, Res = Bits10) -> Result
//    end()
//    enabled() -> bool                 ADEN
//    ready() -> bool                   конверсия завершена (ADSC == 0)
//    clock() -> uint32_t               частота ADC, Гц
//    clockInRange() -> bool            50..200 кГц (условие 10 бит)
//
//    setMux(Mux) / setReference(Ref) / setPrescaler(Prescaler) / setResolution(Res)
//    mux() / reference() / resolution()
//
//    start()                           запустить конверсию
//    read10() -> uint16_t              сырой результат, атомарно
//    read8() -> uint8_t                старший байт (корректен при Bits8)
//    readBlocking(uint16_t& out, uint32_t timeoutUs = FN_ADC_TIMEOUT_US) -> Result
//    readBlocking() -> int32_t         -1 при ошибке/таймауте
//    readMux(Mux, uint16_t& out, uint32_t timeoutUs) -> Result   с паузой и сбросом первой
//    readMux(Mux) -> int32_t
//    readNoiseReduced(uint16_t& out) -> Result   сон ADC Noise Reduction
//
//    enableAutoTrigger(Trigger) / disableAutoTrigger()
//    attach(Callback) -> Result / detach() / isAttached()
//
//    disableDigitalInput(uint8_t mask) / enableDigitalInput(uint8_t mask)
//      бит i соответствует A0+i (DIDR0). Отключает цифровой буфер вывода:
//      меньше потребление и меньше шума на аналоговом входе.
//
//    readVccMillivolts() -> uint16_t   напряжение питания через Vbg
//    readTemperatureC() -> int16_t     температура кристалла, ±10 °C без калибровки
//
//    lastError() -> Result
//    timeouts() -> uint16_t
//    clearStats()
//
//  КОНФИГУРАЦИЯ
//    FN_ADC_TIMEOUT_US      2000  таймаут одной конверсии по умолчанию
//    FN_ADC_TEMP_OFFSET_MV  314   напряжение датчика при 25 °C (типовое)
//    FN_ADC_TEMP_SLOPE_UV   1220  наклон, мкВ/°C (типовой)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Частота ADC должна быть 50..200 кГц для полных 10 бит. При 16 МГц
//      подходит P128 (125 кГц) и P64 (250 кГц, уже вне спецификации).
//      P2 даёт 8 МГц: конверсия быстрая, но реальная разрядность падает
//      примерно до 8 бит. begin() возвращает OutOfRange при выходе за
//      200 кГц, но настройку применяет — решение за вызывающим.
//    - Конверсия занимает 13 тактов ADC: при 125 кГц это 104 мкс, при 8 МГц
//      1.6 мкс. readBlocking ждёт с таймаутом, а не бесконечно.
//    - После смены MUX или REF первая конверсия недостоверна (заряд выборки).
//      readMux() делает холостую конверсию и возвращает вторую. Прямой
//      setMux() + readBlocking() этого НЕ делает: вызывайте readMux().
//    - read8() корректен только при Res::Bits8 (ADLAR=1). При Bits10 он
//      вернёт биты 9:8, а не старший байт результата. Проверяйте resolution().
//    - read10() при Bits8 вернёт значение, сдвинутое влево на 6. Для
//      единообразия читайте read8() при Bits8 и read10() при Bits10.
//    - Термодатчик требует Ref::Internal и паузы ~10 мкс после выбора;
//      readMux(Mux::TempSensor) это делает. Точность без калибровки ±10 °C:
//      разброс смещения от кристалла к кристаллу не нормируется.
//    - readVccMillivolts() измеряет Vbg относительно AVcc и вычисляет питание.
//      Точность зависит от разброса Vbg (1.0..1.2 В по даташиту, ±10%).
//      Для точного значения откалибруйте константу под свой экземпляр.
//    - readNoiseReduced() вводит МК в ADC Noise Reduction на время конверсии:
//      CPU и I/O стоят, ADC работает. Требует разрешённых прерываний;
//      модуль временно включает ADIE и восстанавливает состояние.
//      Даёт выигрыш ~1-2 младших разряда на шумной плате.
//    - Автозапуск (FreeRun) требует первой конверсии вручную: enableAutoTrigger
//      только выбирает источник, start() запускает цепочку.
//    - Коллбэк вызывается ИЗ ISR: короткий, без delay/Serial. Результат
//      забирайте read10() внутри коллбэка.
//    - Отключение цифрового буфера (DIDR0) обязательно для A6/A7 и рекомендуется
//      для всех аналоговых входов: буфер на среднем уровне потребляет ток.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 6 байт (коллбэк 2, lastError 1, timeouts 2, флаги 1).
//    Flash: ~600 байт при полном использовании, включая Vcc и температуру.
//    Неиспользуемые методы вырезаются линкером. Таблиц нет.
//
//  ПРИМЕР
//    FN_CLAIM_VECTOR(Adc, FN_OWNER_ADC);
//
//    void setup() {
//        fn::Adc::begin(fn::Adc::Ref::Avcc, fn::Adc::Prescaler::P128);
//        fn::Adc::disableDigitalInput(0x3F);        // A0..A5 — аналоговые
//        uint16_t v = uint16_t(fn::Adc::readMux(fn::Adc::Mux::A0));
//        uint16_t vcc = fn::Adc::readVccMillivolts();
//    }
//
//    // Асинхронно
//    void onAdc() { lastValue = fn::Adc::read10(); }
//    fn::Adc::attach(onAdc);
//    fn::Adc::setMux(fn::Adc::Mux::A1);
//    fn::Adc::start();
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Resources.h,
//               timer/Tick.h; реализация — Adc.cpp
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Resources.h"

#include <stdint.h>

#if F_USE_ADC

#ifndef FN_ADC_TIMEOUT_US
#define FN_ADC_TIMEOUT_US 2000
#endif

#ifndef FN_ADC_TEMP_OFFSET_MV
#define FN_ADC_TEMP_OFFSET_MV 314
#endif

#ifndef FN_ADC_TEMP_SLOPE_UV
#define FN_ADC_TEMP_SLOPE_UV 1220
#endif

namespace fn {

class Adc {
public:
    enum class Mux : uint8_t {
        A0 = 0, A1, A2, A3, A4, A5, A6, A7,
        TempSensor = 8,
        Vbg        = 14,
        Gnd        = 15
    };

    enum class Ref : uint8_t { Aref = 0, Avcc = 1, Internal = 3 };

    // Значения = поле ADPS2..0.
    enum class Prescaler : uint8_t { P2 = 1, P4, P8, P16, P32, P64, P128 };

    // Значения = поле ADTS2..0 регистра ADCSRB.
    enum class Trigger : uint8_t {
        FreeRun = 0, AnalogComp, Int0, Timer0CompA,
        Timer0Ovf, Timer1CompB, Timer1Ovf, Timer1Capt
    };

    enum class Res : uint8_t { Bits8, Bits10 };

    using Callback = void (*)();

    static Result begin(Ref r = Ref::Avcc,
                        Prescaler p = Prescaler::P128,
                        Res res = Res::Bits10);
    static void end();

    static bool enabled();
    static bool ready();
    static uint32_t clock();
    static bool clockInRange();

    static void setMux(Mux m);
    static void setReference(Ref r);
    static void setPrescaler(Prescaler p);
    static void setResolution(Res r);

    static Mux mux();
    static Ref reference();
    static Res resolution();

    static void start();
    static uint16_t read10();
    static uint8_t read8();

    static Result readBlocking(uint16_t& out, uint32_t timeoutUs = FN_ADC_TIMEOUT_US);
    static int32_t readBlocking();

    static Result readMux(Mux m, uint16_t& out, uint32_t timeoutUs = FN_ADC_TIMEOUT_US);
    static int32_t readMux(Mux m);

    static Result readNoiseReduced(uint16_t& out);

    static void enableAutoTrigger(Trigger t);
    static void disableAutoTrigger();

    static Result attach(Callback cb);
    static void detach();
    static bool isAttached();

    static void disableDigitalInput(uint8_t mask);
    static void enableDigitalInput(uint8_t mask);

    static uint16_t readVccMillivolts();
    static int16_t readTemperatureC();

    static Result lastError();
    static uint16_t timeouts();
    static void clearStats();

private:
    Adc() = delete;
};

}  // namespace fn

#endif  // F_USE_ADC

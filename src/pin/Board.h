#pragma once
// ============================================================================
//  Board.h — раскладка выводов Arduino Nano / Uno (ATmega328P)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Единственное место, где живёт соответствие «номер вывода платы -> порт и
//    бит». Ядро (core/) знает только порты B/C/D и биты 0..7; перевод нумерации
//    платы — задача этого файла. Все функции constexpr: при константном номере
//    в код не попадает ничего.
//
//  НУМЕРАЦИЯ
//    D0..D13  = 0..13   (PD0..PD7, PB0..PB5)
//    A0..A5   = 14..19  (PC0..PC5)
//    A6/A7 на Nano есть физически, но это только ADC-входы (ADC6/ADC7),
//    цифрового порта у них НЕТ. В нумерацию не входят.
//
//  API
//    constexpr uint8_t PinCount = 20
//    PORT_B / PORT_C / PORT_D            legacy-индексы портов (0/1/2)
//    pinValid(n)                         номер существует (0..19)
//    portOf(n)  -> uint8_t               индекс порта
//    bitOf(n)   -> uint8_t               номер бита
//    boardPin(n) -> fn::Pin              упакованный дескриптор ядра
//    boardNumber(Pin) -> uint8_t         обратно: Pin -> номер платы (0xFF если нет)
//    isAnalogOnly(n)                     true для 20..21 (A6/A7), если используете
//    hasPcint(Pin)                       вывод поддерживает Pin Change
//    intLineOf(Pin) -> int8_t            0/1 для D2/D3, иначе -1
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Функции НЕ проверяют физические ограничения: PC6 = RESET, PB6/PB7 = кварц,
//      PD0/PD1 = UART. Формально это валидные выводы, реально их трогать нельзя
//      без перенастройки фьюзов или отказа от соответствующей периферии.
//    - portOf/bitOf для невалидного номера возвращают 0 — это безопасно, но
//      бессмысленно. Всегда проверяйте pinValid() или используйте boardPin(),
//      который вернёт fn::NoPin.
//    - Обратное преобразование boardNumber() работает только для цифровых
//      выводов; для NoPin и PC6/PC7 возвращает 0xFF.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт flash и RAM: всё constexpr. Рантаймовые вызовы с переменным
//    номером — 2-4 команды сравнения и вычитания.
//
//  ПРИМЕР
//    constexpr fn::Pin LED = fn::boardPin(13);      // PB5
//    static_assert(LED.port() == fn::Port::B, "");
//    fn::regs::modeOutput(LED, true);
//    uint8_t n = fn::boardNumber(LED);              // 13
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"

#include <stdint.h>

namespace fn {

constexpr uint8_t PinCount = 20;

// Legacy-индексы портов: совпадают с fn::Port и со старым Fregs.h.
constexpr uint8_t PORT_B = 0;
constexpr uint8_t PORT_C = 1;
constexpr uint8_t PORT_D = 2;

constexpr bool pinValid(uint8_t n) { return n < PinCount; }

constexpr uint8_t portOf(uint8_t n) {
    return (n <= 7) ? PORT_D : ((n <= 13) ? PORT_B : PORT_C);
}

constexpr uint8_t bitOf(uint8_t n) {
    return (n <= 7) ? n : ((n <= 13) ? uint8_t(n - 8) : uint8_t(n - 14));
}

// Упакованный дескриптор ядра. Невалидный номер -> fn::NoPin.
constexpr Pin boardPin(uint8_t n) {
    return pinValid(n) ? Pin(static_cast<Port>(portOf(n)), bitOf(n)) : Pin();
}

// Обратное преобразование. Нет соответствия -> 0xFF.
constexpr uint8_t boardNumber(Pin p) {
    return !p.valid() ? uint8_t(0xFF)
           : (p.port() == Port::D) ? uint8_t(p.bitIndex())
           : (p.port() == Port::B) ? ((p.bitIndex() <= 5) ? uint8_t(p.bitIndex() + 8) : uint8_t(0xFF))
                                   : ((p.bitIndex() <= 5) ? uint8_t(p.bitIndex() + 14) : uint8_t(0xFF));
}

// A6/A7 (номера 20/21 в расширенной нумерации) — только ADC, цифрового порта нет.
constexpr bool isAnalogOnly(uint8_t n) { return n == 20 || n == 21; }

// Pin Change доступен всем цифровым выводам, кроме отсутствующего PC7.
constexpr bool hasPcint(Pin p) { return p.valid(); }

// Линия внешнего прерывания: D2 -> 0, D3 -> 1, иначе -1.
constexpr int8_t intLineOf(Pin p) {
    return (p.port() != Port::D) ? int8_t(-1)
           : (p.bitIndex() == 2)   ? int8_t(0)
           : (p.bitIndex() == 3)   ? int8_t(1)
                                   : int8_t(-1);
}

}  // namespace fn

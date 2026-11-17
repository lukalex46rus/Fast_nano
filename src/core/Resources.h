#pragma once
// ============================================================================
//  Resources.h — compile-time арбитраж векторов прерываний и таймеров
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Не дать двум модулям занять один вектор или один таймер. На 328p под
//    прикладные задачи есть INT0/INT1, PCINT0..2, TIMER0/1/2 (по 3 источника),
//    USART, SPI, TWI, ADC, ANALOG_COMP, WDT. В библиотеке ~25 модулей-претендентов.
//    Без арбитража два ISR на вектор дают "multiple definition of __vector_N"
//    на линковке — диагностика нечитаемая. Здесь конфликт ловится на компиляции
//    с именем вектора и именами обоих владельцев.
//
//  КАК ЭТО РАБОТАЕТ
//    Каждый вектор имеет constexpr-константу владельца FN_VEC_<NAME>_OWNER,
//    заданную централизованно (этот файл или -D для всех TU). Модуль в своём
//    .cpp вызывает FN_CLAIM_VECTOR(NAME, OWNER), что разворачивается в
//    static_assert(владелец == заявленный). Никакой генерации макросов из
//    макросов — прежняя версия на этом и ломалась.
//
//  API
//    Владельцы: FN_OWNER_NONE / ARDUINO / IRQ / TIMER0 / TIMER1 / TIMER2 /
//               TICK / PWM / SERVO / STEPPER / UART / SPI / I2C / ADC /
//               PULSE / USER
//    FN_CLAIM_VECTOR(name, owner)      static_assert в теле модуля
//    FN_CLAIM_TIMER(n, owner)          то же для таймера целиком (0/1/2)
//    FN_VECTOR_OWNER(name)             текущий владелец (constexpr)
//    FN_VECTOR_IS_FREE(name)           true, если NONE
//    fn::res::ownerName(uint8_t) -> PGM_P   имя владельца для лога
//
//  ВЕКТОРЫ И ИХ ВЛАДЕЛЬЦЫ ПО УМОЛЧАНИЮ
//    INT0, INT1              ARDUINO  — ядро держит ISR в WInterrupts.c
//    PCINT0, PCINT1, PCINT2  IRQ      — наш диспетчер
//    TIMER0_OVF              ARDUINO  — millis()/micros()/delay()
//    TIMER0_COMPA/B          NONE
//    TIMER1_CAPT/OVF/COMPA/B NONE
//    TIMER2_OVF/COMPA/B      NONE
//    USART_RX/UDRE, SPI_STC, TWI, ADC, ANALOG_COMP, WDT   NONE
//
//  ТАЙМЕРЫ ЦЕЛИКОМ
//    FN_TIMER0_DEVICE = ARDUINO   (millis/micros/delay + analogWrite на D5/D6)
//    FN_TIMER1_DEVICE = NONE
//    FN_TIMER2_DEVICE = NONE      (analogWrite на D3/D11 использует его регистры,
//                                  но не прерывания — совместимо с нашим Timer2,
//                                  если не звать analogWrite на этих выводах)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - INT0/INT1 и TIMER0_OVF принадлежат ядру Arduino и НЕ МОГУТ быть отданы
//      нашему модулю: ядро определяет эти ISR безусловно. Заявка другого
//      владельца даст #error/static_assert — это правильно, а не ограничение
//      механизма. Работайте через attachInterrupt (INT) или через COMPA/COMPB
//      (Timer0).
//    - Владение объявляется ОДИН раз централизованно. Объявление в одном .cpp
//      = разные конфигурации в разных TU = нарушение ODR.
//    - Арбитраж ловит только модули, которые вызывают FN_CLAIM_*. Чужой ISR,
//      написанный мимо механизма, конфликт не поймает.
//    - Регистровый конфликт (наш PWM против analogWrite) арбитражем векторов
//      не ловится: для этого FN_CLAIM_TIMER и отказ от analogWrite на тех же
//      выводах.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт flash и RAM: constexpr + static_assert. ownerName() — таблица
//    указателей во flash, вырезается линкером, если не вызывается.
//
//  ПРИМЕР
//    // В pin/Pulse.cpp:
//    FN_CLAIM_VECTOR(TIMER1_CAPT, FN_OWNER_PULSE);
//    FN_CLAIM_VECTOR(TIMER1_OVF,  FN_OWNER_PULSE);
//    FN_CLAIM_TIMER(1, FN_OWNER_PULSE);
//
//    // Если Servo уже занял Timer1 -> static_assert с текстом:
//    // "Fast_nano: вектор TIMER1_CAPT занят владельцем 8, заявлен 14"
//    // Освободить: в этом файле FN_TIMER1_CAPT_OWNER = FN_OWNER_PULSE
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>
#include <avr/pgmspace.h>

// ---------------------------- коды владельцев ------------------------------
#define FN_OWNER_NONE 0
#define FN_OWNER_ARDUINO 1
#define FN_OWNER_IRQ 2
#define FN_OWNER_TIMER0 3
#define FN_OWNER_TIMER1 4
#define FN_OWNER_TIMER2 5
#define FN_OWNER_TICK 6
#define FN_OWNER_PWM 7
#define FN_OWNER_SERVO 8
#define FN_OWNER_STEPPER 9
#define FN_OWNER_UART 10
#define FN_OWNER_SPI 11
#define FN_OWNER_I2C 12
#define FN_OWNER_ADC 13
#define FN_OWNER_PULSE 14
#define FN_OWNER_USER 15

// ---------------------------- владельцы векторов ---------------------------
// INT0/INT1: ISR определены в WInterrupts.c ядра Arduino. Отдать нельзя.
#ifndef FN_INT0_OWNER
#define FN_INT0_OWNER FN_OWNER_ARDUINO
#endif
#ifndef FN_INT1_OWNER
#define FN_INT1_OWNER FN_OWNER_ARDUINO
#endif

// PCINT: в ядре Arduino не определены, отдаём нашему диспетчеру.
#ifndef FN_PCINT0_OWNER
#define FN_PCINT0_OWNER FN_OWNER_IRQ
#endif
#ifndef FN_PCINT1_OWNER
#define FN_PCINT1_OWNER FN_OWNER_IRQ
#endif
#ifndef FN_PCINT2_OWNER
#define FN_PCINT2_OWNER FN_OWNER_IRQ
#endif

// Timer0 OVF: millis()/micros()/delay(). Отдать нельзя.
#ifndef FN_TIMER0_OVF_OWNER
#define FN_TIMER0_OVF_OWNER FN_OWNER_ARDUINO
#endif
#ifndef FN_TIMER0_COMPA_OWNER
#define FN_TIMER0_COMPA_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER0_COMPB_OWNER
#define FN_TIMER0_COMPB_OWNER FN_OWNER_NONE
#endif

#ifndef FN_TIMER1_CAPT_OWNER
#define FN_TIMER1_CAPT_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER1_OVF_OWNER
#define FN_TIMER1_OVF_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER1_COMPA_OWNER
#define FN_TIMER1_COMPA_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER1_COMPB_OWNER
#define FN_TIMER1_COMPB_OWNER FN_OWNER_NONE
#endif

#ifndef FN_TIMER2_OVF_OWNER
#define FN_TIMER2_OVF_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER2_COMPA_OWNER
#define FN_TIMER2_COMPA_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TIMER2_COMPB_OWNER
#define FN_TIMER2_COMPB_OWNER FN_OWNER_NONE
#endif

#ifndef FN_USART_RX_OWNER
#define FN_USART_RX_OWNER FN_OWNER_NONE
#endif
#ifndef FN_USART_UDRE_OWNER
#define FN_USART_UDRE_OWNER FN_OWNER_NONE
#endif
#ifndef FN_SPI_STC_OWNER
#define FN_SPI_STC_OWNER FN_OWNER_NONE
#endif
#ifndef FN_TWI_OWNER
#define FN_TWI_OWNER FN_OWNER_NONE
#endif
#ifndef FN_ADC_OWNER
#define FN_ADC_OWNER FN_OWNER_NONE
#endif
#ifndef FN_ANALOG_COMP_OWNER
#define FN_ANALOG_COMP_OWNER FN_OWNER_NONE
#endif
#ifndef FN_WDT_OWNER
#define FN_WDT_OWNER FN_OWNER_NONE
#endif

// ---------------------------- таймеры целиком ------------------------------
#ifndef FN_TIMER0_DEVICE
#define FN_TIMER0_DEVICE FN_OWNER_ARDUINO
#endif
#ifndef FN_TIMER1_DEVICE
#define FN_TIMER1_DEVICE FN_OWNER_NONE
#endif
#ifndef FN_TIMER2_DEVICE
#define FN_TIMER2_DEVICE FN_OWNER_NONE
#endif

// ---------------------------- проверка владения ----------------------------
namespace fn {
namespace res {

// Зеркала макросов в constexpr: нужны для static_assert и для лога.
constexpr uint8_t vecInt0        = FN_INT0_OWNER;
constexpr uint8_t vecInt1        = FN_INT1_OWNER;
constexpr uint8_t vecPcint0      = FN_PCINT0_OWNER;
constexpr uint8_t vecPcint1      = FN_PCINT1_OWNER;
constexpr uint8_t vecPcint2      = FN_PCINT2_OWNER;
constexpr uint8_t vecTimer0Ovf   = FN_TIMER0_OVF_OWNER;
constexpr uint8_t vecTimer0Compa = FN_TIMER0_COMPA_OWNER;
constexpr uint8_t vecTimer0Compb = FN_TIMER0_COMPB_OWNER;
constexpr uint8_t vecTimer1Capt  = FN_TIMER1_CAPT_OWNER;
constexpr uint8_t vecTimer1Ovf   = FN_TIMER1_OVF_OWNER;
constexpr uint8_t vecTimer1Compa = FN_TIMER1_COMPA_OWNER;
constexpr uint8_t vecTimer1Compb = FN_TIMER1_COMPB_OWNER;
constexpr uint8_t vecTimer2Ovf   = FN_TIMER2_OVF_OWNER;
constexpr uint8_t vecTimer2Compa = FN_TIMER2_COMPA_OWNER;
constexpr uint8_t vecTimer2Compb = FN_TIMER2_COMPB_OWNER;
constexpr uint8_t vecUsartRx     = FN_USART_RX_OWNER;
constexpr uint8_t vecUsartUdre   = FN_USART_UDRE_OWNER;
constexpr uint8_t vecSpiStc      = FN_SPI_STC_OWNER;
constexpr uint8_t vecTwi         = FN_TWI_OWNER;
constexpr uint8_t vecAdc         = FN_ADC_OWNER;
constexpr uint8_t vecAnalogComp  = FN_ANALOG_COMP_OWNER;
constexpr uint8_t vecWdt         = FN_WDT_OWNER;

constexpr uint8_t devTimer0 = FN_TIMER0_DEVICE;
constexpr uint8_t devTimer1 = FN_TIMER1_DEVICE;
constexpr uint8_t devTimer2 = FN_TIMER2_DEVICE;

// Имя владельца во flash. Указатель читать только через pgm_read_*/FPSTR.
inline PGM_P ownerName(uint8_t owner) {
    static const char n0[]  FN_PROGMEM = "none";
    static const char n1[]  FN_PROGMEM = "arduino";
    static const char n2[]  FN_PROGMEM = "irq";
    static const char n3[]  FN_PROGMEM = "timer0";
    static const char n4[]  FN_PROGMEM = "timer1";
    static const char n5[]  FN_PROGMEM = "timer2";
    static const char n6[]  FN_PROGMEM = "tick";
    static const char n7[]  FN_PROGMEM = "pwm";
    static const char n8[]  FN_PROGMEM = "servo";
    static const char n9[]  FN_PROGMEM = "stepper";
    static const char n10[] FN_PROGMEM = "uart";
    static const char n11[] FN_PROGMEM = "spi";
    static const char n12[] FN_PROGMEM = "i2c";
    static const char n13[] FN_PROGMEM = "adc";
    static const char n14[] FN_PROGMEM = "pulse";
    static const char n15[] FN_PROGMEM = "user";
    static const char nx[]  FN_PROGMEM = "?";

    switch (owner) {
        case 0:  return n0;   case 1:  return n1;   case 2:  return n2;
        case 3:  return n3;   case 4:  return n4;   case 5:  return n5;
        case 6:  return n6;   case 7:  return n7;   case 8:  return n8;
        case 9:  return n9;   case 10: return n10;  case 11: return n11;
        case 12: return n12;  case 13: return n13;  case 14: return n14;
        case 15: return n15;  default: return nx;
    }
}

}  // namespace res
}  // namespace fn

// Заявка модуля на вектор. Разворачивается в static_assert — работает
// на уровне файла и внутри функции, ничего не генерирует.
#define FN_CLAIM_VECTOR(vecName, owner)                                       \
    static_assert(::fn::res::vec##vecName == (owner),                         \
                  "Fast_nano: вектор " #vecName " занят другим владельцем. "  \
                  "Освободите его в core/Resources.h (FN_" #vecName "_OWNER).")

// Заявка на таймер целиком. n = 0/1/2.
#define FN_CLAIM_TIMER(n, owner)                                              \
    static_assert(::fn::res::devTimer##n == (owner),                          \
                  "Fast_nano: таймер " #n " занят другим владельцем. "        \
                  "Освободите его в core/Resources.h (FN_TIMER" #n "_DEVICE).")

#define FN_VECTOR_OWNER(vecName) (::fn::res::vec##vecName)
#define FN_VECTOR_IS_FREE(vecName) (::fn::res::vec##vecName == FN_OWNER_NONE)

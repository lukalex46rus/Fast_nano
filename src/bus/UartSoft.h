#pragma once
// ============================================================================
//  UartSoft.h — программный UART TX на любом выводе (только передача)
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Отладочный вывод, когда аппаратный USART занят или нужны два порта.
//    Только TX: программный RX требует PCINT и точного замера полубита,
//    это отдельная задача (см. ограничения). Тайминг в тактах, не в
//    микросекундах — прежняя версия считала bitUs = 1e6/baud и на 115200
//    давала ошибку 8%, связь не работала.
//
//  API
//    template <uint8_t PinNum, uint32_t Baud> class SoftUartTx
//      begin()                                вывод в HIGH (idle)
//      end()                                  вывод в вход без подтяжки
//      writeByte(uint8_t) -> Result
//      write(const uint8_t*, uint16_t) -> Result
//      read(...) -> Unsupported               приёма нет
//      transfer(...) -> Unsupported
//      available() -> 0
//      flush(timeoutMs) -> Ok                 передача синхронная
//      busy() -> false
//      bitCycles() -> uint32_t  constexpr     тактов на бит
//      errorPermille() -> int16_t constexpr   отклонение от заданной скорости
//
//    FN_SOFTUART_NO_IRQ_GUARD  0 (по умолчанию) — критическая секция на байт;
//                              1 — без запрета прерываний (тайминг рвётся)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - По умолчанию каждый байт передаётся с запрещёнными прерываниями.
//      На 9600 это 1.04 мс: приём аппаратного UART в это время теряется
//      (аппаратный буфер 2 байта), millis() недосчитывает тик.
//      Без запрета (FN_SOFTUART_NO_IRQ_GUARD=1) любой ISR разрывает бит
//      и байт приходит битым. Выбор осознанный, третьего варианта нет:
//      программный UART на AVR несовместим с плотными прерываниями.
//    - Практический диапазон: 1200..57600 при 16 МГц. Выше 57600 накладные
//      расходы на вызов и цикл становятся сравнимы с временем бита.
//      На 9600 ошибка от округления тактов < 0.1%, на 115200 — до 1.5%.
//    - Точность зависит от источника тактирования: внутренний RC-генератор
//      Nano даёт до ±1%, кварц — ±50 ppm. При RC на 115200 связь нестабильна.
//    - Приём не реализован. Для RX используйте аппаратный Uart либо
//      отдельный модуль на PCINT с замером полубита (не входит в этот файл).
//    - Вывод должен быть свободен: конфликт с PWM/PCINT того же пина
//      приведёт к искажению. Проверяйте по карте каналов Pwm.h.
//    - Формат фиксирован: 8N1, LSB first, idle HIGH.
//
//  СТОИМОСТЬ (AVR)
//    Ноль байт SRAM. Flash: ~120 байт на инстанцирование (пин + скорость).
//    Каждый уникальный набор <PinNum, Baud> — своя копия кода.
//
//  ПРИМЕР
//    using Debug = fn::SoftUartTx<7, 9600>;    // D7, 9600 бод
//    Debug::begin();
//    fn::bus::writeLine<Debug>(FN_PSTR("hello"));
//    static_assert(Debug::errorPermille() < 10, "скорость недостижима");
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h, core/Registers.h,
//               core/BusContract.h, pin/Board.h, pin/Pin.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"
#include "../core/Registers.h"
#include "../core/BusContract.h"
#include "../pin/Board.h"
#include "../pin/Pin.h"

#include <stdint.h>
#include <util/delay_basic.h>

#if F_USE_UART

#ifndef FN_SOFTUART_NO_IRQ_GUARD
#define FN_SOFTUART_NO_IRQ_GUARD 0
#endif

namespace fn {

template <uint8_t PinNum, uint32_t Baud>
class SoftUartTx {
    static_assert(pinValid(PinNum), "SoftUartTx: номер вывода вне 0..19");
    static_assert(Baud >= 300 && Baud <= 250000, "SoftUartTx: baud вне 300..250000");

    using P = BoardPin<PinNum>;

    // Тактов на бит с округлением к ближайшему.
    static constexpr uint32_t kCycles = (F_CPU + Baud / 2UL) / Baud;
    // Итераций _delay_loop_2 (4 такта на итерацию), минимум 1.
    static constexpr uint32_t kLoops = (kCycles > 4UL) ? (kCycles / 4UL) : 1UL;
    // Накладные расходы на установку бита и вызов задержки, тактов.
    static constexpr uint32_t kOverhead = 12UL;

public:
    static constexpr uint32_t bitCycles() { return kCycles; }

    // Отклонение фактической скорости от заданной, в промилле.
    static constexpr int16_t errorPermille() {
        return int16_t(((int32_t(F_CPU / (kLoops * 4UL + kOverhead)) - int32_t(Baud))
                        * 1000L) / int32_t(Baud));
    }

    static void begin() {
        P::output(true);      // idle HIGH; порядок PORT->DDR внутри regs
    }

    static void end() { P::input(); }

    static Result writeByte(uint8_t b) {
#if FN_SOFTUART_NO_IRQ_GUARD
        sendByte(b);
#else
        AtomicBlock lock;
        sendByte(b);
#endif
        return Result::Ok;
    }

    static Result write(const uint8_t* data, uint16_t len) {
        if (data == nullptr) return Result::BadArg;
        const uint8_t* p = data;
        const uint8_t* end = p + len;
        for (; p != end; ++p) writeByte(*p);
        return Result::Ok;
    }

    static Result read(uint8_t*, uint16_t, uint32_t) { return Result::Unsupported; }
    static Result transfer(const uint8_t*, uint8_t*, uint16_t) { return Result::Unsupported; }
    static uint16_t available() { return 0; }
    static Result flush(uint32_t) { return Result::Ok; }
    static bool busy() { return false; }

private:
    // Задержка на бит с вычетом накладных расходов.
    FN_INLINE static void bitDelay() {
        const uint32_t n = (kCycles > kOverhead) ? ((kCycles - kOverhead) / 4UL) : 1UL;
        _delay_loop_2(uint16_t(n ? n : 1UL));
    }

    static void sendByte(uint8_t b) {
        P::low();                 // start bit
        bitDelay();
        for (uint8_t i = 0; i < 8; ++i) {
            P::write((b & 0x01u) != 0);
            b = uint8_t(b >> 1);
            bitDelay();
        }
        P::high();                // stop bit
        bitDelay();
    }
};

}  // namespace fn

#endif  // F_USE_UART

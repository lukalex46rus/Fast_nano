// ============================================================================
//  Uart.cpp — USART0: расчёт UBRR, приём в буфер через ISR, передача с таймаутом
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация аппаратного UART. Единственный ISR(USART_RX_vect) в проекте.
//    Приём обязателен через прерывание: аппаратный буфер всего 2 байта.
//
//  РАСЧЁТ СКОРОСТИ
//    U2X=1: UBRR = round(F_CPU / (8 * baud)) - 1
//    U2X=0: UBRR = round(F_CPU / (16 * baud)) - 1
//    Округление к ближайшему через (F_CPU + k*baud/2) / (k*baud).
//    Прежняя версия считала U2X=0 двойным целочисленным делением без
//    округления и не проверяла достижимость скорости.
//    Фактическая скорость: F_CPU / (k * (UBRR + 1)).
//
//  ОБРАБОТКА ОШИБОК
//    Флаги FE/DOR/UPE читаются ДО UDR0 (после чтения они сбрасываются).
//    Байт с ошибкой кадра/чётности отбрасывается и считается; байт при DOR
//    сохраняется (он корректен, потерян предыдущий).
//
//  ОГРАНИЧЕНИЯ
//    - Буфер SPSC: ISR пишет head, main читает tail. При FN_UART_RX_SIZE <= 128
//      индексы 8-битные, блокировка не нужна. При 256+ требуется внешняя защита.
//    - Передача блокирующая с таймаутом через fn::expired(): сравнение
//      millis() > deadline ломалось бы на переполнении через 49.7 суток.
//    - Файл должен лежать в src/bus/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Uart.h, core/BusContract.h, timer/Tick.h, avr/io.h, interrupt.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_UART

#include "Uart.h"
#include "../timer/Tick.h"

#include <avr/io.h>
#include <avr/interrupt.h>

// Вектор принадлежит ядру Arduino по умолчанию. Для fn::Uart нужно явно
// задать FN_USART_RX_OWNER=FN_OWNER_UART централизованно и отказаться от Serial.
static_assert(FN_USART_RX_OWNER == FN_OWNER_UART,
              "fn::Uart: задайте FN_USART_RX_OWNER=FN_OWNER_UART и не используйте Serial");
FN_CLAIM_VECTOR(UsartRx, FN_OWNER_UART);

namespace fn {
namespace uart_detail {

UartRxBuffer rx;

volatile uint16_t dorCount = 0;
volatile uint16_t feCount = 0;
volatile uint16_t upeCount = 0;

uint32_t wantedBaud = 0;
uint32_t realBaud = 0;
bool isReady = false;

// Расчёт делителя с округлением. Возвращает false, если скорость недостижима.
bool calcUbrr(uint32_t baud, bool u2x, uint16_t& ubrrOut, uint32_t& realOut) {
    if (baud == 0) return false;
    const uint32_t k = u2x ? 8UL : 16UL;
    // round(F_CPU / (k * baud)) без переполнения: F_CPU <= 16e6, k*baud <= 16e6.
    const uint32_t denom = k * baud;
    const uint32_t quo = (F_CPU + denom / 2UL) / denom;
    if (quo == 0 || quo > 4096UL) return false;
    ubrrOut = uint16_t(quo - 1UL);
    realOut = F_CPU / (k * quo);
    return true;
}

}  // namespace uart_detail

// ============================ инициализация ================================
Result Uart::begin(const Config& cfg) {
    using namespace uart_detail;

    uint16_t ubrr = 0;
    uint32_t real = 0;
    bool u2x = cfg.doubleSpeed;

    if (!calcUbrr(cfg.baud, u2x, ubrr, real)) {
        // Слишком низкая скорость для U2X — пробуем обычный режим.
        u2x = false;
        if (!calcUbrr(cfg.baud, u2x, ubrr, real)) return Result::OutOfRange;
    }

    {
        AtomicBlock lock;
        wantedBaud = cfg.baud;
        realBaud = real;

        UBRR0H = uint8_t(ubrr >> 8);
        UBRR0L = uint8_t(ubrr);

        uint8_t a = u2x ? uint8_t(1u << U2X0) : 0;
        UCSR0A = a;

        uint8_t c = uint8_t((1u << UCSZ01) | (1u << UCSZ00));   // 8 бит
        if (cfg.parity == Parity::Even) c = uint8_t(c | (1u << UPM01));
        else if (cfg.parity == Parity::Odd) c = uint8_t(c | (1u << UPM01) | (1u << UPM00));
        if (cfg.stop == StopBits::Two) c = uint8_t(c | (1u << USBS0));
        UCSR0C = c;

        uint8_t b = 0;
        if (cfg.txEnable) b = uint8_t(b | (1u << TXEN0));
        if (cfg.rxEnable) b = uint8_t(b | (1u << RXEN0) | (1u << RXCIE0));
        UCSR0B = b;

        rx.clear();
        rx.clearDropped();
        dorCount = 0;
        feCount = 0;
        upeCount = 0;
        isReady = true;
    }

    // Отклонение сверх порога — предупреждаем кодом, но настройку оставляем.
    const int16_t err = baudErrorPermille();
    const int16_t abs = (err < 0) ? int16_t(-err) : err;
    if (abs > FN_UART_MAX_ERR_PERMILLE) return Result::OutOfRange;
    return Result::Ok;
}

Result Uart::begin(uint32_t baud) { return begin(Config(baud)); }

void Uart::end() {
    AtomicBlock lock;
    UCSR0B = 0;
    UCSR0A = 0;
    UCSR0C = 0;
    uart_detail::isReady = false;
    uart_detail::rx.clear();
}

bool Uart::ready() { return uart_detail::isReady; }

// ============================ передача =====================================
Result Uart::writeByte(uint8_t b) {
    if (!uart_detail::isReady) return Result::NotReady;
    const Result r = bus::waitUntil(
        [] { return (UCSR0A & (1u << UDRE0)) != 0; }, FN_UART_TX_TIMEOUT_MS);
    if (!ok(r)) return r;
    UDR0 = b;
    return Result::Ok;
}

Result Uart::write(const uint8_t* data, uint16_t len) {
    if (data == nullptr) return Result::BadArg;
    if (!uart_detail::isReady) return Result::NotReady;
    const uint8_t* p = data;
    const uint8_t* end = p + len;
    for (; p != end; ++p) {
        const Result r = writeByte(*p);
        if (!ok(r)) return r;
    }
    return Result::Ok;
}

Result Uart::flush(uint32_t timeoutMs) {
    if (!uart_detail::isReady) return Result::NotReady;
    return bus::waitUntil(
        [] { return (UCSR0A & (1u << TXC0)) != 0; }, timeoutMs);
}

bool Uart::busy() {
    return uart_detail::isReady && ((UCSR0A & (1u << TXC0)) == 0);
}

// ============================ приём ========================================
uint16_t Uart::available() { return uart_detail::rx.available(); }

int16_t Uart::readByte() {
    uint8_t b = 0;
    return uart_detail::rx.pop(b) ? int16_t(b) : int16_t(-1);
}

int16_t Uart::peekByte() {
    uint8_t b = 0;
    return uart_detail::rx.peek(b) ? int16_t(b) : int16_t(-1);
}

Result Uart::read(uint8_t* dst, uint16_t len, uint32_t timeoutMs) {
    if (dst == nullptr) return Result::BadArg;
    if (!uart_detail::isReady) return Result::NotReady;
    if (len == 0) return Result::Ok;

    const uint32_t t0 = ::fn::millis();
    uint16_t got = 0;
    while (got < len) {
        uint8_t b = 0;
        if (uart_detail::rx.pop(b)) {
            dst[got++] = b;
            continue;
        }
        if (elapsed(t0, ::fn::millis()) >= timeoutMs) return Result::Timeout;
    }
    return Result::Ok;
}

Result Uart::transfer(const uint8_t*, uint8_t*, uint16_t) {
    return Result::Unsupported;
}

void Uart::clearRx() { uart_detail::rx.clear(); }

// ============================ диагностика ==================================
uint16_t Uart::dropped() { return uart_detail::rx.dropped(); }

uint16_t Uart::overruns() {
    AtomicBlock lock;
    return uart_detail::dorCount;
}
uint16_t Uart::frameErrors() {
    AtomicBlock lock;
    return uart_detail::feCount;
}
uint16_t Uart::parityErrors() {
    AtomicBlock lock;
    return uart_detail::upeCount;
}

void Uart::clearStats() {
    AtomicBlock lock;
    uart_detail::dorCount = 0;
    uart_detail::feCount = 0;
    uart_detail::upeCount = 0;
    uart_detail::rx.clearDropped();
}

uint32_t Uart::actualBaud() { return uart_detail::realBaud; }

int16_t Uart::baudErrorPermille() {
    const uint32_t want = uart_detail::wantedBaud;
    if (want == 0) return 0;
    const int32_t diff = int32_t(uart_detail::realBaud) - int32_t(want);
    return int16_t((diff * 1000L) / int32_t(want));
}

// ============================ прямой доступ ================================
void Uart::enableRxIrq()  { AtomicBlock l; UCSR0B = uint8_t(UCSR0B | (1u << RXCIE0)); }
void Uart::disableRxIrq() { AtomicBlock l; UCSR0B = uint8_t(UCSR0B & uint8_t(~(1u << RXCIE0))); }
void Uart::enableTxIrq()  { AtomicBlock l; UCSR0B = uint8_t(UCSR0B | (1u << UDRIE0)); }
void Uart::disableTxIrq() { AtomicBlock l; UCSR0B = uint8_t(UCSR0B & uint8_t(~(1u << UDRIE0))); }

}  // namespace fn

// ============================================================================
//  ISR — единственный в проекте для вектора USART_RX
// ============================================================================
ISR(USART_RX_vect) {
    using namespace fn::uart_detail;

    // Флаги ошибок читаются ДО UDR0: чтение данных их сбрасывает.
    const uint8_t status = UCSR0A;
    const uint8_t data = UDR0;

    if (status & (1u << DOR0))  ++dorCount;
    if (status & (1u << FE0))   { ++feCount; return; }   // кадр битый — отбросить
    if (status & (1u << UPE0))  { ++upeCount; return; }  // чётность битая — отбросить

    rx.push(data);   // при переполнении увеличит dropped()
}

#endif  // F_USE_UART

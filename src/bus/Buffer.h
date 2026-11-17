#pragma once
// ============================================================================
//  Buffer.h — кольцевой буфер для SPSC (ISR -> main), без динамической памяти
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Приёмный буфер UART и любые потоки «один производитель, один потребитель».
//    Индексы монотонные (не сворачиваются в 0..N-1): разность head-tail корректно
//    различает пусто и полно без отдельного счётчика. Подход сохранён из
//    исходной версии — он верен и экономит байт состояния.
//
//  API
//    template <typename T, uint16_t N> class RingBuffer
//      capacity() -> uint16_t              constexpr, равно N
//      available() -> uint16_t             сколько элементов можно прочитать
//      free() -> uint16_t                  сколько можно записать
//      empty() / full() -> bool
//      push(const T&) -> bool              false: полон, элемент не записан
//      pop(T&) -> bool                     false: пуст, приёмник не изменён
//      peek(T&) -> bool                    подсмотреть без извлечения
//      write(const T* src, uint16_t n) -> uint16_t   фактически записано
//      read(T* dst, uint16_t n) -> uint16_t          фактически прочитано
//      clear()                             сброс (под AtomicBlock)
//      dropped() -> uint16_t               счётчик отказов push (переполнений)
//      clearDropped()
//
//    using ByteRing<N> = RingBuffer<uint8_t, N>
//    using UartRxBuffer = ByteRing<FN_UART_RX_SIZE>   (по умолчанию 64)
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - N ОБЯЗАНО быть степенью двойки (static_assert). Причина: позиция
//      считается как index & (N-1). Остаток от деления на нечётное N — это
//      вызов __udivqi3 (~40 байт flash и десятки тактов) НА КАЖДЫЙ БАЙТ.
//      Прежняя версия использовала % N и платила эту цену при N=100.
//    - Индексы 8-битные при N <= 128, 16-битные при N >= 256. Монотонный
//      8-битный индекс переполняется через 256 элементов — это корректно,
//      разность по модулю 256 даёт верный результат при N <= 128.
//      При N = 256 нужен 16-битный индекс, иначе полное состояние неотличимо
//      от пустого.
//    - SPSC-контракт: push/write/dropped пишет ТОЛЬКО производитель,
//      pop/read — ТОЛЬКО потребитель. При 8-битных индексах блокировка не
//      нужна: каждый индекс имеет ровно одного писателя, чтение байта
//      атомарно. При 16-битных индексах (N >= 256) обмен требует внешней
//      защиты: оберните вызовы в fn::AtomicBlock или FN_ATOMIC.
//    - pushOverwrite УДАЛЁН намеренно: он двигал _tail, которым владеет
//      потребитель, и ломал SPSC. Нужна политика «затирать старое» —
//      делайте это в потребителе: if (full()) pop(dummy); push(v);
//    - dropped() считает отказы push. Это диагностика потери байтов:
//      растёт — увеличивайте буфер или разгружайте main.
//    - sizeof(RingBuffer<uint8_t,64>) = 64 + 2 индекса + 2 счётчик = 68 байт.
//      При 115200 бод буфер 64 байта даёт 5.5 мс запаса; для тяжёлого main
//      берите 128 (11 мс).
//    - clear() сбрасывает оба индекса под AtomicBlock: вызывать при
//      остановленном производителе либо смириться с потерей байта в окне.
//
//  СТОИМОСТЬ (AVR)
//    N * sizeof(T) + 2*sizeof(Index) + 2 байта. Никаких таблиц и вызовов
//    деления. push/pop при 8-битных индексах — 6-10 машинных команд.
//
//  ПРИМЕР
//    fn::UartRxBuffer rx;                 // 64 байта, SPSC
//
//    ISR(USART_RX_vect) {                 // производитель
//        rx.push(UDR0);                   // без блокировки: индексы 8-битные
//    }
//
//    void loop() {                        // потребитель
//        uint8_t b;
//        while (rx.pop(b)) handle(b);
//        if (rx.dropped()) { /* потеря байтов */ rx.clearDropped(); }
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Atomic.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Atomic.h"

#include <stdint.h>

#if F_USE_BUFFER

#ifndef FN_UART_RX_SIZE
#define FN_UART_RX_SIZE 64
#endif

namespace fn {

template <typename T, uint16_t N>
class RingBuffer {
    static_assert(N > 0, "RingBuffer: N должно быть больше нуля");
    static_assert((N & (N - 1)) == 0,
                  "RingBuffer: N должно быть степенью двойки (иначе деление на байт)");
    static_assert(N <= 32768u, "RingBuffer: N слишком велико для uint16 индексов");

    // 8-битный индекс достаточен при N <= 128: разность по модулю 256
    // однозначно различает пусто (0) и полно (N).
    using Index = typename detail::Select<(N <= 128u), uint8_t, uint16_t>::type;

public:
    constexpr uint16_t capacity() const { return N; }

    uint16_t available() const {
        const Index h = _head;
        const Index t = _tail;
        return static_cast<uint16_t>(static_cast<Index>(h - t));
    }

    uint16_t free() const { return static_cast<uint16_t>(N - available()); }
    bool empty() const { return available() == 0; }
    bool full() const { return available() >= N; }

    // Производитель. false — буфер полон, элемент не записан.
    bool push(const T& v) {
        if (full()) {
            _dropped = static_cast<uint16_t>(_dropped + 1u);
            return false;
        }
        const Index h = _head;
        _buf[static_cast<uint16_t>(h) & (N - 1u)] = v;
        _head = static_cast<Index>(h + 1);
        return true;
    }

    // Потребитель. false — буфер пуст, приёмник не изменён.
    bool pop(T& v) {
        if (empty()) return false;
        const Index t = _tail;
        v = _buf[static_cast<uint16_t>(t) & (N - 1u)];
        _tail = static_cast<Index>(t + 1);
        return true;
    }

    bool peek(T& v) const {
        if (empty()) return false;
        const Index t = _tail;
        v = _buf[static_cast<uint16_t>(t) & (N - 1u)];
        return true;
    }

    uint16_t write(const T* src, uint16_t n) {
        if (src == nullptr || n == 0) return 0;
        uint16_t accepted = 0;
        while (accepted < n && push(src[accepted])) ++accepted;
        return accepted;
    }

    uint16_t read(T* dst, uint16_t n) {
        if (dst == nullptr || n == 0) return 0;
        uint16_t count = 0;
        while (count < n && pop(dst[count])) ++count;
        return count;
    }

    void clear() {
        AtomicBlock lock;
        _head = 0;
        _tail = 0;
    }

    uint16_t dropped() const {
        AtomicBlock lock;
        return _dropped;
    }

    void clearDropped() {
        AtomicBlock lock;
        _dropped = 0;
    }

private:
    T _buf[N];
    volatile Index _head = 0;      ///< пишет только производитель
    volatile Index _tail = 0;      ///< пишет только потребитель
    volatile uint16_t _dropped = 0;///< пишет производитель, читает потребитель
};

template <uint16_t N>
using ByteRing = RingBuffer<uint8_t, N>;

using UartRxBuffer = ByteRing<FN_UART_RX_SIZE>;

}  // namespace fn

#endif  // F_USE_BUFFER

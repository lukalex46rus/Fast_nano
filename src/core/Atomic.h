#pragma once
// ============================================================================
//  Atomic.h — критические секции и атомарные обращения
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Защита read-modify-write и многобайтовых обращений от прерываний.
//    На 8-битном AVR чтение/запись uint16_t и uint32_t идёт двумя-четырьмя
//    командами, ISR может вклиниться между ними.
//
//  API
//    class AtomicBlock                RAII: SREG -> cli -> барьер -> ... ->
//                                     барьер -> SREG
//    FN_ATOMIC(body)                  то же макросом с областью видимости
//    atomicRead8/16/32(p)             атомарное чтение
//    atomicWrite8/16/32(p, v)         атомарная запись
//    atomicModify(p, f) -> T          newValue = f(old) под защитой
//    atomicExchange(p, v) -> T        записать, вернуть прежнее
//    class AtomicCounter<T>           inc/dec/add/sub/get/set/exchange
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - 8-битные чтения и записи атомарны АППАРАТНО (одна LDS/STS).
//      atomicRead8/atomicWrite8 не запрещают прерывания: экономия ~6 тактов.
//      Защищать нужно RMW (|=, ++, +=), а не одиночный доступ.
//    - Барьеры памяти обязательны: без них компилятор переносит обращения
//      к обычным (не volatile) переменным за границы блока.
//    - Безымянный AtomicBlock() уничтожается сразу и НЕ защищает ничего.
//      Всегда давайте имя: AtomicBlock lock;
//    - AtomicCounter: все RMW под блокировкой, включая 8-битные. Байтовый
//      RMW — это LDS/OR/STS, ISR по тому же адресу его рвёт. Если писатель
//      ровно один (только ISR), используйте голый volatile-счётчик напрямую.
//    - Внутри блока держать код коротким: длинные вычисления задерживают
//      прерывания и ломают тайминги UART/таймеров.
//    - T только uint8_t/uint16_t/uint32_t.
//
//  СТОИМОСТЬ (AVR)
//    AtomicBlock: in SREG, cli, ..., out SREG ≈ 4 такта накладных.
//    atomicRead8/Write8: одна команда, накладных нет.
//
//  ПРИМЕР
//    volatile uint32_t g_us;                       // обновляется в ISR
//    uint32_t now = fn::atomicRead32(&g_us);
//    void bump() { fn::AtomicBlock lock; g_shared += 5; }
//    fn::AtomicCounter<uint16_t> events;
//    ISR(PCINT0_vect) { events.inc(); }
//    uint16_t n = events.exchange(0);
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, avr/io.h, avr/interrupt.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>

#if F_USE_ATOMIC

namespace fn {

class AtomicBlock {
public:
    AtomicBlock() : _sreg(SREG) { cli(); FN_BARRIER(); }
    ~AtomicBlock() { FN_BARRIER(); SREG = _sreg; }
    AtomicBlock(const AtomicBlock&) = delete;
    AtomicBlock& operator=(const AtomicBlock&) = delete;
private:
    uint8_t _sreg;
};

#define FN_ATOMIC(body)            \
    do {                           \
        ::fn::AtomicBlock _fnLock; \
        body;                      \
    } while (0)

// 8 бит: одна машинная команда, защита не нужна.
inline uint8_t atomicRead8(const volatile uint8_t* p) { return *p; }
inline void atomicWrite8(volatile uint8_t* p, uint8_t v) { *p = v; }

inline uint16_t atomicRead16(const volatile uint16_t* p) { AtomicBlock l; return *p; }
inline void atomicWrite16(volatile uint16_t* p, uint16_t v) { AtomicBlock l; *p = v; }

inline uint32_t atomicRead32(const volatile uint32_t* p) { AtomicBlock l; return *p; }
inline void atomicWrite32(volatile uint32_t* p, uint32_t v) { AtomicBlock l; *p = v; }

template <typename T, typename F>
inline T atomicModify(volatile T* p, F f) {
    AtomicBlock lock;
    const T nv = f(*p);
    *p = nv;
    return nv;
}

template <typename T>
inline T atomicExchange(volatile T* p, T v) {
    AtomicBlock lock;
    const T old = *p;
    *p = v;
    return old;
}

template <typename T>
class AtomicCounter {
    static_assert(detail::IsBitType<T>::value,
                  "fn::AtomicCounter<T>: T = uint8_t / uint16_t / uint32_t");
public:
    AtomicCounter() : _v(0) {}
    explicit AtomicCounter(T v) : _v(v) {}

    void inc() { add(T(1)); }
    void dec() { sub(T(1)); }
    void add(T d) { AtomicBlock l; _v = T(_v + d); }
    void sub(T d) { AtomicBlock l; _v = T(_v - d); }

    T get() const { AtomicBlock l; return _v; }
    void set(T v) { AtomicBlock l; _v = v; }
    T exchange(T v) { AtomicBlock l; const T o = _v; _v = v; return o; }

private:
    volatile T _v;
};

}  // namespace fn

#endif  // F_USE_ATOMIC
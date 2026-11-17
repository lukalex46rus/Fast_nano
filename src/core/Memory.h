#pragma once
// ============================================================================
//  Memory.h — диагностика SRAM и обёртки над mem*
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    На 2 КБ SRAM переполнение стека — основная причина «плавающих» отказов.
//    Модуль даёт инструменты увидеть проблему до проявления: свободный зазор,
//    глубину стека по покраске, раскладку памяти.
//
//  API (namespace fn::mem)
//    freeRam()        -> uint16_t  зазор между кучей и текущим стеком
//    heapUsed()       -> uint16_t  байт в куче (malloc)
//    heapTop()        -> uint16_t  адрес вершины кучи
//    stackPointer()   -> uint16_t  текущий SP (атомарно)
//    stackPaint()     -> uint16_t  закрасить свободную RAM, вернуть размер
//    stackUsed()      -> uint16_t  глубина стека от вершины SRAM (см. ниже)
//    stackCollision() -> bool      куча дошла до стека
//    struct RamReport { dataBss, heap, free, stackUsed, total }
//    report()         -> RamReport
//
//    memZero / memFill / memCopy / memMove / memCopyP / memEqual
//    StackPaintByte = 0xA5, StackPaintGuard = 64
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - freeRam() — расстояние от вершины кучи до ТЕКУЩЕГО кадра стека.
//      При коллизии возвращает 0, а не огромное число.
//    - stackPaint() красит от вершины кучи до (SP - StackPaintGuard).
//      Верхние ~64 байта не покрашены намеренно: там кадр самой функции
//      и кадр вызывающего, их затирание разрушило бы адрес возврата.
//    - Граница покраски запоминается в paintTop (2 байта SRAM). Без неё
//      stackUsed() стартовал бы от SramEnd и сразу упирался в непокрашенный
//      байт — результат был бы нулевым почти всегда. Прежняя версия именно
//      так и работала; шапка обещала «занижение на 64», фактически был ноль.
//    - stackUsed() считает от ВЕРШИНЫ SRAM до первого незатёртого паттерна,
//      то есть включает непокрашенный guard. Это нижняя граница реальной
//      глубины: стек мог заходить в guard-зону, но мы её не красили.
//      До первого stackPaint() возвращает 0.
//    - stackPaint() вызывать один раз в начале setup(), до крупных локальных
//      буферов. Повторная покраска сбрасывает статистику.
//    - malloc на 2 КБ не рекомендуется: фрагментация. Модуль работает и без
//      кучи (__brkval == 0).
//    - Чтение SP под AtomicBlock: ISR между SPL и SPH даст разорванное значение.
//
//  СТОИМОСТЬ (AVR)
//    SRAM: 2 байта (paintTop). Flash: десятки байт на функцию, неиспользуемые
//    вырезаются линкером. Таблиц нет.
//
//  ПРИМЕР
//    void setup() { fn::mem::stackPaint(); fn::Uart::begin(9600); }
//    void loop() {
//        doWork();
//        if (fn::mem::stackUsed() > 1500) { /* близко к пределу */ }
//    }
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h; реализация — Memory.cpp
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"

#include <stdint.h>
#include <string.h>
#include <avr/pgmspace.h>

#if F_USE_MEMORY

namespace fn {
namespace mem {

constexpr uint8_t StackPaintByte = 0xA5u;
constexpr uint16_t StackPaintGuard = 64u;

uint16_t freeRam();
uint16_t heapUsed();
uint16_t heapTop();
uint16_t stackPointer();
uint16_t stackPaint();
uint16_t stackUsed();
bool stackCollision();

struct RamReport {
    uint16_t dataBss;
    uint16_t heap;
    uint16_t free;
    uint16_t stackUsed;
    uint16_t total;
};

RamReport report();

inline void memZero(void* dst, uint16_t n) { if (dst && n) memset(dst, 0, n); }
inline void memFill(void* dst, uint8_t v, uint16_t n) { if (dst && n) memset(dst, v, n); }
inline void memCopy(void* d, const void* s, uint16_t n) { if (d && s && n) memcpy(d, s, n); }
inline void memMove(void* d, const void* s, uint16_t n) { if (d && s && n) memmove(d, s, n); }
inline void memCopyP(void* d, PGM_VOID_P s, uint16_t n) { if (d && s && n) memcpy_P(d, s, n); }
inline bool memEqual(const void* a, const void* b, uint16_t n) {
    if (!a || !b) return a == b;
    return memcmp(a, b, n) == 0;
}

}  // namespace mem

inline uint16_t freeRam() { return mem::freeRam(); }
inline void memZero(void* d, uint16_t n) { mem::memZero(d, n); }
inline void memCopy(void* d, const void* s, uint16_t n) { mem::memCopy(d, s, n); }
inline void memCopyP(void* d, PGM_VOID_P s, uint16_t n) { mem::memCopyP(d, s, n); }

}  // namespace fn

#endif  // F_USE_MEMORY
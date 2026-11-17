// ============================================================================
//  Memory.cpp — диагностика SRAM через символы линкера и регистр SP
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Замеры памяти. Единственное статическое состояние — граница покраски
//    paintTop (2 байта): без неё stackUsed() не знает, откуда начинать скан.
//
//  ПОКРАСКА И ЗАМЕР
//    Красим [heapEnd, SP - Guard). Guard защищает кадр этой функции и кадр
//    вызывающего от затирания. paintTop запоминает верхнюю границу.
//    stackUsed() сканирует вниз ОТ paintTop и прибавляет непокрашенный
//    хвост (SramEnd - paintTop): стек заведомо доходил до paintTop, иначе
//    покраска бы туда не достала.
//
//  ЗАЩИТА ОТ КОЛЛИЗИИ
//    Если куча поднялась выше текущего кадра стека, разность отрицательна.
//    Прежняя версия приводила её к uint16_t и возвращала огромное число.
//    Теперь 0 + явный stackCollision().
//
//  ОГРАНИЧЕНИЯ
//    - paintTop живёт в .bss, обнуляется при старте: до stackPaint()
//      stackUsed() возвращает 0, а не мусор.
//    - Файл должен лежать в src/core/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Memory.h, Atomic.h, avr/io.h
// ============================================================================

#include "Config.h"

#if F_USE_MEMORY

#include "Memory.h"
#include "Atomic.h"

#include <avr/io.h>

extern "C" char* __brkval;
extern "C" char __heap_start;
extern "C" char __data_start;
extern "C" char __bss_end;

namespace fn {
namespace mem {
namespace {

uint8_t* paintTop = nullptr;      ///< верхняя граница покрашенной области

inline uint16_t readSp() {
    AtomicBlock lock;
    return SP;
}

inline char* heapEnd() { return __brkval ? __brkval : &__heap_start; }

}  // namespace

uint16_t stackPointer() { return readSp(); }

uint16_t heapTop() {
    return static_cast<uint16_t>(reinterpret_cast<uintptr_t>(heapEnd()));
}

uint16_t heapUsed() {
    return static_cast<uint16_t>(heapEnd() - &__heap_start);
}

uint16_t freeRam() {
    char marker;
    const char* stack = &marker;
    const char* heap = heapEnd();
    if (stack <= heap) return 0;
    return static_cast<uint16_t>(stack - heap);
}

bool stackCollision() {
    char marker;
    return &marker <= heapEnd();
}

uint16_t stackPaint() {
    uint8_t* p = reinterpret_cast<uint8_t*>(heapEnd());
    uint8_t* end = reinterpret_cast<uint8_t*>(readSp());
    if (end <= p + StackPaintGuard) {
        paintTop = nullptr;
        return 0;
    }
    end -= StackPaintGuard;
    const uint16_t n = static_cast<uint16_t>(end - p);
    memset(p, StackPaintByte, n);
    paintTop = end;
    return n;
}

uint16_t stackUsed() {
    if (paintTop == nullptr) return 0;      // покраски не было

    const uint8_t* p = paintTop;
    const uint8_t* low = reinterpret_cast<const uint8_t*>(heapEnd());
    uint16_t untouched = 0;
    while (p > low && *p == StackPaintByte) {
        --p;
        ++untouched;
    }
    // Непокрашенный хвост сверху: стек точно доходил до paintTop.
    const uint16_t guardZone =
        static_cast<uint16_t>(SramEnd - reinterpret_cast<uintptr_t>(paintTop));
    const uint16_t painted = static_cast<uint16_t>(paintTop - low);
    return static_cast<uint16_t>(guardZone + (painted - untouched));
}

RamReport report() {
    RamReport r;
    r.dataBss = static_cast<uint16_t>(&__bss_end - &__data_start);
    r.heap = heapUsed();
    r.free = freeRam();
    r.stackUsed = stackUsed();
    r.total = RamSize;
    return r;
}

}  // namespace mem
}  // namespace fn

#endif  // F_USE_MEMORY
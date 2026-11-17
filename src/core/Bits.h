#pragma once
// ============================================================================
//  Bits.h — umbrella битового слоя + compile-time самотест
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Одно подключение всего битового слоя. Для экономии времени сборки
//    подключайте нужные модули напрямую — они самодостаточны:
//      BitMask.h     конфигурация слоя, маски, одиночные биты
//      BitScan.h     ctz / ffs / bitWidth
//      BitCount.h    popcount / parity
//      BitOps.h      битовые поля            (F_USE_BIT_OPS)
//      BitShift.h    повороты, перестановки
//      BitReverse.h  реверс бит              (F_USE_BIT_REVERSE)
//      BitPack.h     BitPack<N>, BitPackWide (F_USE_BIT_PACK)
//      BitSet.h      BitSet<N>, BitSetWide   (F_USE_BIT_SET)
//      BitFlags.h    флаги в скаляре         (F_USE_BIT_FLAGS)
//
//  САМОТЕСТ
//    FN_BITS_SELFTEST = 1 (по умолчанию) включает static_assert-набор ниже.
//    Проверяет семантику на этапе компиляции, не занимает ни байта flash/RAM.
//    Падение здесь означает сломанную конфигурацию или трейты, а не логику скетча.
//
//  ОГРАНИЧЕНИЯ
//    - Таблицы слоя лежат в Tables.cpp: файл должен быть в проекте.
//    - Конфигурация (FN_BITS_*) задаётся в BitMask.h централизованно для всех TU.
//    - BitPackWide / BitSetWide не владеют буфером и копируются как указатель.
//
//  ПРИМЕР
//    #include "Bits.h"
//    fn::BitSet<24> events;
//    fn::BitFlags8  state;
//    void setup() {
//        events.set(3);
//        events.setBits(8, 4, 0x0A);
//        state.writeField(fn::maskRange8(2, 0), 5);
//    }
//    void loop() {
//        events.forEachSet([](uint16_t i) { events.clear(i); });
//    }
//
//  ЗАВИСИМОСТИ: все модули битового слоя
// ============================================================================

#include "BitMask.h"
#include "BitScan.h"
#include "BitCount.h"
#include "BitOps.h"
#include "BitShift.h"
#include "BitReverse.h"
#include "BitPack.h"
#include "BitSet.h"
#include "BitFlags.h"

#if FN_BITS_SELFTEST
namespace fn {
namespace selftest {

// --- маски ---
static_assert(bitMask8(0) == 0x01, "bitMask8");
static_assert(bitMask8(7) == 0x80, "bitMask8");
static_assert(bitMask8(8) == 0x01, "bitMask8 wrap");
static_assert(maskLo8(0) == 0x00, "maskLo8(0)");
static_assert(maskLo8(3) == 0x07, "maskLo8(3)");
static_assert(maskLo8(8) == 0xFF, "maskLo8 clamp");
static_assert(maskLo8(200) == 0xFF, "maskLo8 clamp");
static_assert(maskRange8(5, 2) == 0x3C, "maskRange8");
static_assert(maskRange8(2, 5) == 0x00, "maskRange8 inverted");
static_assert(bytesNeeded(1) == 1, "bytesNeeded");
static_assert(bytesNeeded(8) == 1, "bytesNeeded");
static_assert(bytesNeeded(9) == 2, "bytesNeeded");
static_assert(bytesNeeded(65535) == 8192, "bytesNeeded no overflow");
static_assert(hasSingleBit(uint8_t(0x10)), "hasSingleBit");
static_assert(!hasSingleBit(uint8_t(0x11)), "hasSingleBit");

// --- сканирование ---
static_assert(ctz8(0) == 8, "ctz8(0)");
static_assert(ctz8(0x08) == 3, "ctz8");
static_assert(ctz16(0x0100) == 8, "ctz16");
static_assert(ctz16(0) == 16, "ctz16(0)");
static_assert(ctz32(0x00010000UL) == 16, "ctz32");
static_assert(ctz32(0) == 32, "ctz32(0)");
static_assert(ffs8(0x10) == 5, "ffs8");
static_assert(bitWidth8(0x80) == 8, "bitWidth8");
static_assert(bitWidth8(0) == 0, "bitWidth8(0)");
static_assert(bitWidth16(0x0100) == 9, "bitWidth16");

// --- popcount ---
static_assert(popcount8c(0x00) == 0, "popcount8c");
static_assert(popcount8c(0xA5) == 4, "popcount8c");
static_assert(popcount8c(0xFF) == 8, "popcount8c");

// --- повороты и реверс ---
static_assert(rotl8(0x81, 1) == 0x03, "rotl8");
static_assert(rotr8(0x03, 1) == 0x81, "rotr8");
static_assert(rotl8(0xA5, 0) == 0xA5, "rotl8 n=0");
static_assert(swapNibbles8(0x12) == 0x21, "swapNibbles8");
static_assert(swapBytes16(0x1234) == 0x3412, "swapBytes16");
#if F_USE_BIT_REVERSE
static_assert(bitrev8c(0x01) == 0x80, "bitrev8c");
static_assert(bitrev8c(0xA5) == 0xA5, "bitrev8c palindrome");
#endif

// --- поля ---
#if F_USE_BIT_OPS
static_assert(bitfGet(uint8_t(0xA5), uint8_t(0x0E)) == 2, "bitfGet8");
static_assert(bitfGet(uint8_t(0xA5), uint8_t(0x00)) == 0, "bitfGet8 mask=0");
static_assert(bitfMake(uint8_t(0x03), uint8_t(0x30)) == 0x30, "bitfMake8");
static_assert(bitfMake(uint8_t(0xFF), uint8_t(0x30)) == 0x30, "bitfMake8 clip");
static_assert(bitfSet(uint8_t(0xFF), uint8_t(0x00), uint8_t(0x0F)) == 0xF0, "bitfSet8");
static_assert(bitfToggle(uint8_t(0x0F), uint8_t(0x03)) == 0x0C, "bitfToggle8");
static_assert(bitfAll(uint8_t(0x0F), uint8_t(0x03)), "bitfAll8");
static_assert(bitfNone(uint8_t(0xF0), uint8_t(0x03)), "bitfNone8");
static_assert(bitfGetS(uint8_t(0x0F), uint8_t(0x0F)) == 0xFF, "bitfGetS8 -1");
static_assert(bitfGetS(uint8_t(0x07), uint8_t(0x0F)) == 0x07, "bitfGetS8 +7");
static_assert(bitfWidth(uint8_t(0x38)) == 3, "bitfWidth");
static_assert(bitfGet(uint16_t(0x1234), 0x0F00) == 0x02, "bitfGet16");
static_assert(bitfGet(uint32_t(0x12345678UL), 0x00FF0000UL) == 0x34, "bitfGet32");
#endif

// --- геометрия паков и множеств ---
#if F_USE_BIT_PACK
static_assert(BitPack<1>::sizeBytes() == 1, "BitPack<1>");
static_assert(BitPack<8>::tailMask() == 0xFF, "tailMask aligned");
static_assert(BitPack<10>::sizeBytes() == 2, "BitPack<10>");
static_assert(BitPack<10>::tailMask() == 0x03, "tailMask partial");
static_assert(BitPack<10>::amount() == 10, "amount");
static_assert(sizeof(BitPack<10>) == 2, "sizeof BitPack");
static_assert(sizeof(BitPackWide) == 7, "sizeof BitPackWide");
#endif

#if F_USE_BIT_SET
static_assert(sizeof(BitSet<10>) == 2, "sizeof BitSet (EBO)");
static_assert(sizeof(BitSetWide) == 7, "sizeof BitSetWide (EBO)");
#endif

#if F_USE_BIT_FLAGS
static_assert(sizeof(BitFlags8) == 1, "sizeof BitFlags8");
static_assert(sizeof(BitFlags16) == 2, "sizeof BitFlags16");
static_assert(sizeof(BitFlags32) == 4, "sizeof BitFlags32");
#endif

}  // namespace selftest
}  // namespace fn
#endif  // FN_BITS_SELFTEST

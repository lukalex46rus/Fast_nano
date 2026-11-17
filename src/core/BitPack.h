#pragma once
// ============================================================================
//  BitPack.h — битовые массивы: BitPack<N>, BitPackWide и общее CRTP-ядро
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Хранилище битов и полный набор операций. Вся логика в
//    detail::BitPackCore<D> (CRTP): наследники дают только хранилище и
//    метаданные, поэтому статический и динамический варианты не дублируют
//    код и имеют одинаковый API.
//      BitPack<N>   своё хранилище, размер известен компилятору, проверки
//                   границ и маска хвоста сворачиваются в константы;
//      BitPackWide  вид (view) на внешний буфер, размер в рантайме.
//
//  РАСКЛАДКА БИТ
//    Бит i лежит в байте data()[i >> 3], позиция (i & 7), нумерация от
//    младшего бита (LSB-first). Байты идут по возрастанию адресов.
//
//  ИНВАРИАНТ
//    Биты за пределами amount() в последнем байте ВСЕГДА нулевые. Его
//    держат setAll / writeAll / invert / shift* / reverse* / copyFrom /
//    assign и операторы BitSet. При прямой записи в data() вызывайте
//    normalize(). Благодаря инварианту count() — простая сумма, а
//    operator== корректен (в старой версии хвост ломал сравнение).
//
//  КОНТРАКТ НАСЛЕДНИКА D
//    uint8_t* data() / const uint8_t* data() const
//    uint16_t amount() const, uint16_t sizeBytes() const
//    uint8_t tailMask() const, bool valid() const
//    Методы могут быть static constexpr — тогда проверки стоят 0 тактов.
//
//  API — одиночные биты (bool = операция выполнена)
//    set(i), clear(i), toggle(i), write(i, v), read(i) const, operator[](i)
//  API — весь набор
//    setAll(), clearAll(), writeAll(v), invert(), normalize()
//  API — диапазоны [pos .. pos+len-1], len == 0 -> true
//    setRange(pos, len), clearRange(pos, len), writeRange(pos, len, v)
//  API — поля (LSB-first, len = 1..32)
//    getBits(pos, len) const -> uint32_t   обрезается по границе набора
//    setBits(pos, len, value) -> bool      выход за границу -> false, без записи
//  API — статистика
//    count(), countRange(pos, len)
//    any() / all() / none()                по всему набору, побайтово
//    any(n) / all(n) / none(n)             по первым n битам, n обрезается
//  API — поиск (не найдено -> amount())
//    firstSet(), firstClear(), nextSet(i), nextClear(i), forEachSet(f)
//  API — сравнение и копирование
//    operator==, operator!=, assign(o) -> bool,
//    copyFrom(const uint8_t* src) -> bool, copyTo(uint8_t* dst) const
//  API — перестановки
//    shiftLeft(n), shiftRight(n)           логические сдвиги всего набора
//    reverseBitsInBytes()                  зеркало бит внутри каждого байта
//                                          (только при F_USE_BIT_REVERSE)
//    reverseBitOrder()                     зеркало всего набора, i <-> N-1-i
//    reverseBytes()                        порядок байт на обратный
//
//  API — BitPack<N>
//    BitPack()                             обнуляет хранилище
//    pack[]                                прямой доступ (после записи — normalize())
//    bitsCount, bytesCount, tailBitsMask   static constexpr поля
//    amount(), sizeBytes(), tailMask(), valid()  static constexpr методы
//    data()
//  API — BitPackWide
//    BitPackWide() / BitPackWide(buf, bits, clear = true)
//    init(buf, bits, clear = true) -> bool false при nullptr или bits == 0
//    attach(buf, bits)                     привязать БЕЗ очистки
//    amount(), sizeBytes(), tailMask(), valid(), data()
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - FN_BITS_CHECKS = 1: индексы проверяются, nullptr безопасен, операции
//      возвращают false и ничего не меняют. = 0: проверок нет, выход за
//      границы — UB.
//    - any(0) = false, all(0) = true, none(0) = true (пустой диапазон).
//      Для пустого набора: none() = true, all() = true, any() = false.
//    - getBits/setBits ограничены 32 битами за вызов.
//    - reverseBitOrder работает напрямую с байтами: прежняя версия звала
//      read/write и платила двойную проверку границ на каждый бит.
//    - reverseBytes при amount() % 8 != 0 меняет соответствие бит и данных:
//      это операция над хранилищем, применяйте осознанно.
//    - BitPackWide не владеет буфером: время жизни буфера должно превышать
//      время жизни объекта. Копирование объекта копирует УКАЗАТЕЛЬ.
//    - BitPackWide кэширует sizeBytes и tailMask (7 байт на объект) —
//      дешевле, чем пересчитывать в каждом вызове. При смене буфера нужен
//      новый init().
//    - Атомарности нет: объект, разделяемый с ISR, защищать fn::AtomicBlock.
//    - N > 0 (static_assert). Максимум 65535 бит, но SRAM 328p = 2 КБ.
//    - Макросы FN_BP_BYTE / FN_BP_BIT удалены: они текли в глобальную
//      область без #undef. Вместо них — inline-вычисления в методах.
//
//  СТОИМОСТЬ (AVR)
//    sizeof(BitPack<N>) == bytesCount, sizeof(BitPackWide) == 7.
//    Все методы ядра шаблонные: неиспользуемые не инстанцируются. Циклы
//    идут по указателям, а не по 16-битным индексам. any/all/none —
//    побайтовые (примерно в 8 раз быстрее побитовых).
//
//  ПРИМЕР
//    fn::BitPack<24> p;
//    p.setBits(4, 12, 0x0ABC);
//    uint16_t v = uint16_t(p.getBits(4, 12));
//    for (uint16_t i = p.firstSet(); i < p.amount(); i = p.nextSet(i)) { ... }
//
//    uint8_t fb[128];
//    fn::BitPackWide screen(fb, 1024);
//    screen.writeRange(10, 20, true);
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, Types.h, BitMask.h, BitScan.h,
//               BitCount.h, BitShift.h, BitReverse.h (опционально)
// ============================================================================

#include "Config.h"
#include "Compiler.h"
#include "Types.h"
#include "BitMask.h"
#include "BitScan.h"
#include "BitCount.h"
#include "BitShift.h"
#include "BitReverse.h"

#include <stdint.h>
#include <string.h>

#if F_USE_BIT_PACK

namespace fn {
namespace detail {

// ============================ общее ядро (CRTP) ============================
template <class D>
class BitPackCore {
protected:
    D&       self()       { return static_cast<D&>(*this); }
    const D& self() const { return static_cast<const D&>(*this); }

    // Поиск бита со значением want начиная с start. Не найдено -> amount().
    uint16_t findFrom(uint16_t start, bool want) const {
        const uint16_t n = self().amount();
        const uint8_t* p = self().data();
        if (p == nullptr || start >= n) return n;
        const uint16_t nb = self().sizeBytes();
        uint16_t bi = uint16_t(start >> 3);
        uint8_t m = uint8_t(0xFFu << (start & 7u));
        while (bi < nb) {
            uint8_t v = want ? p[bi] : uint8_t(~p[bi]);
            v = uint8_t(v & m);
            if (bi == uint16_t(nb - 1)) v = uint8_t(v & self().tailMask());
            if (v) {
                const uint16_t r = uint16_t((bi << 3) + ctz8(v));
                return (r < n) ? r : n;
            }
            m = 0xFF;
            ++bi;
        }
        return n;
    }

public:
    // ------------------------------ одиночные биты -------------------------
    bool set(uint16_t i) {
#if FN_BITS_CHECKS
        if (!self().valid() || i >= self().amount()) return false;
#endif
        uint8_t* p = self().data();
        const uint16_t b = uint16_t(i >> 3);
        p[b] = uint8_t(p[b] | bitMask8(uint8_t(i & 7u)));
        return true;
    }

    bool clear(uint16_t i) {
#if FN_BITS_CHECKS
        if (!self().valid() || i >= self().amount()) return false;
#endif
        uint8_t* p = self().data();
        const uint16_t b = uint16_t(i >> 3);
        p[b] = uint8_t(p[b] & uint8_t(~bitMask8(uint8_t(i & 7u))));
        return true;
    }

    bool toggle(uint16_t i) {
#if FN_BITS_CHECKS
        if (!self().valid() || i >= self().amount()) return false;
#endif
        uint8_t* p = self().data();
        const uint16_t b = uint16_t(i >> 3);
        p[b] = uint8_t(p[b] ^ bitMask8(uint8_t(i & 7u)));
        return true;
    }

    bool write(uint16_t i, bool v) { return v ? set(i) : clear(i); }

    bool read(uint16_t i) const {
#if FN_BITS_CHECKS
        if (!self().valid() || i >= self().amount()) return false;
#endif
        const uint8_t* p = self().data();
        return (p[i >> 3] & bitMask8(uint8_t(i & 7u))) != 0;
    }

    bool operator[](uint16_t i) const { return read(i); }

    // ------------------------------ весь набор -----------------------------
    void setAll() {
        uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        if (p == nullptr || nb == 0) return;
        memset(p, 0xFF, nb);
        p[nb - 1] = uint8_t(p[nb - 1] & self().tailMask());
    }

    void clearAll() {
        uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        if (p == nullptr || nb == 0) return;
        memset(p, 0x00, nb);
    }

    void writeAll(bool v) { v ? setAll() : clearAll(); }

    void normalize() {
        uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        if (p == nullptr || nb == 0) return;
        const uint8_t tm = self().tailMask();
        if (tm != 0xFF) p[nb - 1] = uint8_t(p[nb - 1] & tm);
    }

    void invert() {
        uint8_t* p = self().data();
        if (p == nullptr) return;
        uint8_t* end = p + self().sizeBytes();
        for (; p != end; ++p) *p = uint8_t(~*p);
        normalize();
    }

    // ------------------------------ диапазоны ------------------------------
    bool writeRange(uint16_t pos, uint16_t len, bool v) {
        if (len == 0) return true;
#if FN_BITS_CHECKS
        if (!self().valid() || pos >= self().amount()) return false;
        if (len > uint16_t(self().amount() - pos)) return false;
#endif
        uint8_t* p = self().data();
        const uint16_t end = uint16_t(pos + len);
        const uint16_t first = uint16_t(pos >> 3);
        const uint16_t last = uint16_t((end - 1) >> 3);
        const uint8_t mF = uint8_t(0xFFu << (pos & 7u));
        const uint8_t mL = maskLo8(uint8_t(((end - 1) & 7u) + 1));

        if (first == last) {
            const uint8_t m = uint8_t(mF & mL);
            p[first] = v ? uint8_t(p[first] | m) : uint8_t(p[first] & uint8_t(~m));
        } else {
            p[first] = v ? uint8_t(p[first] | mF) : uint8_t(p[first] & uint8_t(~mF));
            if (last > uint16_t(first + 1)) {
                memset(p + first + 1, v ? 0xFF : 0x00, size_t(last - first - 1));
            }
            p[last] = v ? uint8_t(p[last] | mL) : uint8_t(p[last] & uint8_t(~mL));
        }
        return true;
    }

    bool setRange(uint16_t pos, uint16_t len)   { return writeRange(pos, len, true); }
    bool clearRange(uint16_t pos, uint16_t len) { return writeRange(pos, len, false); }

    // ------------------------------ поля -----------------------------------
    uint32_t getBits(uint16_t pos, uint8_t len) const {
        if (len == 0 || len > 32) return 0;
#if FN_BITS_CHECKS
        if (!self().valid() || pos >= self().amount()) return 0;
#endif
        const uint16_t avail = uint16_t(self().amount() - pos);
        if (len > avail) len = uint8_t(avail);

        const uint8_t* p = self().data();
        uint32_t r = 0;
        uint8_t done = 0;
        while (done < len) {
            const uint16_t idx = uint16_t(pos + done);
            const uint8_t off = uint8_t(idx & 7u);
            uint8_t take = uint8_t(8 - off);
            if (take > uint8_t(len - done)) take = uint8_t(len - done);
            const uint8_t chunk = uint8_t((p[idx >> 3] >> off) & maskLo8(take));
            r |= uint32_t(chunk) << done;
            done = uint8_t(done + take);
        }
        return r;
    }

    bool setBits(uint16_t pos, uint8_t len, uint32_t value) {
        if (len == 0) return true;
        if (len > 32) return false;
#if FN_BITS_CHECKS
        if (!self().valid() || pos >= self().amount()) return false;
        if (len > uint16_t(self().amount() - pos)) return false;
#endif
        uint8_t* p = self().data();
        uint8_t done = 0;
        while (done < len) {
            const uint16_t idx = uint16_t(pos + done);
            const uint8_t off = uint8_t(idx & 7u);
            uint8_t take = uint8_t(8 - off);
            if (take > uint8_t(len - done)) take = uint8_t(len - done);
            const uint8_t m = uint8_t(maskLo8(take) << off);
            const uint8_t chunk = uint8_t((value >> done) & maskLo8(take));
            const uint16_t b = uint16_t(idx >> 3);
            p[b] = uint8_t((p[b] & uint8_t(~m)) | uint8_t(chunk << off));
            done = uint8_t(done + take);
        }
        return true;
    }

    // ------------------------------ статистика -----------------------------
    uint16_t count() const {
        const uint8_t* p = self().data();
        if (p == nullptr) return 0;
        const uint8_t* end = p + self().sizeBytes();
        uint16_t c = 0;
        for (; p != end; ++p) c += popcount8(*p);
        return c;
    }

    uint16_t countRange(uint16_t pos, uint16_t len) const {
        const uint16_t n = self().amount();
        const uint8_t* p = self().data();
        if (p == nullptr || pos >= n || len == 0) return 0;
        if (len > uint16_t(n - pos)) len = uint16_t(n - pos);

        const uint16_t end = uint16_t(pos + len);
        const uint16_t nb = self().sizeBytes();
        uint16_t bi = uint16_t(pos >> 3);
        uint8_t m = uint8_t(0xFFu << (pos & 7u));
        uint16_t c = 0;
        while (bi < nb) {
            const uint16_t lastBit = uint16_t((bi << 3) + 7);
            const bool stop = (lastBit >= end);
            if (stop) m = uint8_t(m & maskLo8(uint8_t(end - (bi << 3))));
            c += popcount8(uint8_t(p[bi] & m));
            if (stop) break;
            m = 0xFF;
            ++bi;
        }
        return c;
    }

    bool none() const {
        const uint8_t* p = self().data();
        if (p == nullptr) return true;
        const uint8_t* end = p + self().sizeBytes();
        for (; p != end; ++p) if (*p) return false;
        return true;
    }

    bool any() const { return !none(); }

    bool all() const {
        const uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        if (p == nullptr) return false;
        if (nb == 0) return true;
        const uint8_t* end = p + (nb - 1);
        for (; p != end; ++p) if (*p != 0xFF) return false;
        return *p == self().tailMask();
    }

    bool none(uint16_t n) const {
        const uint16_t total = self().amount();
        if (n > total) n = total;
        const uint8_t* p = self().data();
        if (p == nullptr || n == 0) return true;
        const uint16_t full = uint16_t(n >> 3);
        for (uint16_t i = 0; i < full; ++i) if (p[i]) return false;
        const uint8_t rem = uint8_t(n & 7u);
        return rem == 0 || (p[full] & maskLo8(rem)) == 0;
    }

    bool all(uint16_t n) const {
        const uint16_t total = self().amount();
        if (n > total) n = total;
        const uint8_t* p = self().data();
        if (p == nullptr) return false;
        if (n == 0) return true;
        const uint16_t full = uint16_t(n >> 3);
        for (uint16_t i = 0; i < full; ++i) if (p[i] != 0xFF) return false;
        const uint8_t rem = uint8_t(n & 7u);
        if (rem == 0) return true;
        const uint8_t m = maskLo8(rem);
        return uint8_t(p[full] & m) == m;
    }

    bool any(uint16_t n) const { return !none(n); }

    // ------------------------------ поиск ----------------------------------
    uint16_t firstSet()   const { return findFrom(0, true); }
    uint16_t firstClear() const { return findFrom(0, false); }

    uint16_t nextSet(uint16_t i) const {
        if (i >= self().amount()) return self().amount();
        return findFrom(uint16_t(i + 1), true);
    }

    uint16_t nextClear(uint16_t i) const {
        if (i >= self().amount()) return self().amount();
        return findFrom(uint16_t(i + 1), false);
    }

    // f(uint16_t index) — вызывается для каждого установленного бита.
    template <typename F>
    void forEachSet(F f) const {
        const uint16_t n = self().amount();
        for (uint16_t i = firstSet(); i < n; i = nextSet(i)) f(i);
    }

    // --------------------------- сравнение / копия -------------------------
    bool operator==(const D& o) const {
        if (self().amount() != o.amount()) return false;
        const uint8_t* a = self().data();
        const uint8_t* b = o.data();
        if (a == nullptr || b == nullptr) return a == b;
        const uint8_t* end = a + self().sizeBytes();
        for (; a != end; ++a, ++b) if (*a != *b) return false;
        return true;
    }

    bool operator!=(const D& o) const { return !(*this == o); }

    bool assign(const D& o) {
        if (!self().valid() || !o.valid()) return false;
        if (self().amount() != o.amount()) return false;
        memcpy(self().data(), o.data(), self().sizeBytes());
        normalize();
        return true;
    }

    bool copyFrom(const uint8_t* src) {
        if (!self().valid() || src == nullptr) return false;
        memcpy(self().data(), src, self().sizeBytes());
        normalize();
        return true;
    }

    void copyTo(uint8_t* dst) const {
        if (!self().valid() || dst == nullptr) return;
        memcpy(dst, self().data(), self().sizeBytes());
    }

    // ------------------------------ сдвиги ---------------------------------
    void shiftLeft(uint16_t n) {
        if (!self().valid() || n == 0) return;
        if (n >= self().amount()) { clearAll(); return; }

        uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        const uint16_t bs = uint16_t(n >> 3);
        const uint8_t sh = uint8_t(n & 7u);

        if (sh == 0) {
            for (int16_t i = int16_t(nb) - 1; i >= 0; --i) {
                p[i] = (i >= int16_t(bs)) ? p[uint16_t(i - int16_t(bs))] : 0;
            }
        } else {
            for (int16_t i = int16_t(nb) - 1; i >= 0; --i) {
                const int16_t s = int16_t(i - int16_t(bs));
                uint16_t v = 0;
                if (s >= 0) v = uint16_t(uint16_t(p[uint16_t(s)]) << 8);
                if (s >= 1) v = uint16_t(v | p[uint16_t(s - 1)]);
                p[i] = uint8_t(v >> (8u - sh));
            }
        }
        normalize();
    }

    void shiftRight(uint16_t n) {
        if (!self().valid() || n == 0) return;
        if (n >= self().amount()) { clearAll(); return; }

        uint8_t* p = self().data();
        const uint16_t nb = self().sizeBytes();
        const uint16_t bs = uint16_t(n >> 3);
        const uint8_t sh = uint8_t(n & 7u);

        if (sh == 0) {
            for (uint16_t i = 0; i < nb; ++i) {
                const uint16_t s = uint16_t(i + bs);
                p[i] = (s < nb) ? p[s] : 0;
            }
        } else {
            for (uint16_t i = 0; i < nb; ++i) {
                const uint16_t s = uint16_t(i + bs);
                uint16_t v = (s < nb) ? p[s] : 0;
                if (uint16_t(s + 1) < nb) v = uint16_t(v | (uint16_t(p[s + 1]) << 8));
                p[i] = uint8_t(v >> sh);
            }
        }
        normalize();
    }

    // ------------------------------ реверс ---------------------------------
#if F_USE_BIT_REVERSE
    void reverseBitsInBytes() {
        uint8_t* p = self().data();
        if (p == nullptr) return;
        uint8_t* end = p + self().sizeBytes();
        for (; p != end; ++p) *p = bitrev8(*p);
        normalize();
    }
#endif

    // Прямой обмен бит без read/write: двойная проверка границ на каждый
    // бит стоила бы дороже самого обмена.
    void reverseBitOrder() {
        const uint16_t n = self().amount();
        if (!self().valid() || n < 2) return;
        uint8_t* p = self().data();
        uint16_t i = 0;
        uint16_t j = uint16_t(n - 1);
        while (i < j) {
            const uint8_t mi = bitMask8(uint8_t(i & 7u));
            const uint8_t mj = bitMask8(uint8_t(j & 7u));
            uint8_t& bi = p[i >> 3];
            uint8_t& bj = p[j >> 3];
            const bool a = (bi & mi) != 0;
            const bool b = (bj & mj) != 0;
            if (a != b) {
                bi = uint8_t(bi ^ mi);
                bj = uint8_t(bj ^ mj);
            }
            ++i;
            --j;
        }
    }

    void reverseBytes() {
        uint8_t* p = self().data();
        if (p == nullptr) return;
        reverseByteOrder(p, self().sizeBytes());
        normalize();
    }
};

}  // namespace detail

// ========================= статический битовый пакет =======================
template <uint16_t N>
class BitPack : public detail::BitPackCore<BitPack<N>> {
    static_assert(N > 0, "fn::BitPack<N>: N должен быть больше нуля");

public:
    static constexpr uint16_t bitsCount = N;
    static constexpr uint16_t bytesCount = (N >> 3) + ((N & 7) ? 1 : 0);
    static constexpr uint8_t tailBitsMask =
        (N & 7) ? uint8_t((1u << (N & 7)) - 1u) : uint8_t(0xFF);

    uint8_t pack[bytesCount];

    BitPack() { this->clearAll(); }

    static constexpr uint16_t amount()    { return N; }
    static constexpr uint16_t sizeBytes() { return bytesCount; }
    static constexpr uint8_t  tailMask()  { return tailBitsMask; }
    static constexpr bool     valid()     { return true; }

    uint8_t*       data()       { return pack; }
    const uint8_t* data() const { return pack; }
};

// ====================== битовый пакет на внешнем буфере ====================
class BitPackWide : public detail::BitPackCore<BitPackWide> {
public:
    BitPackWide() = default;

    BitPackWide(uint8_t* buf, uint16_t bits, bool clear = true) { init(buf, bits, clear); }

    bool init(uint8_t* buf, uint16_t bits, bool clear = true) {
        if (buf == nullptr || bits == 0) {
            _buf = nullptr;
            _bits = 0;
            _bytes = 0;
            _tail = 0xFF;
            return false;
        }
        _buf = buf;
        _bits = bits;
        _bytes = bytesNeeded(bits);
        _tail = (bits & 7) ? uint8_t((1u << (bits & 7)) - 1u) : uint8_t(0xFF);
        if (clear) this->clearAll();
        return true;
    }

    void attach(uint8_t* buf, uint16_t bits) { init(buf, bits, false); }

    uint16_t amount()    const { return _bits; }
    uint16_t sizeBytes() const { return _bytes; }
    uint8_t  tailMask()  const { return _tail; }
    bool     valid()     const { return _buf != nullptr; }

    uint8_t*       data()       { return _buf; }
    const uint8_t* data() const { return _buf; }

private:
    uint8_t* _buf = nullptr;
    uint16_t _bits = 0;
    uint16_t _bytes = 0;
    uint8_t _tail = 0xFF;
};

}  // namespace fn

#endif  // F_USE_BIT_PACK
// ============================================================================
//  Quadrature.cpp — квадратурный декодер: регистрация и обработка фронтов
//  Цель: ATmega328P/328
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Реализация декодера. Eigene ISR нет: обработчик регистрируется в общем
//    диспетчере fn::irq::PinChange с контекстом this. Диспетчер вызывает
//    коллбэк отдельно для каждого изменившегося вывода, поэтому обход всех
//    декодеров (как в прежней версии с реестром) не нужен.
//
//  ДЕКОДИРОВАНИЕ БЕЗ ТАБЛИЦЫ
//    Состояние = A | (B << 1). Для разности diff = prev ^ cur:
//      0 — нет изменения; 3 — изменились оба канала (пропуск фронта);
//      1 или 2 — один канал, направление = ((prev >> 1) ^ cur) & 1 = pB ^ cA.
//    Проверено по всем восьми допустимым переходам против табличного варианта:
//    результат совпадает. Формула дешевле LPM и не занимает flash под таблицу.
//
//  ПОРЯДОК ИНИЦИАЛИЗАЦИИ
//    Объект заполняется полностью (пины, режим входов, _last, _pos, флаги)
//    и только затем регистрируется в диспетчере. Прежняя версия добавляла
//    объект в реестр до инициализации — ISR мог вызвать полусобранный декодер.
//    При отказе второго attach первый снимается, объект гасится.
//
//  ОБРАБОТКА ПРОПУСКА ФРОНТА
//    Недопустимый переход (оба канала сразу) позицию НЕ меняет и увеличивает
//    errors(). Прежняя таблица давала 0 и для «нет изменения», и для
//    «оба канала», из-за чего позиция молча уезжала при пропуске.
//
//  ОГРАНИЧЕНИЯ
//    - Позиция volatile int32_t: чтение и запись под AtomicBlock, иначе
//      основной код увидит разорванное значение.
//    - В режиме прерываний apply() вызывается из ISR; в polling — из update()
//      под AtomicBlock (защита от собственного ISR пользователя).
//    - Файл должен лежать в src/pin/ (формат библиотеки 1.5).
//
//  ЗАВИСИМОСТИ: Quadrature.h, core/Registers.h, core/Atomic.h, pin/Irq.h
// ============================================================================

#include "../core/Config.h"

#if F_USE_QUAD

#include "Quadrature.h"

namespace fn {

// ============================ декодирование ================================
uint8_t QuadDecoder::readState() const {
    return uint8_t((regs::read(_a) ? 1u : 0u) | (regs::read(_b) ? 2u : 0u));
}

void QuadDecoder::apply(uint8_t s) {
    const uint8_t prev = _last;
    const uint8_t diff = uint8_t(prev ^ s);
    if (diff == 0) return;                 // изменения нет

    _last = s;                             // фиксируем фактическое состояние

    if (diff == 3) {
        // Изменились оба канала: фронт пропущен. Позицию не трогаем.
        _err = uint16_t(_err + 1u);
        return;
    }

    // Направление = pB ^ cA. Проверено по всем 8 допустимым переходам.
    const bool up = ((((prev >> 1) ^ s) & 1u) != 0);
    _pos = up ? int32_t(_pos + 1) : int32_t(_pos - 1);
}

void QuadDecoder::onChange(Pin /*changed*/) {
    if (!active()) return;
    // Диспетчер вызывает нас только для наших выводов; состояние читается
    // целиком (оба канала), поэтому различать A и B здесь не требуется.
    apply(readState());
}

void QuadDecoder::isr(void* ctx, Pin changed) {
    static_cast<QuadDecoder*>(ctx)->onChange(changed);
}

void QuadDecoder::update() {
    if (!active() || useInterrupts()) return;
    AtomicBlock lock;
    apply(readState());
}

// ============================ регистрация ==================================
Result QuadDecoder::begin(uint8_t pinA, uint8_t pinB, bool useInterrupts) {
    return begin(boardPin(pinA), boardPin(pinB), useInterrupts);
}

Result QuadDecoder::begin(Pin a, Pin b, bool useInterrupts) {
    if (!a.valid() || !b.valid() || a == b) return Result::BadArg;

#if !F_USE_IRQ
    if (useInterrupts) return Result::NotReady;
#endif

    if (active()) end();

#if F_USE_IRQ
    // Каждый декодер занимает два слота пула. Проверяем заранее, чтобы
    // не трогать железо при заведомо нехватке места.
    if (useInterrupts && irq::PinChange::free() < 2) return Result::NoBuffer;
#endif

    _a = a;
    _b = b;
    regs::modeInputPullup(_a);
    regs::modeInputPullup(_b);

    {
        AtomicBlock lock;
        _pos = 0;
        _readPos = 0;
        _err = 0;
        _last = readState();
        _flags = uint8_t(FlagActive | (useInterrupts ? FlagIsr : 0));
    }

#if F_USE_IRQ
    if (useInterrupts) {
        // attach сам включает прерывание; отдельный enable() не нужен.
        const Result ra = irq::PinChange::attach(_a, isr, this);
        if (!ok(ra)) { _flags = 0; return ra; }

        const Result rb = irq::PinChange::attach(_b, isr, this);
        if (!ok(rb)) {
            irq::PinChange::detach(_a);
            _flags = 0;
            return rb;
        }
    }
#endif
    return Result::Ok;
}

void QuadDecoder::end() {
    if (!active()) return;

#if F_USE_IRQ
    if (useInterrupts()) {
        irq::PinChange::detach(_a);
        irq::PinChange::detach(_b);
    }
#endif
    AtomicBlock lock;
    _flags = 0;
}

// ============================ доступ к позиции =============================
int32_t QuadDecoder::position() const {
    AtomicBlock lock;
    return _pos;
}

int32_t QuadDecoder::takeDelta() {
    AtomicBlock lock;
    const int32_t p = _pos;
    const int32_t d = int32_t(p - _readPos);
    _readPos = p;
    return d;
}

void QuadDecoder::reset(int32_t pos) {
    AtomicBlock lock;
    _last = readState();
    _pos = pos;
    _readPos = pos;
    _err = 0;
}

uint16_t QuadDecoder::errors() const {
    AtomicBlock lock;
    return _err;
}

void QuadDecoder::clearErrors() {
    AtomicBlock lock;
    _err = 0;
}

}  // namespace fn

#endif  // F_USE_QUAD
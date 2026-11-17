#pragma once
// ============================================================================
//  Types.h — базовые типы, коды результата, дескрипторы выводов, трейты
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Общий словарь типов для всех слоёв. Кода, работающего с железом, здесь
//    нет: только то, что нужно и ядру, и прикладным модулям.
//
//  API (namespace fn)
//    Коды результата
//      enum class Result : uint8_t
//        Ok, Fail, BadArg, OutOfRange, NoBuffer, Busy, Timeout,
//        NotReady, Unsupported, CrcError, AddrNack, DataNack
//      using Status = uint8_t              компактное хранение кода
//      ok(Result) / ok(Status)             true только для Ok
//      toStatus(Result) / toResult(Status)
//      resultName(Result) -> PGM_P         имя кода во flash (Tables.cpp)
//
//    Выводы и порты
//      enum class Port : uint8_t { B = 0, C = 1, D = 2 }
//      enum class Reg  : uint8_t { Pin = 0, Ddr = 1, Port = 2 }
//      struct Pin                          упакованный дескриптор, 1 байт
//        constexpr Pin()                   невалидный (NoPin)
//        constexpr Pin(Port, bit)          сборка; вне диапазона -> NoPin
//        static constexpr fromId(id)       разбор упакованного байта
//        id() / port() / bit() / mask() / valid()
//      NoPin, PinIdInvalid = 0xFF
//
//    Трейты (fn::detail, служебные)
//      Select<C, A, B>::type               выбор типа по условию
//      Id<T>::type                         тождественный (невыводимый параметр)
//      UnsignedOf<T>::type                 беззнаковый аналог той же ширины
//      IsBitType<T>::value                 true для uint8_t/uint16_t/uint32_t
//      WidthOf<T>::value                   разрядность 8/16/32
//    fn::UBits<T>                          алиас UnsignedOf<T>::type
//
//  ОГРАНИЧЕНИЯ / ГАРАНТИИ
//    - sizeof(Pin) == 1. Формат (порт << 3) | бит: таблица из 24 сигналов
//      занимает 24 байта вместо 48 при хранении порта и маски отдельно.
//    - Pin::valid() проверяет диапазоны и отсутствие PC7. Физические
//      ограничения (PC6 = RESET, PB6/PB7 = кварц, PD0/PD1 = UART) НЕ
//      проверяются: это ответственность pin/Board.h.
//    - Status — uint8_t, а не enum: дешевле хранить и сравнивать, слабее
//      типобезопасность. В параметрах функций используйте Result.
//    - 64-битные типы трейтами не поддерживаются: подстановка даст ошибку
//      компиляции. На AVR это дорогие библиотечные вызовы.
//    - resultName возвращает указатель во flash: разыменовывать напрямую
//      нельзя, только pgm_read_* / strcpy_P / FPSTR. Определение вынесено
//      в Tables.cpp: static-массив указателей внутри inline-функции может
//      дублироваться между единицами трансляции.
//    - Трейты дублировать в других модулях запрещено — только отсюда.
//
//  СТОИМОСТЬ (AVR)
//    Всё constexpr или inline. Ноль байт RAM, пока не заведено переменных.
//    resultName: 12 строк + 12 указателей во flash, вырезается если не звать.
//
//  ПРИМЕР
//    constexpr fn::Pin LED(fn::Port::B, 5);
//    constexpr fn::Pin RELAY[4] = {
//        {fn::Port::D, 2}, {fn::Port::D, 3}, {fn::Port::B, 0}, fn::NoPin};
//    static_assert(sizeof(RELAY) == 4, "по байту на вывод");
//
//    fn::Result r = doWork();
//    if (!fn::ok(r)) Uart::writeP(fn::resultName(r));
//
//  ЗАВИСИМОСТИ: Config.h, Compiler.h, stdint.h, avr/pgmspace.h
// ============================================================================

#include "Config.h"
#include "Compiler.h"

#include <stdint.h>
#include <avr/pgmspace.h>

namespace fn {

// ============================ коды результата ==============================
enum class Result : uint8_t {
    Ok = 0,        ///< операция выполнена
    Fail,          ///< общая ошибка без уточнения
    BadArg,        ///< недопустимый аргумент (nullptr, нулевой размер)
    OutOfRange,    ///< индекс или значение вне диапазона
    NoBuffer,      ///< буфер не привязан / пул исчерпан
    Busy,          ///< ресурс занят, повторить позже
    Timeout,       ///< истёк срок ожидания
    NotReady,      ///< модуль не инициализирован
    Unsupported,   ///< операция недоступна в этой конфигурации
    CrcError,      ///< не совпала контрольная сумма
    AddrNack,      ///< шина: адрес не подтверждён
    DataNack       ///< шина: байт данных не подтверждён
};

using Status = uint8_t;

inline constexpr bool ok(Result r) { return r == Result::Ok; }
inline constexpr bool ok(Status s) { return s == static_cast<Status>(Result::Ok); }
inline constexpr Status toStatus(Result r) { return static_cast<Status>(r); }
inline constexpr Result toResult(Status s) { return static_cast<Result>(s); }

// Имя кода во flash. Определение — в Tables.cpp.
PGM_P resultName(Result r);

// ============================ порты и регистры =============================
enum class Port : uint8_t { B = 0, C = 1, D = 2 };

// Порядок регистров в тройке: PIN, DDR, PORT. Свойство ATmega328P.
enum class Reg : uint8_t { Pin = 0, Ddr = 1, Port = 2 };

// ============================ дескриптор вывода ============================
constexpr uint8_t PinIdInvalid = 0xFFu;

struct Pin {
    uint8_t packed;   ///< (порт << 3) | бит; 0xFF = не задан

    constexpr Pin() : packed(PinIdInvalid) {}

    constexpr Pin(Port p, uint8_t b)
        : packed((static_cast<uint8_t>(p) < PortCount && b < 8)
                     ? static_cast<uint8_t>((static_cast<uint8_t>(p) << 3) | b)
                     : PinIdInvalid) {}

    static constexpr Pin fromId(uint8_t id) { return Pin(id, RawTag{}); }

    constexpr uint8_t id() const { return packed; }
    constexpr Port port() const { return static_cast<Port>(packed >> 3); }
    constexpr uint8_t bit() const { return static_cast<uint8_t>(packed & 7u); }
    constexpr uint8_t mask() const { return static_cast<uint8_t>(1u << (packed & 7u)); }

    // PC7 на ATmega328P физически отсутствует.
    constexpr bool valid() const {
        return packed != PinIdInvalid && (packed >> 3) < PortCount &&
               !(port() == Port::C && bit() == 7);
    }

    constexpr bool operator==(const Pin& o) const { return packed == o.packed; }
    constexpr bool operator!=(const Pin& o) const { return packed != o.packed; }

private:
    struct RawTag {};
    constexpr Pin(uint8_t id, RawTag) : packed(id) {}
};

constexpr Pin NoPin{};

static_assert(sizeof(Pin) == 1, "fn::Pin должен занимать ровно один байт");

// ============================ служебные трейты =============================
namespace detail {

template <bool C, typename A, typename B>
struct Select { using type = A; };
template <typename A, typename B>
struct Select<false, A, B> { using type = B; };

// Невыводимый контекст: параметр приводится к типу первого аргумента,
// а не участвует в выводе T (иначе bitfGet(u8, 0x0F) не скомпилируется).
template <typename T>
struct Id { using type = T; };

// Беззнаковый аналог той же ширины. На AVR char / signed char / unsigned char
// и short / int — попарно разные типы, поэтому специализаций девять.
template <typename T>
struct UnsignedOf;
template <> struct UnsignedOf<unsigned char>  { using type = uint8_t;  };
template <> struct UnsignedOf<signed char>    { using type = uint8_t;  };
template <> struct UnsignedOf<char>           { using type = uint8_t;  };
template <> struct UnsignedOf<unsigned short> { using type = uint16_t; };
template <> struct UnsignedOf<short>          { using type = uint16_t; };
template <> struct UnsignedOf<unsigned int>   { using type = uint16_t; };
template <> struct UnsignedOf<int>            { using type = uint16_t; };
template <> struct UnsignedOf<unsigned long>  { using type = uint32_t; };
template <> struct UnsignedOf<long>           { using type = uint32_t; };

template <typename T>
struct IsBitType { static constexpr bool value = false; };
template <> struct IsBitType<uint8_t>  { static constexpr bool value = true; };
template <> struct IsBitType<uint16_t> { static constexpr bool value = true; };
template <> struct IsBitType<uint32_t> { static constexpr bool value = true; };

template <typename T>
struct WidthOf {
    static constexpr uint8_t value = static_cast<uint8_t>(sizeof(T) * 8u);
};

// Заведомо ложное утверждение, зависящее от шаблона: срабатывает только
// при инстанцировании, а не при разборе шаблона.
template <typename>
struct AlwaysFalse { static constexpr bool value = false; };

}  // namespace detail

template <typename T>
using UBits = typename detail::UnsignedOf<T>::type;

}  // namespace fn
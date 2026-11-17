#pragma once
// ============================================================================
//  Packet.h — пакетный протокол для UART/радио: парсер и сборщик кадра
//  Цель: ATmega328P/328, gnu++11
// ----------------------------------------------------------------------------
//  НАЗНАЧЕНИЕ
//    Неблокирующий конечный автомат приёма и сборщик передачи. Без буферов
//    ввода-вывода, без таймеров, без прерываний: чистая логика над байтами.
//    Транспорт (Uart/Spi/радио) отдаёт байты в push() и забирает data()/size().
//
//  ФОРМАТ КАДРА
//    [SYNC] [LEN] [TYPE] [PAYLOAD ... LEN байт] [CRC]
//    LEN   — длина PAYLOAD (0..MaxPayload).
//    CRC   — по умолчанию CRC-8 (poly 0x07, init 0x00), 1 байт.
//            При FN_PACKET_CRC16=1 — CRC-16/CCITT-FALSE, 2 байта, старший первый.
//    CRC считается по LEN, TYPE и PAYLOAD. SYNC в CRC не входит.
//    Накладные расходы: 4 байта при CRC-8, 5 при CRC-16.
//
//  API
//    template <uint8_t MaxPayload> class PacketParser
//      enum class State : uint8_t { Sync, Len, Type, Payload, Crc }
//      static constexpr crcBytes / overhead
//      reset()                            сброс автомата и флагов кадра
//      push(uint8_t b) -> bool            true: принят целый кадр с верным CRC
//      push(const void* p, uint16_t n) -> uint16_t  байт обработано (до 1-го кадра)
//      state() / inFrame()                позиция автомата
//      type() / length() / payload()      содержимое последнего кадра
//      crcOk() / overflow()               флаги последнего кадра
//      status() -> Result                 Ok / CrcError / OutOfRange / NotReady
//      frames() / crcErrors() / overflows() -> uint16_t
//      clearStats()
//
//    template <uint8_t MaxPayload> class PacketWriter
//      begin(type = 0)                    начать кадр
//      push(uint8_t) / push(p, n) -> bool false: кадр закрыт или переполнен
//      pushU8 / pushU16 / pushI16 / pushU32 / pushI32 -> bool   little-endian
//      finish() -> uint16_t               вписать LEN и CRC, вернуть размер кадра
//      data() / size() / length() / type()
//      ready() / full() / free()
//
//    using PacketReader = PacketParser    legacy-алиас
//
//  КОНФИГУРАЦИЯ
//    FN_PACKET_SYNC         0xAA   байт синхронизации
//    FN_PACKET_MAX_PAYLOAD  32     максимальная длина полезной нагрузки
//    FN_PACKET_CRC16        0      0 = CRC-8 (совместимо с прежними кадрами),
//                                1 = CRC-16/CCITT-FALSE
//    FN_PACKET_STATS        1      счётчики кадров/ошибок (6 байт SRAM на парсер)
//    Legacy F_PACKET_SYNC / F_PACKET_MAX_PAYLOAD сохранены как алиасы.
//
//  ОГРАНИЧЕНИЯ / ВАЖНО
//    - Флаги кадра (crcOk/overflow/crcErr) сбрасываются при входе в состояние
//      Len, то есть на каждом новом кадре. Прежняя версия оставляла overflow
//      висеть до следующего заголовка, из-за чего overflow() врал после
//      длинного кадра. Счётчик overflows() при этом накопительный.
//    - Мёртвая ветка устранена: в состоянии Crc переход теперь зависит только
//      от совпадения байта с SYNC. Прежняя формула (_crcOk || b == SYNC)
//      немедленно перезаписывалась и ничего не решала.
//    - Ресинхронизация: при ошибке CRC автомат смотрит на сам CRC-байт — если
//      он равен SYNC, следующий байт трактуется как LEN. Это ускоряет захват
//      нового кадра, но не сканирует PAYLOAD на SYNC: кадр, потерявший байт
//      в середине, может «съесть» начало следующего. Полное восстановление
//      гарантируется только по таймауту (см. ниже).
//    - Таймаута НЕТ намеренно: модуль не зависит от Tick и остаётся чистым
//      автоматом, тестируемым на хосте. Оборванный кадр оставляет автомат в
//      состоянии Payload навсегда. Вызывающий обязан звать reset() по таймауту
//      шины: if (inFrame() && fn::expired(lastByteMs, 50)) parser.reset();
//    - MaxPayload <= 255: LEN однобайтовый. Кадр с LEN > MaxPayload отбрасывается
//      целиком, счётчик overflows() растёт, payload не пишется.
//    - Байты многобайтных pushU16/pushU32 идут little-endian (младший первый),
//      а CRC-16 в кадре — big-endian. Это разные соглашения намеренно:
//      полезная нагрузка совместима с AVR-представлением, CRC — с сетевым.
//    - PacketWriter не владеет транспортом: finish() только формирует кадр
//      в своём буфере, отправку делает вызывающий.
//    - Память: парсер MaxPayload + 14 байт (при CRC-8 и статистике),
//      сборщик MaxPayload + 8. Ни того, ни другого нет в .bss, пока объект
//      не создан: это шаблон, инстанцируется по месту объявления.
//    - CRC-8 пропускает ~1/256 искажений. Для радиоканала с помехами
//      ставьте FN_PACKET_CRC16=1: 5 байт накладных вместо 4, зато 1/65536.
//
//  СТОИМОСТЬ (AVR)
//    sizeof(PacketParser<32>) = 46 байт при CRC-8 + статистике,
//                               40 без статистики, +3 при CRC-16.
//    sizeof(PacketWriter<32>) = 40 байт при CRC-8.
//    Flash: ~250 байт парсер, ~180 сборщик; CRC берётся из общего движка.
//
//  ПРИМЕР
//    fn::PacketParser<32> rx;
//    fn::PacketWriter<32> tx;
//
//    void loop() {
//        uint8_t b;
//        while (fn::Uart::read(&b, 1, 0) == fn::Result::Ok) {
//            if (rx.push(b)) handle(rx.type(), rx.payload(), rx.length());
//        }
//        if (rx.inFrame() && fn::expired(lastByteMs, 50)) rx.reset();
//
//        tx.begin(0x01);
//        tx.pushU16(sensorValue);
//        const uint16_t n = tx.finish();
//        fn::Uart::write(tx.data(), n);
//    }
//
//  ЗАВИСИМОСТИ: core/Config.h, core/Types.h, core/Crc.h
// ============================================================================

#include "../core/Config.h"
#include "../core/Types.h"
#include "../core/Crc.h"

#include <stdint.h>

#if F_USE_PACKET

#ifndef FN_PACKET_SYNC
#define FN_PACKET_SYNC 0xAA
#endif

#ifndef FN_PACKET_MAX_PAYLOAD
#define FN_PACKET_MAX_PAYLOAD 32
#endif

#ifndef FN_PACKET_CRC16
#define FN_PACKET_CRC16 0
#endif

#ifndef FN_PACKET_STATS
#define FN_PACKET_STATS 1
#endif

// Legacy-имена: сохранены, чтобы существующий код продолжал собираться.
#ifndef F_PACKET_SYNC
#define F_PACKET_SYNC FN_PACKET_SYNC
#endif
#ifndef F_PACKET_MAX_PAYLOAD
#define F_PACKET_MAX_PAYLOAD FN_PACKET_MAX_PAYLOAD
#endif

namespace fn {

// ============================ приёмник =====================================
template <uint8_t MaxPayload = FN_PACKET_MAX_PAYLOAD>
class PacketParser {
    static_assert(MaxPayload <= 255, "PacketParser: LEN однобайтовый, MaxPayload <= 255");

public:
    enum class State : uint8_t { Sync, Len, Type, Payload, Crc };

    static constexpr uint8_t crcBytes = FN_PACKET_CRC16 ? 2u : 1u;
    static constexpr uint8_t overhead = uint8_t(3u + crcBytes);

    void reset() {
        _state = State::Sync;
        _len = 0;
        _idx = 0;
        _type = 0;
        _crc = 0;
        _crcIdx = 0;
        _flags = 0;
#if FN_PACKET_CRC16
        _rxCrc = 0;
#endif
    }

    // true — принят целый кадр с корректным CRC.
    bool push(uint8_t b) {
        switch (_state) {
            case State::Sync:
                if (b == uint8_t(FN_PACKET_SYNC)) _state = State::Len;
                break;

            case State::Len:
                _len = b;
                _idx = 0;
                _crcIdx = 0;
                _flags = 0;                       // флаги живут ровно один кадр
                if (_len > MaxPayload) {
                    _flags |= FlagOverflow;
#if FN_PACKET_STATS
                    _ovf = uint16_t(_ovf + 1u);
#endif
                    // LEN совпал с SYNC — вероятно это начало нового кадра:
                    // остаёмся в Len и прочитаем LEN следующим байтом.
                    _state = (b == uint8_t(FN_PACKET_SYNC)) ? State::Len : State::Sync;
                    break;
                }
                _crc = crcStep(crcInit(), b);
                _state = State::Type;
                break;

            case State::Type:
                _type = b;
                _crc = crcStep(_crc, b);
                _state = (_len == 0) ? State::Crc : State::Payload;
                break;

            case State::Payload:
                _payload[_idx++] = b;
                _crc = crcStep(_crc, b);
                if (_idx >= _len) _state = State::Crc;
                break;

            case State::Crc: {
#if FN_PACKET_CRC16
                if (_crcIdx == 0) {
                    _rxCrc = uint16_t(uint16_t(b) << 8);   // старший байт первым
                    _crcIdx = 1;
                    break;
                }
                _rxCrc = uint16_t(_rxCrc | b);
                const bool ok = (_rxCrc == _crc);
#else
                const bool ok = (b == uint8_t(_crc));
#endif
                if (ok) {
                    _flags |= FlagCrcOk;
#if FN_PACKET_STATS
                    _frames = uint16_t(_frames + 1u);
#endif
                    _state = State::Sync;
                    return true;
                }
                _flags |= FlagCrcErr;
#if FN_PACKET_STATS
                _crcErr = uint16_t(_crcErr + 1u);
#endif
                // Единственное условие перехода — совпадение с SYNC.
                _state = (b == uint8_t(FN_PACKET_SYNC)) ? State::Len : State::Sync;
                break;
            }
        }
        return false;
    }

    // Обрабатывает буфер, останавливается на первом корректном кадре.
    // Возвращает число реально потреблённых байт.
    uint16_t push(const void* data, uint16_t n) {
        if (data == nullptr) return 0;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (uint16_t i = 0; i < n; ++i) {
            if (push(p[i])) return uint16_t(i + 1u);
        }
        return n;
    }

    State   state()   const { return _state; }
    bool    inFrame() const { return _state != State::Sync; }
    uint8_t type()    const { return _type; }
    uint8_t length()  const { return _len; }
    const uint8_t* payload() const { return _payload; }

    bool crcOk()    const { return (_flags & FlagCrcOk) != 0; }
    bool overflow() const { return (_flags & FlagOverflow) != 0; }

    Result status() const {
        if (_flags & FlagCrcOk)    return Result::Ok;
        if (_flags & FlagOverflow) return Result::OutOfRange;
        if (_flags & FlagCrcErr)   return Result::CrcError;
        return Result::NotReady;
    }

    uint16_t frames() const {
#if FN_PACKET_STATS
        return _frames;
#else
        return 0;
#endif
    }
    uint16_t crcErrors() const {
#if FN_PACKET_STATS
        return _crcErr;
#else
        return 0;
#endif
    }
    uint16_t overflows() const {
#if FN_PACKET_STATS
        return _ovf;
#else
        return 0;
#endif
    }

    void clearStats() {
#if FN_PACKET_STATS
        _frames = 0;
        _crcErr = 0;
        _ovf = 0;
#endif
    }

private:
    static constexpr uint8_t FlagCrcOk    = 0x01;
    static constexpr uint8_t FlagOverflow = 0x02;
    static constexpr uint8_t FlagCrcErr   = 0x04;

    static uint16_t crcInit() { return FN_PACKET_CRC16 ? 0xFFFFu : 0x0000u; }

    static uint16_t crcStep(uint16_t c, uint8_t b) {
#if FN_PACKET_CRC16
        return crc::crc16_ccitt_update(c, b);
#else
        return crc::crc8_update(uint8_t(c), b);
#endif
    }

    uint8_t _payload[MaxPayload] = {};
    uint8_t _len = 0;
    uint8_t _idx = 0;
    uint8_t _type = 0;
    uint8_t _crcIdx = 0;
    uint8_t _flags = 0;
    uint16_t _crc = 0;
    State _state = State::Sync;
#if FN_PACKET_CRC16
    uint16_t _rxCrc = 0;
#endif
#if FN_PACKET_STATS
    uint16_t _frames = 0;
    uint16_t _crcErr = 0;
    uint16_t _ovf = 0;
#endif
};

// ============================ сборщик ======================================
template <uint8_t MaxPayload = FN_PACKET_MAX_PAYLOAD>
class PacketWriter {
    static_assert(MaxPayload <= 255, "PacketWriter: LEN однобайтовый, MaxPayload <= 255");

public:
    static constexpr uint8_t crcBytes = FN_PACKET_CRC16 ? 2u : 1u;
    static constexpr uint8_t overhead = uint8_t(3u + crcBytes);

    void begin(uint8_t type = 0) {
        _buf[0] = uint8_t(FN_PACKET_SYNC);
        _buf[1] = 0;
        _buf[2] = type;
        _len = 0;
        _size = 0;
        _open = true;
    }

    bool push(uint8_t b) {
        if (!_open || _len >= MaxPayload) return false;
        _buf[3u + _len] = b;
        _len = uint8_t(_len + 1u);
        return true;
    }

    bool push(const void* data, uint16_t n) {
        if (data == nullptr) return false;
        const uint8_t* p = static_cast<const uint8_t*>(data);
        for (uint16_t i = 0; i < n; ++i) {
            if (!push(p[i])) return false;
        }
        return true;
    }

    // Многобайтные — little-endian, младший байт первым.
    bool pushU8(uint8_t v) { return push(v); }
    bool pushU16(uint16_t v) {
        return push(uint8_t(v)) && push(uint8_t(v >> 8));
    }
    bool pushI16(int16_t v) { return pushU16(uint16_t(v)); }
    bool pushU32(uint32_t v) {
        return push(uint8_t(v)) && push(uint8_t(v >> 8)) &&
               push(uint8_t(v >> 16)) && push(uint8_t(v >> 24));
    }
    bool pushI32(int32_t v) { return pushU32(uint32_t(v)); }

    // Вписывает LEN и CRC, закрывает кадр. Возвращает полный размер в байтах.
    uint16_t finish() {
        if (!_open) return 0;
        _buf[1] = _len;

        // CRC по LEN, TYPE и PAYLOAD — то есть по _buf[1 .. 2+_len].
        const uint16_t n = uint16_t(_len) + 2u;
#if FN_PACKET_CRC16
        const uint16_t c = crc::crc16_ccitt_update(0xFFFFu, &_buf[1], n);
        _buf[3u + _len] = uint8_t(c >> 8);
        _buf[4u + _len] = uint8_t(c);
#else
        const uint8_t c = crc::crc8_update(uint8_t(0), &_buf[1], n);
        _buf[3u + _len] = c;
#endif
        _size = uint16_t(_len) + overhead;
        _open = false;
        return _size;
    }

    const uint8_t* data() const { return _buf; }
    uint16_t size()   const { return _size; }
    uint8_t  length() const { return _len; }
    uint8_t  type()   const { return _buf[2]; }
    bool     ready()  const { return _size != 0 && !_open; }
    bool     full()   const { return _len >= MaxPayload; }
    uint8_t  free()   const { return uint8_t(MaxPayload - _len); }

private:
    uint8_t _buf[uint16_t(MaxPayload) + overhead] = {};
    uint8_t _len = 0;
    uint16_t _size = 0;
    bool _open = false;
};

template <uint8_t MaxPayload = FN_PACKET_MAX_PAYLOAD>
using PacketReader = PacketParser<MaxPayload>;

}  // namespace fn

#endif  // F_USE_PACKET
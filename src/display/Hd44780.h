#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LCD

#include "../timer/Tick.h"

namespace fn {

// Транспортно-независимое ядро HD44780 (4-битный режим).
// Наследник реализует rawWrite() — отправку одного байта в расширитель.
// Стандартная распиновка PCF8574: P0=RS, P1=RW, P2=E, P3=Backlight, P4..P7=D4..D7.
class Hd44780 {
   public:
    virtual ~Hd44780() {}

    // Полная последовательность инициализации HD44780 в 4-битном режиме.
    bool begin(uint8_t cols = 16, uint8_t rows = 2);

    void clear();
    void home();

    void setCursor(uint8_t col, uint8_t row);
    void cursor(bool on);
    void blink(bool on);
    void display(bool on);
    void scrollDisplayLeft();
    void scrollDisplayRight();
    void leftToRight();
    void rightToLeft();
    void autoscroll(bool on);

    void backlight(bool on);

    // Создать свой символ в CGRAM (0..7).
    void createChar(uint8_t slot, const uint8_t bitmap[8]);

    // Вывод.
    void write(char c);
    void write(const char* s);
    void write(const uint8_t* data, uint16_t len);

    // Форматированный вывод (минимум — только для отладки).
    void print(int v) { print(int32_t(v)); }              // AVR int = 16 бит
    void print(unsigned int v) { print(uint32_t(v)); }
    void print(int32_t v);
    void print(uint32_t v);
    void print(float v, uint8_t decimals = 2);

    uint8_t cols() const { return _cols; }
    uint8_t rows() const { return _rows; }
    bool ok() const { return _ok; }

    // Переопределить маску пинов расширителя (если модуль нестандартный).
    // rs/rw/e/backlight — номера битов 0..7.
    void setPinMap(uint8_t rs, uint8_t rw, uint8_t e, uint8_t backlight);

   protected:
    // Дефолтный конструктор (объявленный copy-ctor его подавляет).
    Hd44780() {}

    // Транспорт: отправить один байт в PCF8574. Не трогает _cache.
    virtual void rawWrite(uint8_t b) = 0;

    void pulse(uint8_t data);
    void writeNibble(uint8_t nibble, bool rs);
    void writeByte(uint8_t b, bool rs);
    void cmd(uint8_t c);
    void data(uint8_t d);

    uint8_t _cols = 16;
    uint8_t _rows = 2;

    uint8_t _maskRs = 0x01;
    uint8_t _maskRw = 0x02;
    uint8_t _maskE  = 0x04;
    uint8_t _maskBl = 0x08;

    bool    _backlight = true;
    bool    _ok = false;

    // Кэш последнего записанного байта (для быстрого backlight-toggle).
    uint8_t _cache = 0;

    // Текущее состояние регистра display control (bit2=ON, bit1=cursor, bit0=blink).
    uint8_t _displayCtrl = 0x0C;

   private:
    Hd44780(const Hd44780&) = delete;
    Hd44780& operator=(const Hd44780&) = delete;
};

// -------------------- инициализация --------------------

inline bool Hd44780::begin(uint8_t cols, uint8_t rows) {
    _cols = cols;
    _rows = rows;
    _ok = false;

    // Стартовое состояние: E=0, RS=0, RW=0, BL=1.
    _cache = _maskBl;
    rawWrite(_cache);
    delayMs(50);

    // Инициализация в 4-битном режиме (HD44780 datasheet, p.46).
    // Шлём 0x03 трижды в 8-битном «режиме» (только старший ниббл).
    writeNibble(0x03, false); delayMs(5);
    writeNibble(0x03, false); delayUs(150);
    writeNibble(0x03, false); delayUs(150);

    // Переход в 4-битный: 0x02.
    writeNibble(0x02, false); delayUs(150);

    // Теперь обычные команды, но по 4 бита за такт.
    cmd(0x28);   // Function Set: 4-bit, 2-line, 5x8
    cmd(0x08);   // Display OFF
    cmd(0x01);   // Clear
    delayMs(2);
    cmd(0x06);   // Entry Mode: increment, no shift
    cmd(0x0C);   // Display ON, cursor OFF, blink OFF

    _ok = true;
    return true;
}

// -------------------- базовые операции --------------------

inline void Hd44780::pulse(uint8_t data) {
    // E=1
    _cache = uint8_t(data | _maskE);
    rawWrite(_cache);
    delayUs(1);
    // E=0
    _cache = uint8_t(data & ~_maskE);
    rawWrite(_cache);
    delayUs(50);
}

inline void Hd44780::writeNibble(uint8_t nibble, bool rs) {
    // nibble: 0..15. Ставим D4..D7 = nibble << 4.
    uint8_t data = uint8_t((nibble & 0x0F) << 4);
    if (_backlight) data |= _maskBl;
    if (rs) data |= _maskRs;
    pulse(data);
}

inline void Hd44780::writeByte(uint8_t b, bool rs) {
    // 4-битный режим: сначала старший ниббл, потом младший.
    writeNibble(uint8_t(b >> 4), rs);
    writeNibble(uint8_t(b & 0x0F), rs);
}

inline void Hd44780::cmd(uint8_t c) {
    writeByte(c, false);
    // Для команд clear/home нужна пауза, но вызывающий сам решает.
    if (c == 0x01 || c == 0x02) delayMs(2);
}

inline void Hd44780::data(uint8_t d) {
    writeByte(d, true);
}

// -------------------- публичный API --------------------

inline void Hd44780::clear() { cmd(0x01); }
inline void Hd44780::home()  { cmd(0x02); }

inline void Hd44780::setCursor(uint8_t col, uint8_t row) {
    if (row >= _rows) row = uint8_t(_rows - 1);
    if (col >= _cols) col = uint8_t(_cols - 1);
    static const uint8_t offsets[] = { 0x00, 0x40, 0x14, 0x54 };
    uint8_t addr = uint8_t(0x80 | (offsets[row & 0x03] + col));
    cmd(addr);
}

inline void Hd44780::cursor(bool on) {
    // read-modify-write: бит 1 = cursor
    if (on) _displayCtrl |= 0x02;
    else    _displayCtrl &= uint8_t(~0x02);
    cmd(_displayCtrl);
}

inline void Hd44780::blink(bool on) {
    if (on) _displayCtrl |= 0x01;
    else    _displayCtrl &= uint8_t(~0x01);
    cmd(_displayCtrl);
}

inline void Hd44780::display(bool on) {
    if (on) _displayCtrl |= 0x04;
    else    _displayCtrl &= uint8_t(~0x04);
    cmd(_displayCtrl);
}

inline void Hd44780::scrollDisplayLeft()  { cmd(0x18); }
inline void Hd44780::scrollDisplayRight() { cmd(0x1C); }

inline void Hd44780::leftToRight() { cmd(0x06); }
inline void Hd44780::rightToLeft() { cmd(0x04); }
inline void Hd44780::autoscroll(bool on) { cmd(on ? 0x07 : 0x05); }

inline void Hd44780::backlight(bool on) {
    _backlight = on;
    _cache = uint8_t(on ? (_cache | _maskBl) : (_cache & ~_maskBl));
    rawWrite(_cache);
}

inline void Hd44780::createChar(uint8_t slot, const uint8_t bitmap[8]) {
    slot &= 0x07;
    cmd(uint8_t(0x40 | (slot << 3)));   // CGRAM addr
    for (uint8_t i = 0; i < 8; i++) data(bitmap[i]);
    cmd(0x80);   // вернуть курсор в DDRAM
}

inline void Hd44780::write(char c) { data(uint8_t(c)); }

inline void Hd44780::write(const char* s) {
    while (*s) data(uint8_t(*s++));
}

inline void Hd44780::write(const uint8_t* data_, uint16_t len) {
    while (len--) data(*data_++);
}

// -------------------- простой print --------------------

inline void Hd44780::print(int32_t v) {
    char buf[12];
    int8_t i = 11;
    buf[i] = 0;
    bool neg = v < 0;
    uint32_t uv = neg ? uint32_t(-v) : uint32_t(v);
    if (uv == 0) buf[--i] = '0';
    while (uv) { buf[--i] = char('0' + (uv % 10)); uv /= 10; }
    if (neg) buf[--i] = '-';
    write(&buf[i]);
}

inline void Hd44780::print(uint32_t v) {
    char buf[11];
    int8_t i = 10;
    buf[i] = 0;
    if (v == 0) buf[--i] = '0';
    while (v) { buf[--i] = char('0' + (v % 10)); v /= 10; }
    write(&buf[i]);
}

inline void Hd44780::print(float v, uint8_t decimals) {
    if (decimals > 4) decimals = 4;
    // Знак.
    if (v < 0) { write('-'); v = -v; }
    // Целая часть.
    uint32_t ip = uint32_t(v);
    print(ip);
    if (!decimals) return;
    write('.');
    // Дробная.
    float frac = v - float(ip);
    for (uint8_t i = 0; i < decimals; i++) {
        frac *= 10.0f;
        uint8_t d = uint8_t(frac);
        write(char('0' + d));
        frac -= float(d);
    }
}

// -------------------- нестандартные модули --------------------

inline void Hd44780::setPinMap(uint8_t rs, uint8_t rw, uint8_t e, uint8_t bl) {
    _maskRs = uint8_t(1u << rs);
    _maskRw = uint8_t(1u << rw);
    _maskE  = uint8_t(1u << e);
    _maskBl = uint8_t(1u << bl);
}

}  // namespace fn

#endif  // F_USE_LCD

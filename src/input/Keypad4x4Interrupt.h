#pragma once
#include <stdint.h>
#include <avr/interrupt.h>

#include "../core/Features.h"

#if F_USE_KEYPAD

#include "../pin/Pin.h"
#include "../timer/Timer2.h"

namespace fn {

// Keypad 4x4 с фоновым сканированием на Timer2 (раз в 2 мс).
// Loop не блокируется. Клавиша читается через read().
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
class Keypad4x4Interrupt {
   public:
    static constexpr char DEFAULT_MAP[17] = "123A456B789C*0#D";

    // Запускает Timer2 с прерыванием каждые 2 мс. Внутри ISR сканирует
    // одну строку за раз (row 0, 1, 2, 3, 0, ...) — полный скан за 8 мс.
    static bool begin() {
        Pin<R1>::output(); Pin<R1>::high();
        Pin<R2>::output(); Pin<R2>::high();
        Pin<R3>::output(); Pin<R3>::high();
        Pin<R4>::output(); Pin<R4>::high();
        Pin<C1>::inputPullup();
        Pin<C2>::inputPullup();
        Pin<C3>::inputPullup();
        Pin<C4>::inputPullup();

        _row = 0;
        _rowKey[0] = 0;
        _rowKey[1] = 0;
        _rowKey[2] = 0;
        _rowKey[3] = 0;
        _rawPrev = 0;
        _stable = 0;
        _held = 0;
        _heldRow = 0;

        // Timer2, CTC, prescaler 64: 16e6 / 64 / 125 = 2000 Гц → 2 мс.
        if (!Timer2::begin(Timer2::Mode::CTC, Timer2::Clock::Prescale64)) return false;
        Timer2::setOcrA(124);   // 125 тиков
        _selectRow(_row, true); // заранее выбрать строку 0
        Timer2::enableCompA();
        return true;
    }

    static void end() {
        Timer2::disableCompA();
        Timer2::end();
        _selectRow(_row, false);   // оставить все строки неактивными
    }

    // Прочитать текущую клавишу (0 = ничего). Не блокирует.
    static char read() {
        uint8_t s = SREG;
        cli();
        char c = _stable;
        _stable = 0;
        SREG = s;
        return c;
    }

    // Текущая удерживаемая клавиша (с антидребезгом), без сброса.
    static char current() { return _held; }

    // Для отладки: последний сырой скан.
    static char raw() { return _rawPrev; }

    // Вызывается из ISR таймера (внутри модуля).
    static void _isr() {
        // Читаем строку, выбранную в прошлом прерывании: у неё было
        // 2 мс на стабилизацию после переключения.
        uint8_t row = _row;
        uint8_t cols = _readCols();

        // Найти нажатый столбец в этой строке.
        char key = 0;
        for (uint8_t c = 0; c < 4; c++) {
            if (!(cols & (1u << c))) {
                key = DEFAULT_MAP[row * 4 + c];
                break;
            }
        }
        _rawPrev = key;

        // Отпустить прочитанную строку и выбрать следующую.
        _selectRow(row, false);
        _row = (row + 1) & 3;
        _selectRow(_row, true);

        // Каждая строка сканируется раз в 8 мс. Совпадение с прошлым
        // сканом этой же строки — антидребезг на два отсчёта.
        char prev = _rowKey[row];
        _rowKey[row] = key;

        // Отпускание: строка удерживаемой клавиши больше её не видит.
        if (_held && _heldRow == row && key != _held) _held = 0;

        // Нажатие: та же клавиша в этой строке второй скан подряд.
        if (key && key == prev && key != _held) {
            _held = key;
            _heldRow = row;
            _stable = key;   // «событие» нажатия — прочитается через read()
        }
    }

   private:
    static void _selectRow(uint8_t r, bool low) {
        switch (r) {
            case 0: Pin<R1>::write(!low); break;
            case 1: Pin<R2>::write(!low); break;
            case 2: Pin<R3>::write(!low); break;
            case 3: Pin<R4>::write(!low); break;
        }
    }

    static uint8_t _readCols() {
        uint8_t v = 0;
        if (Pin<C1>::read()) v |= 0x01;
        if (Pin<C2>::read()) v |= 0x02;
        if (Pin<C3>::read()) v |= 0x04;
        if (Pin<C4>::read()) v |= 0x08;
        return v;
    }

    // C++11: объявления в классе, определения — ниже (как у DEFAULT_MAP).
    static volatile uint8_t _row;
    static volatile char    _rawPrev;
    static volatile char    _stable;
    static volatile char    _held;
    static volatile uint8_t _heldRow;
    static volatile char    _rowKey[4];
};

template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
constexpr char Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::DEFAULT_MAP[17];

template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile uint8_t Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_row = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile char Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_rawPrev = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile char Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_stable = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile char Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_held = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile uint8_t Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_heldRow = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
volatile char Keypad4x4Interrupt<R1, R2, R3, R4, C1, C2, C3, C4>::_rowKey[4] = { 0, 0, 0, 0 };

}  // namespace fn

#endif  // F_USE_KEYPAD

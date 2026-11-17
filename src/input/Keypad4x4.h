#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_KEYPAD

#include "../pin/Pin.h"
#include "../logic/Debounce.h"
#include "../timer/Tick.h"

namespace fn {

// Мембранная клавиатура 4x4. Строки — выходы (R1..R4), столбцы — входы с pull-up (C1..C4).
// Символы по умолчанию: "123A456B789C*0#D".
// Сканирование — по одному вызову tick() в loop(); время ~4 мс (4 строки * 1 мс).
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
class Keypad4x4 {
   public:
    static constexpr uint8_t ROWS = 4;
    static constexpr uint8_t COLS = 4;
    static constexpr uint8_t NO_KEY = 0xFF;

    static constexpr char DEFAULT_MAP[17] = "123A456B789C*0#D";

    // Инициализация пинов. Строки — LOW (неактивны), столбцы — INPUT_PULLUP.
    static void begin() {
        Pin<R1>::output(); Pin<R1>::high();
        Pin<R2>::output(); Pin<R2>::high();
        Pin<R3>::output(); Pin<R3>::high();
        Pin<R4>::output(); Pin<R4>::high();
        Pin<C1>::inputPullup();
        Pin<C2>::inputPullup();
        Pin<C3>::inputPullup();
        Pin<C4>::inputPullup();

        _deb.reset(0);
        _repeatChar = 0;
        _repeatNext = 0;
    }

    // Сканировать один раз. Возвращает символ нажатой клавиши или 0,
    // если ничего не нажато. Если нажато несколько — возвращает первую найденную.
    static char scan() {
        for (uint8_t r = 0; r < ROWS; r++) {
            _selectRow(r, true);
            delayUs(2);   // дать линиям стабилизироваться

            uint8_t cols = _readCols();
            if (cols != 0x0F) {           // хоть один столбец «прижат»
                for (uint8_t c = 0; c < COLS; c++) {
                    if (!(cols & (1u << c))) {
                        _selectRow(r, false);
                        return DEFAULT_MAP[r * 4 + c];
                    }
                }
            }
            _selectRow(r, false);
        }
        return 0;
    }

    // Сканировать с антидребезгом. Вызывать в loop().
    // Возвращает символ, когда клавиша нажата стабильно debounceMs,
    // и НЕ возвращает его снова, пока клавиша не отпущена.
    static char tick(uint16_t debounceMs = 20) {
        _deb.setWindow(debounceMs);
        if (_deb.update(scan())) {
            char stable = _deb.state();
            if (stable) return stable;
        }
        return 0;
    }

    // Символ, удерживаемый сейчас (с антидребезгом).
    static char current() { return _deb.state(); }

    // Повтор: если клавиша удерживается, tickRepeat() возвращает её каждые repeatMs.
    // initialDelayMs — задержка до первого повтора.
    static char tickRepeat(uint16_t repeatMs = 150, uint16_t initialDelayMs = 500) {
        char c = tick();
        uint16_t now = uint16_t(millis());
        char stable = _deb.state();

        if (c) { _repeatNext = now + initialDelayMs; _repeatChar = c; return c; }

        if (stable && _repeatChar && int16_t(now - _repeatNext) >= 0) {
            _repeatNext = now + repeatMs;
            return _repeatChar;
        }
        if (!stable) _repeatChar = 0;
        return 0;
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
    static logic::DebounceValue<char> _deb;
    static char     _repeatChar;
    static uint16_t _repeatNext;
};

// Определение для C++11/14: static constexpr-массив, индексируемый в рантайме,
// иначе линкер не находит символ.
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
constexpr char Keypad4x4<R1, R2, R3, R4, C1, C2, C3, C4>::DEFAULT_MAP[17];

template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
logic::DebounceValue<char> Keypad4x4<R1, R2, R3, R4, C1, C2, C3, C4>::_deb(20, 0);
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
char Keypad4x4<R1, R2, R3, R4, C1, C2, C3, C4>::_repeatChar = 0;
template <uint8_t R1, uint8_t R2, uint8_t R3, uint8_t R4,
          uint8_t C1, uint8_t C2, uint8_t C3, uint8_t C4>
uint16_t Keypad4x4<R1, R2, R3, R4, C1, C2, C3, C4>::_repeatNext = 0;

}  // namespace fn

#endif  // F_USE_KEYPAD

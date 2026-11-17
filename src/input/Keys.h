#pragma once
// Клавиатуры: матричная N×M и аналоговая (резистивная) на одном пине АЦП.
//
// Для каждой есть два варианта:
//   *Min  — только индекс нажатой клавиши;
//   без Min — полный набор событий кнопки (клики, удержание, шаги).
//
// Пример:
//   const uint8_t rows[] = {9, 8, 7, 6};
//   const uint8_t cols[] = {5, 4, 3, 2};
//   fn::MatrixKey keypad(rows, 4, cols, 4);
//   void loop() {
//       if (keypad.tick() && keypad.click()) Serial.println(keypad.key());
//   }

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_KEYPAD

#include <Arduino.h>
#include <avr/pgmspace.h>

#include "../pin/PinRef.h"
#include "../timer/Tick.h"

#if F_USE_LOGIC
#include "../logic/Button.h"
#endif

#ifndef F_KEY_SCAN_MS
#define F_KEY_SCAN_MS 30
#endif

namespace fn {

// =========================== VirtualKeySimple ===========================
// Простая клавиша: tick() возвращает true в момент нажатия.
class VirtualKeySimple {
   public:
    bool tick() {
        uint16_t now = uint16_t(millis());
        if (uint16_t(now - _tmr) < _prd) return false;
        _tmr = now;

        int16_t cur = scanKey();
        if (_prev == cur) return false;

        _prev = cur;
        return cur >= 0;
    }

    // Период сканирования, мс.
    void setScanPeriod(uint8_t ms) { _prd = ms; }

    // Индекс нажатой клавиши или -1.
    int16_t key() const { return _prev; }

    // Символ клавиши из строки-карты или '\0'.
    char key(const char* keys) const {
        return (keys && _prev >= 0) ? keys[_prev] : '\0';
    }

   protected:
    virtual int16_t scanKey() = 0;

   private:
    uint16_t _tmr = 0;
    uint8_t _prd = F_KEY_SCAN_MS;
    int16_t _prev = -1;
};

#if F_USE_LOGIC

// =========================== VirtualKey ===========================
// Клавиша с полным автоматом событий ButtonCore.
class VirtualKey : public logic::ButtonCore {
   public:
    bool tick() {
        uint16_t now = uint16_t(millis());
        if (uint16_t(now - _tmr) >= _prd) {
            _tmr = now;
            int16_t cur = scanKey();

            if (_prev >= 0 && cur >= 0 && _prev != cur) reset();
            else if (!busy()) _n = -1;

            if (cur >= 0) _n = cur;
            _prev = cur;
        }
        return poll(_prev >= 0);
    }

    void setScanPeriod(uint8_t ms) { _prd = ms; }

    int16_t key() const { return _n; }

    char key(const char* keys) const {
        return (keys && _n >= 0) ? keys[_n] : '\0';
    }

   protected:
    virtual int16_t scanKey() = 0;

   private:
    uint16_t _tmr = 0;
    uint8_t _prd = F_KEY_SCAN_MS;
    int16_t _prev = -1;
    int16_t _n = -1;
};

#endif  // F_USE_LOGIC

// =========================== AnalogKeyScanner ===========================
// Сравнение значения АЦП с таблицей ожидаемых сигналов.
class AnalogKeyScanner {
   public:
    // sigs — таблица значений; pgm=true, если таблица лежит в PROGMEM.
    AnalogKeyScanner(const int16_t* sigs, uint8_t len, uint8_t window = 20,
                  bool sigsInPgm = false)
        : _sigs(sigs),
          _len(len),
          _offs(uint8_t(window / 2)),
          _pgm(sigsInPgm) {}

    // Индекс клавиши или -1.
    int16_t scan(int16_t signal) const {
        for (uint8_t i = 0; i < _len; i++) {
            int16_t expected = sigAt(i);
            int16_t diff = int16_t(expected - signal);
            if (diff >= -int16_t(_offs) && diff <= int16_t(_offs)) {
                return int16_t(i);
            }
        }
        return -1;
    }

    // Ширина окна сигнала.
    void setWindow(uint8_t window) { _offs = uint8_t(window / 2); }

   private:
    int16_t sigAt(uint8_t i) const {
        if (_pgm) return int16_t(pgm_read_word(_sigs + i));
        return _sigs[i];
    }

    const int16_t* _sigs;
    uint8_t _len;
    uint8_t _offs;
    bool _pgm;
};

// =========================== AnalogKeySimple ===========================
class AnalogKeySimple : public AnalogKeyScanner, public VirtualKeySimple {
   public:
    AnalogKeySimple(uint8_t pin, const int16_t* sigs, uint8_t len,
                 uint8_t window = 20, bool sigsInPgm = false)
        : AnalogKeyScanner(sigs, len, window, sigsInPgm), _pin(pin) {}

   private:
    int16_t scanKey() override {
        return AnalogKeyScanner::scan(int16_t(analogRead(_pin)));
    }

    uint8_t _pin;
};

// =========================== AnalogKey ===========================
#if F_USE_LOGIC
class AnalogKey : public AnalogKeyScanner, public VirtualKey {
   public:
    AnalogKey(uint8_t pin, const int16_t* sigs, uint8_t len,
              uint8_t window = 20, bool sigsInPgm = false)
        : AnalogKeyScanner(sigs, len, window, sigsInPgm), _pin(pin) {}

   private:
    int16_t scanKey() override {
        return AnalogKeyScanner::scan(int16_t(analogRead(_pin)));
    }

    uint8_t _pin;
};
#endif

// =========================== MatrixKeyScanner ===========================
// Матрица N×M: строки — входы, столбцы по очереди становятся выходами LOW.
// Массивы пинов хранятся снаружи; в объекте только указатели и счётчики.
class MatrixKeyScanner {
   public:
    MatrixKeyScanner(const uint8_t* rowPins, uint8_t rows,
                  const uint8_t* colPins, uint8_t cols,
                  uint8_t mode = INPUT_PULLUP)
        : _rowPins(rowPins),
          _colPins(colPins),
          _rows(rows),
          _cols(cols),
          _mode(mode) {
        for (uint8_t r = 0; r < _rows; r++) PinRef::pinMode(_rowPins[r], _mode);
        for (uint8_t c = 0; c < _cols; c++) PinRef::pinMode(_colPins[c], _mode);
    }

    // Индекс клавиши (r * cols + c) или -1.
    int16_t scan() const {
        for (uint8_t c = 0; c < _cols; c++) {
            // Сначала гасим подтяжку, потом переводим в выход — без
            // короткого импульса HIGH на столбце.
            PinRef::pinLow(_colPins[c]);
            PinRef::pinMode(_colPins[c], OUTPUT);
            delayUs(2);

            for (uint8_t r = 0; r < _rows; r++) {
                if (!PinRef::pinRead(_rowPins[r])) {
                    PinRef::pinMode(_colPins[c], _mode);
                    return int16_t(uint16_t(r) * _cols + c);
                }
            }

            PinRef::pinMode(_colPins[c], _mode);
        }
        return -1;
    }

   private:
    const uint8_t* _rowPins;
    const uint8_t* _colPins;
    uint8_t _rows;
    uint8_t _cols;
    uint8_t _mode;
};

// =========================== MatrixKeySimple ===========================
class MatrixKeySimple : public MatrixKeyScanner, public VirtualKeySimple {
   public:
    MatrixKeySimple(const uint8_t* rowPins, uint8_t rows,
                 const uint8_t* colPins, uint8_t cols,
                 uint8_t mode = INPUT_PULLUP)
        : MatrixKeyScanner(rowPins, rows, colPins, cols, mode) {}

   private:
    int16_t scanKey() override { return MatrixKeyScanner::scan(); }
};

// =========================== MatrixKey ===========================
#if F_USE_LOGIC
class MatrixKey : public MatrixKeyScanner, public VirtualKey {
   public:
    MatrixKey(const uint8_t* rowPins, uint8_t rows,
              const uint8_t* colPins, uint8_t cols,
              uint8_t mode = INPUT_PULLUP)
        : MatrixKeyScanner(rowPins, rows, colPins, cols, mode) {}

   private:
    int16_t scanKey() override { return MatrixKeyScanner::scan(); }
};
#endif

}  // namespace fn

#endif  // F_USE_KEYPAD

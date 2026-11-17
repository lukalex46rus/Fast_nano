#pragma once
#include <Arduino.h>

#include "../../core/Features.h"

#if F_USE_GFX

// движок бегущей строки для GraphicsDisplay

#include "../GraphicsDisplay.h"

#define SCROLL_IDLE 0
#define SCROLL_MOVE 1
#define SCROLL_FINISH 2

namespace fn {
namespace disp {

class ScrollingText {
   public:
    ScrollingText(GraphicsDisplay* gfx) : _gfx(gfx) {
        _x0 = _y = 0;
        _x1 = _gfx->width();
    }

    // установить текст const char*
    void setText(const char* str) {
        _str = str;
        _p = 0;
        _len = _gfx->textLength(_str) * 6;
    }

    // установить текст String
    void setText(String& str) {
        setText(str.c_str());
    }

    // установить текст из PROGMEM (глобальный)
    void setProgmemText(PGM_P str) {
        _str = str;
        _p = 1;
        _len = _gfx->progmemTextLength(_str) * 6;
    }

    // инвертировать текст
    void invertText(bool inv) {
        _invert = inv;
    }

    // масштаб текста
    void setScale(uint8_t scale) {
        _scale = constrain(scale, 1, 4);
    }

    // режим вывода текста GFX_ADD/GFX_REPLACE
    void textDisplayMode(bool mode) {
        _tmode = mode;
    }

    // установить окно (x0, x1, y)
    void setWindow(int16_t x0, int16_t x1, int16_t y) {
        _x0 = x0;
        _x1 = x1;
        _y = y;
    }

    // установить скорость (пикселей в секунду)
    void setSpeed(uint16_t pixPerSec) {
        if (!pixPerSec) pixPerSec = 1;
        _prd = 1000 / pixPerSec;
    }

    // запустить бегущую строку с начала
    void start() {
        _pos = _x1 + 1;
        resume();
    }

    // остановить бегущую строку
    void stop() {
        _tmr = 0;
    }

    // продолжить движение с момента остановки
    void resume() {
        _tmr = (uint16_t)millis();
        if (!_tmr) _tmr = 1;
    }

    // тикер. Вернёт 0 в холостом, 1 при новом шаге, 2 при завершении движения
    // Можно передать false чтобы дисплей не обновлялся сам
    uint8_t tick(bool update = true) {
        if (!_str) return 0;
        if (_tmr && (uint16_t)((uint16_t)millis() - _tmr) >= _prd) {
            resume();
            return tickManual(update);
        }
        return SCROLL_IDLE;
    }

    // сдвинуть строку на 1 пиксель. Можно передать false чтобы дисплей не обновлялся сам
    uint8_t tickManual(bool update = true) {
        if (!_str) return 0;
        TextConfig cfg = _gfx->cfg;
        _gfx->setScale(_scale);
        _gfx->invertText(_invert);
        _gfx->textDisplayMode(_tmode);
        _gfx->setTextBound(_x0, _x1);
        _gfx->setCursor(_pos, _y);
        if (_p) _gfx->print((const __FlashStringHelper*)_str);
        else _gfx->print(_str);
        _gfx->cfg = cfg;
        if (update) _gfx->update();
        if (--_pos <= _x0 - (int16_t)(_len * _scale)) {
            start();
            return SCROLL_FINISH;
        }
        return SCROLL_MOVE;
    }

   private:
    GraphicsDisplay* _gfx;
    const char* _str = nullptr;
    uint16_t _tmr = 0, _prd = 50;
    uint16_t _len = 0;
    int16_t _x0, _x1, _y;
    int16_t _pos = 0;
    bool _p = 0;
    uint8_t _scale = 1;
    bool _invert = 0, _tmode = 0;
};

}  // namespace disp
}  // namespace fn

#endif  // F_USE_GFX
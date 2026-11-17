#pragma once
#include "../core/Features.h"

// LcdDisplay работает через статическую шину I2c.
#if F_USE_LCD && F_USE_I2C

#include <Arduino.h>

#include "../bus/I2c.h"

#include "lcd/Lcd1602Core.h"
#include "lcd/Glyph64.h"
#include "lcd/LcdPrinter.h"

namespace fn {
namespace disp {

// статическая шина Fast_nano в виде Wire-подобного интерфейса для LcdDisplay
struct LcdWire {
    void begin() {
        I2c::begin();
    }
    void beginTransmission(uint8_t a) {
        I2c::beginTransmission(a);
    }
    size_t write(uint8_t b) {
        return I2c::write(b);
    }
    uint8_t endTransmission() {
        return I2c::endTransmission();
    }
};

// шина Fast_nano без Arduino Print
class Lcd1602Core : public LcdCore<LcdWire> {
   public:
    Lcd1602Core(uint8_t address, uint8_t columns, uint8_t rows) : LcdCore<LcdWire>(wire(), address, columns, rows) {}

   private:
    static LcdWire& wire() {
        static LcdWire w;
        return w;
    }
};

// шина Fast_nano + Arduino Print
using LcdDisplay = LcdPrinter<Lcd1602Core>;

}  // namespace disp
}  // namespace fn

// UTF-8 слой требует уже объявленный Lcd1602Core
#include "lcd/Utf8Lcd1602.h"

#endif  // F_USE_LCD && F_USE_I2C

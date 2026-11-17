#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LCD && F_USE_I2C

#include "Hd44780.h"

namespace fn {

// HD44780 16x2 через PCF8574 (I2C). Вся логика дисплея — в Hd44780,
// здесь только транспорт.
class Lcd1602I2c : public Hd44780 {
   public:
    // addr: 0x27 (PCF8574) или 0x3F (PCF8574A).
    // cols/rows: 16/2 для 1602, 20/4 для 2004.
    bool begin(uint8_t addr = 0x27, uint8_t cols = 16, uint8_t rows = 2);

    uint8_t address() const { return _addr; }

   protected:
    void rawWrite(uint8_t b) override;

   private:
    uint8_t _addr = 0x27;
};

}  // namespace fn

#endif  // F_USE_LCD && F_USE_I2C

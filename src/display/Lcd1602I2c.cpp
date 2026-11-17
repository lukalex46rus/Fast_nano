#include "../core/Config.h"

#if F_USE_LCD && F_USE_I2C

#include "Lcd1602I2c.h"
#include "../bus/I2c.h"

namespace fn {

bool Lcd1602I2c::begin(uint8_t addr, uint8_t cols, uint8_t rows) {
    _addr = addr;
    if (!I2c::ping(_addr)) { _ok = false; return false; }
    return Hd44780::begin(cols, rows);
}

void Lcd1602I2c::rawWrite(uint8_t b) {
    I2c::beginTransmission(_addr);
    I2c::write(b);
    I2c::endTransmission();
}

}  // namespace fn

#endif  // F_USE_LCD && F_USE_I2C

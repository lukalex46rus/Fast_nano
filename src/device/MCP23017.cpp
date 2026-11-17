#include "../core/Config.h"
#include "MCP23017.h"

#if F_USE_MCP23017 && F_USE_I2C

namespace fn {

bool MCP23017::begin(uint8_t addr) {
    setAddress(addr);
    return sync();
}

bool MCP23017::sync() {
    return readReg(RegIodirA, _iodir[0]) &&
           readReg(RegIodirB, _iodir[1]) &&
           readReg(RegGppuA,  _gppu[0]) &&
           readReg(RegGppuB,  _gppu[1]) &&
           readReg(RegOlatA,  _olat[0]) &&
           readReg(RegOlatB,  _olat[1]);
}

bool MCP23017::resetToDefaults() {
    // Сначала делаем все пины входами — безопаснее.
    if (!writeReg(RegIodirA, 0xFF)) return false;
    if (!writeReg(RegIodirB, 0xFF)) return false;

    // Полярность входа по умолчанию.
    if (!writeReg(RegIpolA, 0x00)) return false;
    if (!writeReg(RegIpolB, 0x00)) return false;

    // Выключаем прерывания.
    if (!writeReg(RegGpintenA, 0x00)) return false;
    if (!writeReg(RegGpintenB, 0x00)) return false;

    // Сбрасываем сравнение для прерываний.
    if (!writeReg(RegDefvalA, 0x00)) return false;
    if (!writeReg(RegDefvalB, 0x00)) return false;

    // INTCON = 0: сравнение с предыдущим состоянием пина.
    if (!writeReg(RegIntconA, 0x00)) return false;
    if (!writeReg(RegIntconB, 0x00)) return false;

    // IOCON = 0: BANK = 0, MIRROR = 0, SEQOP = 0, ODR = 0, INTPOL = 0.
    if (!writeReg(RegIoconA, 0x00)) return false;
    if (!writeReg(RegIoconB, 0x00)) return false;

    // Pull-up выключены.
    if (!writeReg(RegGppuA, 0x00)) return false;
    if (!writeReg(RegGppuB, 0x00)) return false;

    // Выходные защёлки в 0.
    if (!writeReg(RegOlatA, 0x00)) return false;
    if (!writeReg(RegOlatB, 0x00)) return false;

    _iodir[0] = 0xFF;
    _iodir[1] = 0xFF;

    _gppu[0] = 0x00;
    _gppu[1] = 0x00;

    _olat[0] = 0x00;
    _olat[1] = 0x00;

    return true;
}

bool MCP23017::ping() {
    bool ok = I2c::ping(_addr);
    _err = ok ? I2c::Ok : I2c::lastError();
    return ok;
}

bool MCP23017::writeReg(uint8_t reg, uint8_t value) {
    I2c::beginTransmission(_addr);

    if (I2c::write(reg) != 1 || I2c::write(value) != 1) {
        _err = I2c::lastError();
        I2c::endTransmission(true);
        return false;
    }

    uint8_t e = I2c::endTransmission(true);
    _err = I2c::Error(e);
    return e == I2c::Ok;
}

bool MCP23017::readReg(uint8_t reg, uint8_t& value) {
    I2c::beginTransmission(_addr);

    if (I2c::write(reg) != 1) {
        _err = I2c::lastError();
        I2c::endTransmission(true);
        return false;
    }

    uint8_t e = I2c::endTransmission(false);
    _err = I2c::Error(e);

    if (e != I2c::Ok) {
        // На всякий случай отправляем STOP, если что-то пошло не так.
        I2c::endTransmission(true);
        return false;
    }

    if (I2c::requestFrom(_addr, 1, true) != 1) {
        _err = I2c::lastError();
        return false;
    }

    int b = I2c::read();
    if (b < 0) {
        _err = I2c::Other;
        return false;
    }

    value = uint8_t(b);
    _err = I2c::Ok;
    return true;
}

bool MCP23017::pinMode(uint8_t pin, uint8_t mode) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t dir = _iodir[p];
    uint8_t pull = _gppu[p];

    if (mode == ModeOutput) {
        dir &= ~m;
        pull &= ~m;
    } else if (mode == ModeInputPullup) {
        dir |= m;
        pull |= m;
    } else {
        dir |= m;
        pull &= ~m;
    }

    if (dir != _iodir[p]) {
        if (!writeReg(uint8_t(RegIodirA + p), dir)) return false;
        _iodir[p] = dir;
    }

    if (pull != _gppu[p]) {
        if (!writeReg(uint8_t(RegGppuA + p), pull)) return false;
        _gppu[p] = pull;
    }

    return true;
}

bool MCP23017::setPullUp(uint8_t pin, bool on) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t pull = _gppu[p];

    if (on) pull |= m;
    else    pull &= ~m;

    if (pull == _gppu[p]) return true;

    if (!writeReg(uint8_t(RegGppuA + p), pull)) return false;

    _gppu[p] = pull;
    return true;
}

bool MCP23017::writePin(uint8_t pin, bool level) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t value = _olat[p];

    if (level) value |= m;
    else       value &= ~m;

    if (value == _olat[p]) return true;

    if (!writeReg(uint8_t(RegOlatA + p), value)) return false;

    _olat[p] = value;
    return true;
}

bool MCP23017::togglePin(uint8_t pin) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t value = uint8_t(_olat[p] ^ m);

    if (!writeReg(uint8_t(RegOlatA + p), value)) return false;

    _olat[p] = value;
    return true;
}

bool MCP23017::readPin(uint8_t pin, bool& level) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t gpio = 0;
    if (!readReg(uint8_t(RegGpioA + p), gpio)) return false;

    level = (gpio & m) != 0;
    return true;
}

int MCP23017::readPin(uint8_t pin) {
    bool level = false;
    if (!readPin(pin, level)) return -1;
    return level ? 1 : 0;
}

bool MCP23017::writePort(uint8_t port, uint8_t value) {
    if (!validPort(port)) {
        _err = I2c::Other;
        return false;
    }

    if (!writeReg(uint8_t(RegOlatA + port), value)) return false;

    _olat[port] = value;
    return true;
}

bool MCP23017::readPort(uint8_t port, uint8_t& value) {
    if (!validPort(port)) {
        _err = I2c::Other;
        return false;
    }

    return readReg(uint8_t(RegGpioA + port), value);
}

int MCP23017::readPort(uint8_t port) {
    uint8_t value = 0;
    if (!readPort(port, value)) return -1;
    return int(value);
}

bool MCP23017::writeAll(uint16_t value) {
    return writePort(0, uint8_t(value & 0xFF)) &&
           writePort(1, uint8_t(value >> 8));
}

bool MCP23017::readAll(uint16_t& value) {
    uint8_t a = 0;
    uint8_t b = 0;

    if (!readPort(0, a)) return false;
    if (!readPort(1, b)) return false;

    value = uint16_t(a) | uint16_t(uint16_t(b) << 8);
    return true;
}

bool MCP23017::writeMask(uint16_t mask, uint16_t value) {
    for (uint8_t p = 0; p < 2; ++p) {
        uint8_t m = uint8_t(mask >> (8 * p));

        if (!m) continue;

        uint8_t v = uint8_t(value >> (8 * p));
        uint8_t newv = uint8_t((_olat[p] & ~m) | (v & m));

        if (newv == _olat[p]) continue;

        if (!writeReg(uint8_t(RegOlatA + p), newv)) return false;

        _olat[p] = newv;
    }

    return true;
}

bool MCP23017::setPortDirection(uint8_t port, uint8_t dirMask) {
    if (!validPort(port)) {
        _err = I2c::Other;
        return false;
    }

    if (dirMask == _iodir[port]) return true;

    if (!writeReg(uint8_t(RegIodirA + port), dirMask)) return false;

    _iodir[port] = dirMask;
    return true;
}

bool MCP23017::setPortPullUps(uint8_t port, uint8_t pullMask) {
    if (!validPort(port)) {
        _err = I2c::Other;
        return false;
    }

    if (pullMask == _gppu[port]) return true;

    if (!writeReg(uint8_t(RegGppuA + port), pullMask)) return false;

    _gppu[port] = pullMask;
    return true;
}

bool MCP23017::configureInterruptPins(bool mirror, bool openDrain, bool activeHigh) {
    uint8_t iocon = 0;

    // Пробуем сохранить текущие настройки, но если чтение не удалось —
    // просто пишем безопасное значение.
    readReg(RegIoconA, iocon);

    iocon &= ~uint8_t(IoconBank | IoconMirror | IoconOdr | IoconIntpol);

    if (mirror)     iocon |= IoconMirror;
    if (openDrain)  iocon |= IoconOdr;
    if (activeHigh) iocon |= IoconIntpol;

    // Работаем в BANK = 0.
    iocon &= ~IoconBank;

    return writeReg(RegIoconA, iocon) &&
           writeReg(RegIoconB, iocon);
}

bool MCP23017::setupInterrupt(uint8_t pin,
                              bool enable,
                              bool compareDefVal,
                              bool defVal) {
    if (!validPin(pin)) {
        _err = I2c::Other;
        return false;
    }

    uint8_t p = portOf(pin);
    uint8_t m = maskOf(pin);

    uint8_t gpinten = 0;
    uint8_t intcon  = 0;
    uint8_t def     = 0;

    if (!readReg(uint8_t(RegGpintenA + p), gpinten)) return false;
    if (!readReg(uint8_t(RegIntconA + p), intcon))   return false;
    if (!readReg(uint8_t(RegDefvalA + p), def))      return false;

    if (enable) {
        gpinten |= m;

        if (compareDefVal) {
            intcon |= m;

            if (defVal) def |= m;
            else        def &= ~m;
        } else {
            intcon &= ~m;
        }
    } else {
        gpinten &= ~m;
        intcon  &= ~m;
        def     &= ~m;
    }

    // Сначала конфигурация, затем включаем прерывание.
    return writeReg(uint8_t(RegDefvalA + p), def) &&
           writeReg(uint8_t(RegIntconA + p), intcon) &&
           writeReg(uint8_t(RegGpintenA + p), gpinten);
}

bool MCP23017::readInterruptFlags(uint16_t& flags) {
    uint8_t a = 0;
    uint8_t b = 0;

    if (!readReg(RegIntfA, a)) return false;
    if (!readReg(RegIntfB, b)) return false;

    flags = uint16_t(a) | uint16_t(uint16_t(b) << 8);
    return true;
}

bool MCP23017::readInterruptCapture(uint16_t& capture) {
    uint8_t a = 0;
    uint8_t b = 0;

    if (!readReg(RegIntcapA, a)) return false;
    if (!readReg(RegIntcapB, b)) return false;

    capture = uint16_t(a) | uint16_t(uint16_t(b) << 8);
    return true;
}

bool MCP23017::clearInterrupts() {
    uint8_t a = 0;
    uint8_t b = 0;

    // Чтение GPIO обычно сбрасывает флаги прерываний.
    return readReg(RegGpioA, a) &&
           readReg(RegGpioB, b);
}

}  // namespace fn

#endif  // F_USE_MCP23017 && F_USE_I2C
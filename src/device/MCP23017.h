#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_MCP23017 && F_USE_I2C

#include "../bus/I2c.h"

namespace fn {

// MCP23017: 16-битный I2C GPIO-экспандер.
//
// Нумерация пинов:
// 0..7   = PA0..PA7
// 8..15  = PB0..PB7
//
// Режимы совместимы с Arduino по значениям:
// 0 = INPUT, 1 = OUTPUT, 2 = INPUT_PULLUP.
class MCP23017 {
   public:
    enum : uint8_t {
        DefaultAddress = 0x20,

        ModeInput = 0,
        ModeOutput = 1,
        ModeInputPullup = 2,
    };

    MCP23017() = default;

    explicit MCP23017(uint8_t addr) { setAddress(addr); }

    // MCP23017 address = 0b0100 A2 A1 A0.
    // Пример: все адресные пины к земле -> 0x20.
    static uint8_t makeAddress(uint8_t a2, uint8_t a1, uint8_t a0) {
        return uint8_t(0x20 | ((a2 & 1) << 2) | ((a1 & 1) << 1) | (a0 & 1));
    }

    // Перед вызовом обычно нужно один раз сделать I2c::begin().
    // begin() читает актуальные регистры и синхронизирует внутренний кэш.
    bool begin(uint8_t addr = DefaultAddress);

    // Прочитать кэш из чипа.
    bool sync();

    // Программный сброс основных регистров в безопасное состояние:
    // все пины — входы, pull-up выключены, выходы 0, прерывания выключены.
    bool resetToDefaults();

    bool ping();

    void setAddress(uint8_t addr) { _addr = addr & 0x7F; }
    uint8_t address() const { return _addr; }

    I2c::Error lastError() const { return _err; }

    // --- pin API ---

    bool pinMode(uint8_t pin, uint8_t mode);

    bool setPullUp(uint8_t pin, bool on);

    bool writePin(uint8_t pin, bool level);
    bool togglePin(uint8_t pin);

    bool readPin(uint8_t pin, bool& level);
    int readPin(uint8_t pin);   // -1 при ошибке, иначе 0/1

    // Удобные обёртки в Arduino-стиле.
    void digitalWrite(uint8_t pin, bool level) { writePin(pin, level); }
    int digitalRead(uint8_t pin) { return readPin(pin); }

    // --- port API ---
    // port = 0 -> GPA, port = 1 -> GPB.

    bool writePort(uint8_t port, uint8_t value);
    bool readPort(uint8_t port, uint8_t& value);
    int readPort(uint8_t port);   // -1 при ошибке

    // value: bit 0..7 = PA0..PA7, bit 8..15 = PB0..PB7.
    bool writeAll(uint16_t value);
    bool readAll(uint16_t& value);

    // Записать только биты, указанные в mask.
    bool writeMask(uint16_t mask, uint16_t value);

    // dirMask: 1 = input, 0 = output.
    bool setPortDirection(uint8_t port, uint8_t dirMask);

    // pullMask: 1 = включить pull-up, 0 = выключить.
    bool setPortPullUps(uint8_t port, uint8_t pullMask);

    // --- interrupts ---

    // mirror = true: INTA и INTB работают вместе.
    // openDrain = true: INT-выходы в открытом стоке.
    // activeHigh = true: активный высокий уровень.
    bool configureInterruptPins(bool mirror, bool openDrain, bool activeHigh);

    // compareDefVal = false: прерывание при изменении состояния пина.
    // compareDefVal = true:  прерывание при несовпадении с defVal.
    bool setupInterrupt(uint8_t pin,
                        bool enable,
                        bool compareDefVal = false,
                        bool defVal = false);

    bool readInterruptFlags(uint16_t& flags);
    bool readInterruptCapture(uint16_t& capture);
    bool clearInterrupts();

    // --- low-level register access ---

    bool writeRegister(uint8_t reg, uint8_t value) { return writeReg(reg, value); }
    bool readRegister(uint8_t reg, uint8_t& value) { return readReg(reg, value); }

   private:
    enum Register : uint8_t {
        RegIodirA = 0x00,
        RegIodirB = 0x01,
        RegIpolA = 0x02,
        RegIpolB = 0x03,
        RegGpintenA = 0x04,
        RegGpintenB = 0x05,
        RegDefvalA = 0x06,
        RegDefvalB = 0x07,
        RegIntconA = 0x08,
        RegIntconB = 0x09,
        RegIoconA = 0x0A,
        RegIoconB = 0x0B,
        RegGppuA = 0x0C,
        RegGppuB = 0x0D,
        RegIntfA = 0x0E,
        RegIntfB = 0x0F,
        RegIntcapA = 0x10,
        RegIntcapB = 0x11,
        RegGpioA = 0x12,
        RegGpioB = 0x13,
        RegOlatA = 0x14,
        RegOlatB = 0x15,
    };

    enum IoconBit : uint8_t {
        IoconBank = 0x80,
        IoconMirror = 0x40,
        IoconSeqop = 0x20,
        IoconDisslw = 0x10,
        IoconHaen = 0x08,
        IoconOdr = 0x04,
        IoconIntpol = 0x02,
    };

    bool writeReg(uint8_t reg, uint8_t value);
    bool readReg(uint8_t reg, uint8_t& value);

    static bool validPin(uint8_t pin) { return pin < 16; }
    static bool validPort(uint8_t port) { return port < 2; }
    static uint8_t portOf(uint8_t pin) { return uint8_t(pin >> 3); }
    static uint8_t maskOf(uint8_t pin) { return uint8_t(1u << (pin & 7)); }

    uint8_t _addr = DefaultAddress;
    I2c::Error _err = I2c::Ok;

    // Кэш регистров, чтобы не делать лишнее чтение при каждом изменении.
    // IODIR: 1 = input, 0 = output. GPPU: 1 = pull-up. OLAT: выходная защёлка.
    uint8_t _iodir[2] = {0xFF, 0xFF};
    uint8_t _gppu[2] = {0x00, 0x00};
    uint8_t _olat[2] = {0x00, 0x00};
};

// Лёгкая обёртка для одного пина расширителя.
// Не владеет объектом, только ссылается на него.
class MCP23017Pin {
   public:
    MCP23017Pin() = default;

    MCP23017Pin(MCP23017& exp, uint8_t pin)
        : _exp(&exp), _pin(pin) {}

    void attach(MCP23017& exp, uint8_t pin) {
        _exp = &exp;
        _pin = pin;
    }

    bool valid() const { return _exp != nullptr && _pin < 16; }
    uint8_t pin() const { return _pin; }

    bool pinMode(uint8_t mode) const { return _exp && _exp->pinMode(_pin, mode); }
    bool write(bool level) const { return _exp && _exp->writePin(_pin, level); }
    bool read(bool& level) const { return _exp && _exp->readPin(_pin, level); }
    int read() const { return _exp ? _exp->readPin(_pin) : -1; }
    bool toggle() const { return _exp && _exp->togglePin(_pin); }
    bool setPullUp(bool on) const { return _exp && _exp->setPullUp(_pin, on); }

   private:
    MCP23017* _exp = nullptr;
    uint8_t _pin = 0;
};

}  // namespace fn

#endif  // F_USE_MCP23017 && F_USE_I2C

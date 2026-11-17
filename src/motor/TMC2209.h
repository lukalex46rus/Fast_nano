#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_STEPPER && F_USE_UART && F_USE_TMC2209

#include "StepperDriver.h"
#include "../pin/Pin.h"
#include "../pin/PinRef.h"
#include "../timer/Tick.h"
#include "../bus/Bus.h"
#include "../bus/Uart.h"

namespace fn {

namespace Tmc2209Protocol {
constexpr uint8_t Sync = 0x05;
constexpr uint8_t WriteBit = 0x80;
}

// Основные регистры TMC2209.
namespace Tmc2209Register {
constexpr uint8_t Gconf = 0x00;
constexpr uint8_t Gstat = 0x01;
constexpr uint8_t Ifcnt = 0x02;
constexpr uint8_t SlaveConf = 0x03;
constexpr uint8_t Ioin = 0x06;

constexpr uint8_t IholdIrun = 0x10;
constexpr uint8_t TPowerDown = 0x11;
constexpr uint8_t TStep = 0x12;
constexpr uint8_t TPwmThrs = 0x13;
constexpr uint8_t TCoolThrs = 0x14;

constexpr uint8_t VActual = 0x22;

constexpr uint8_t SgThrs = 0x40;
constexpr uint8_t SgResult = 0x41;
constexpr uint8_t CoolConf = 0x42;

constexpr uint8_t MsCnt = 0x6A;
constexpr uint8_t MsCurAct = 0x6B;
constexpr uint8_t ChopConf = 0x6C;
constexpr uint8_t DrvStatus = 0x6F;
constexpr uint8_t PwmConf = 0x70;
constexpr uint8_t PwmScale = 0x71;
constexpr uint8_t PwmAuto = 0x72;
}

// Часто используемые биты регистров.
namespace Tmc2209Bit {
// GCONF
constexpr uint32_t GconfIScaleAnalog = 1ul << 0;
constexpr uint32_t GconfInternalRsense = 1ul << 1;
constexpr uint32_t GconfEnSpreadCycle = 1ul << 2;
constexpr uint32_t GconfShaft = 1ul << 3;
constexpr uint32_t GconfIndexOtpw = 1ul << 4;
constexpr uint32_t GconfIndexStep = 1ul << 5;
constexpr uint32_t GconfPdnDisable = 1ul << 6;
constexpr uint32_t GconfMstepRegSelect = 1ul << 7;
constexpr uint32_t GconfMultistepFilt = 1ul << 8;

// CHOPCONF
constexpr uint32_t ChopConfToffMask = 0x0Ful;
constexpr uint32_t ChopConfHstrtMask = 0x07ul << 4;
constexpr uint32_t ChopConfHendMask = 0x0Ful << 7;
constexpr uint32_t ChopConfTblMask = 0x03ul << 15;
constexpr uint32_t ChopConfVsense = 1ul << 17;
constexpr uint32_t ChopConfMresShift = 24;
constexpr uint32_t ChopConfMresMask = 0x0Ful << 24;
constexpr uint32_t ChopConfIntpol = 1ul << 28;

// IHOLD_IRUN
constexpr uint32_t IholdIrunIholdMask = 0x1Ful;
constexpr uint32_t IholdIrunIrunShift = 8;
constexpr uint32_t IholdIrunIrunMask = 0x1Ful << 8;
constexpr uint32_t IholdIrunIholdDelayShift = 16;
constexpr uint32_t IholdIrunIholdDelayMask = 0x0Ful << 16;
}

// TMC2209: STEP/DIR-драйвер с конфигурацией через UART.
// Для runtime-пинов используйте TMC2209, для compile-time — TMC2209Pins<...>.
class TMC2209 : public StepperDriver {
   public:
    enum class Microstep : uint16_t {
        Full = 1,
        Half = 2,
        Step4 = 4,
        Step8 = 8,
        Step16 = 16,
        Step32 = 32,
        Step64 = 64,
        Step128 = 128,
        Step256 = 256,
    };

    // UART инициализируется один раз, до использования всех драйверов на шине.
    // Скорость запоминается: из неё считаются паузы между датаграммами и
    // таймаут ожидания ответа (см. byteTimeFor()/timeoutFor()).
    static bool beginUart(uint32_t baud = 115200) {
        if (!baud) baud = 115200;
        _baud = baud;
        return Uart::begin(baud);
    }

    // Если UART настраивается напрямую через fn::Uart::begin(), сообщите
    // скорость здесь — иначе паузы и таймауты будут считаться для 115200.
    static void setBaud(uint32_t baud) {
        if (baud) _baud = baud;
    }

    // Скорость линии, принятая модулем, бод.
    static uint32_t baudRate() { return _baud ? _baud : 115200; }

    // ceil(10 бит / скорость) — время передачи одного байта (8N1), мкс.
    // 0 для baud = 0 (защита от деления на ноль).
    static constexpr uint32_t rawByteTimeFor(uint32_t baud) {
        return baud ? (10000000ul + baud - 1ul) / baud : 0ul;
    }

    // То же с насыщением под delayUs(uint16_t).
    static constexpr uint16_t byteTimeFor(uint32_t baud) {
        return rawByteTimeFor(baud) > 65535ul ? uint16_t(65535)
                                             : uint16_t(rawByteTimeFor(baud));
    }

    // Таймаут ответа драйвера: 8 байт * 3 попытки + 1 мс запаса, мкс.
    // При 115200 это 3088 мкс, при 9600 — 26.0 мс (раньше было жёстко 3000 мкс
    // и на 9600 ответ физически не успевал).
    static constexpr uint32_t timeoutFor(uint32_t baud) {
        return uint32_t(byteTimeFor(baud)) * 24ul + 1000ul;
    }

    // Время байта для текущей скорости линии, мкс.
    static uint16_t byteTimeUs() { return byteTimeFor(baudRate()); }

    // Пауза между датаграммами — не меньше одного байтового времени.
    static uint16_t interDatagramGapUs() { return byteTimeUs(); }

    // Таймаут ответа, вычисленный из скорости линии, мкс.
    static uint32_t defaultUartTimeoutUs() { return timeoutFor(baudRate()); }

    TMC2209() = default;

    // Базовая версия без пинов не подходит для runtime-инициализации.
    bool begin() override { return false; }

    // stepPin, dirPin — обязательны.
    // enablePin: -1, если не подключён.
    // addr: UART-адрес TMC2209 (0..3).
    bool begin(uint8_t stepPin, uint8_t dirPin,
               int8_t enablePin = -1,
               uint8_t addr = 0);

    bool step(bool forward) override;
    void enable(bool on) override;

    void setAddress(uint8_t addr) {
        if (addr <= 3) _addr = addr;
    }

    uint8_t address() const { return _addr; }

    // Таймаут ожидания ответа по UART, мкс. 0 — автоматически из скорости
    // линии (defaultUartTimeoutUs(), см. timeoutFor()).
    void setUartTimeoutUs(uint32_t us) { _uartTimeoutUs = us; }

    // Фактический таймаут ожидания ответа, мкс.
    uint32_t uartTimeoutUs() const {
        return _uartTimeoutUs ? _uartTimeoutUs : defaultUartTimeoutUs();
    }

    // Низкоуровневый доступ к регистрам.
    BusError writeReg(uint8_t reg, uint32_t value);
    BusError readReg(uint8_t reg, uint32_t& value);
    BusError modifyReg(uint8_t reg, uint32_t clearMask, uint32_t setMask);

    // Включает UART-режим и выбор микрошага из регистра.
    BusError initUartRegisters();

    // Микрошаг через CHOPCONF.MRES.
    BusError setMicrostep(Microstep m);

    Microstep microstep() const { return _microstep; }

    BusError setStealthChop(bool on);
    BusError setSpreadCycle(bool on);

    // Интерполяция до 256 шагов.
    BusError setInterpolation(bool on);

    // Инверсия направления через GCONF.SHAFT.
    BusError setDirectionInverted(bool inverted);

    // Ток в сырых значениях 0..31.
    BusError setRunHoldCurrent(uint8_t run, uint8_t hold, uint8_t delay = 6);

    // Ток в мА. senseMilliOhm — сопротивление sense-резистора.
    BusError setCurrentmA(uint16_t run_mA,
                          uint16_t hold_mA = 0,
                          uint16_t senseMilliOhm = 110);

    BusError setVsense(bool high);

    BusError setTPowerDown(uint8_t value);

    // CoolStep / StallGuard.
    BusError setCoolStepThreshold(uint32_t threshold);
    BusError setStallThreshold(uint8_t threshold);
    BusError getStallResult(uint32_t& result);

    // Диагностика.
    BusError getDrvStatus(uint32_t& status);

    // Программное включение силового ключа через CHOPCONF.TOFF,
    // если пин EN не подключён.
    BusError setDriverEnabledUart(bool on);

    void setPulseWidthUs(uint8_t us) { _pulseUs = us ? us : 2; }
    void setEnableDelayUs(uint16_t us) { _enableDelayUs = us; }

   protected:
    // CRC-8 TMC2209: полином 0x07, init 0x00, входные биты LSB-to-MSB (§4.2
    // даташита). Не путать с fn::crc::crc8() — у него другой порядок бит.
    static uint8_t crc8(const uint8_t* data, uint8_t len);
    static uint8_t mresFromMicrostep(Microstep m);

    static uint16_t csRaw(uint16_t mA, uint16_t senseMilliOhm, bool vsenseHigh);
    static uint8_t currentToCS(uint16_t mA, uint16_t senseMilliOhm, bool vsenseHigh);

    PinRef _step;
    PinRef _dirPin;
    PinRef _en;

    bool _hasEn = false;
    uint8_t _addr = 0;

    uint8_t _pulseUs = 2;
    uint16_t _enableDelayUs = 1000;

    // Скорость линии, заданная beginUart()/setBaud(). Общая для всех драйверов
    // на шине: USART0 один, скорость у всех одна.
    static uint32_t _baud;

    // Таймаут ожидания ответа по UART, мкс; 0 — из скорости линии.
    uint32_t _uartTimeoutUs = 0;

    Microstep _microstep = Microstep::Full;

    // TOFF для программного enable через UART.
    uint8_t _toff = 3;
};

// Проверки расчёта пауз/таймаутов (нужны именно здесь: внутри класса
// constexpr-функции-члены ещё не определены, C++11).
static_assert(TMC2209::byteTimeFor(115200) == 87, "TMC2209: время байта на 115200");
static_assert(TMC2209::byteTimeFor(9600) == 1042, "TMC2209: время байта на 9600");
static_assert(TMC2209::byteTimeFor(250000) == 40, "TMC2209: время байта на 250000");
static_assert(TMC2209::timeoutFor(115200) == 3088, "TMC2209: таймаут ответа на 115200");
static_assert(TMC2209::timeoutFor(9600) > 8ul * 1042ul,
              "TMC2209: таймаут должен покрывать 8 байт на 9600");

// Compile-time версия: STEP/DIR-пины — параметры шаблона, UART общий.
template <uint8_t STEP, uint8_t DIR, int8_t EN = -1, uint8_t ADDR = 0>
class TMC2209Pins : public TMC2209 {
    static_assert(ADDR <= 3, "TMC2209 address must be 0..3");

   public:
    bool begin() override {
        setAddress(ADDR);

        Pin<STEP>::output();
        Pin<STEP>::low();

        Pin<DIR>::output();
        Pin<DIR>::low();

        detail::OptionalPin<EN>::output();
        detail::OptionalPin<EN>::high();   // disabled

        _hasEn = (EN >= 0);
        _enabled = false;

        return true;
    }

    bool step(bool forward) override {
        if (!_enabled) return false;

        _dir = forward;
        Pin<DIR>::write(forward);

        Pin<STEP>::high();
        if (_pulseUs) delayUs(_pulseUs);
        Pin<STEP>::low();
        if (_pulseUs) delayUs(_pulseUs);

        _pos += forward ? 1 : -1;
        return true;
    }

    void enable(bool on) override {
        detail::OptionalPin<EN>::write(!on);
        if (EN >= 0 && on) delayUs(_enableDelayUs);
        _enabled = on;
    }
};

}  // namespace fn

#endif  // F_USE_STEPPER && F_USE_UART && F_USE_TMC2209

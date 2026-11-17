#include "../core/Config.h"

#if F_USE_STEPPER && F_USE_UART && F_USE_TMC2209

#include "TMC2209.h"

namespace fn {

// Скорость линии, заданная через beginUart()/setBaud().
// USART0 один на всю библиотеку, поэтому это свойство шины, а не драйвера.
uint32_t TMC2209::_baud = 115200;

namespace {

// Кадр ответа TMC2209 — 8 байт: SYNC, адрес МАСТЕРА 0xFF, регистр,
// 4 байта данных MSB-first, CRC-8. Адрес драйвера (0..3) в ответе НЕ участвует.
constexpr uint8_t kResponseLen = 8;
constexpr uint8_t kMasterAddr = 0xFF;

// Шаг опроса RX, мкс.
constexpr uint16_t kPollUs = 10;

// Приёмное окно. На single-wire линии мастер видит собственное эхо:
// 4 байта запроса при чтении и 8 байт при записи, поэтому окно больше кадра.
constexpr uint8_t kRxWindow = 16;

// Насыщение задержки до диапазона delayUs(uint16_t).
uint16_t clampUs(uint32_t us) { return us > 65535ul ? 65535u : uint16_t(us); }

// CRC-8 TMC2209 по §4.2 даташита (Rev. 1.03): «The CRC8-ATM polynomial with an
// initial value of zero is applied LSB to MSB, including the sync- and
// addressing byte», то есть полином 0x07, init 0x00, но входные биты идут
// LSB-first, а регистр сдвигается влево (MSB-first) — это C-код
// swuart_calcCRC() из даташита и из TMC-API/TMCStepper.
//
// ВАЖНО: fn::crc::crc8() (core/Crc.h) — обычный CRC-8/ATM, данные в него
// вносятся MSB-first, поэтому он даёт ДРУГИЕ значения (для [05 00 06] это 0xD2
// вместо требуемых железом 0x6F). Переиспользовать core/Crc.h нельзя, правка
// локализована здесь (core/Crc.h не менялся).
//
// Контрольные значения (получены этим кодом; [05 00 00] -> 0x48 и
// [05 00 06] -> 0x6F независимо подтверждены перебором на реальном TMC2209):
//   [05 00 00]              -> 0x48
//   [05 00 06] (чтение IOIN)-> 0x6F
//   [05 00 80 00 00 00 00]  -> 0x49  (запись GCONF = 0)
//   [05 FF 00 00 00 00 01]  -> 0x0D  (ответ на чтение GCONF: адрес 0xFF)
uint8_t tmcCrc8(const uint8_t* data, uint8_t len) {
    uint8_t crc = 0x00;
    for (uint8_t i = 0; i < len; ++i) {
        uint8_t cur = data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc >> 7) ^ (cur & 0x01)) {
                crc = uint8_t((crc << 1) ^ 0x07);
            } else {
                crc = uint8_t(crc << 1);
            }
            cur = uint8_t(cur >> 1);
        }
    }
    return crc;
}

// Пауза «дозревания» линии + полный дренаж RX.
// Убирает собственное эхо и мусор, чтобы они не попали в следующую датаграмму.
void drainRx(uint16_t settleUs) {
    if (settleUs) delayUs(settleUs);
    while (Uart::read() >= 0) {}
}

// Поиск ответа драйвера в принятом окне:
// SYNC, адрес мастера 0xFF, регистр, 4 байта данных MSB-first, CRC-8 poly 0x07.
// Эхо запроса (4 байта, байт 1 = адрес драйвера) и эхо записи (8 байт,
// байт 2 = reg | 0x80) под этот шаблон не подходят и отбрасываются.
bool findResponse(const uint8_t* buf, uint8_t n, uint8_t reg, uint32_t& value) {
    for (uint8_t i = 0; uint16_t(i) + kResponseLen <= n; ++i) {
        if (buf[i] != Tmc2209Protocol::Sync) continue;
        if (buf[i + 1] != kMasterAddr) continue;   // ответ всегда от 0xFF
        if (buf[i + 2] != reg) continue;
        if (tmcCrc8(&buf[i], 7) != buf[i + 7]) continue;

        value = (uint32_t(buf[i + 3]) << 24) | (uint32_t(buf[i + 4]) << 16) |
                (uint32_t(buf[i + 5]) << 8) | uint32_t(buf[i + 6]);
        return true;
    }
    return false;
}

}  // namespace

bool TMC2209::begin(uint8_t stepPin, uint8_t dirPin,
                    int8_t enablePin,
                    uint8_t addr) {
    if (addr > 3) return false;

    _addr = addr;

    _step.init(stepPin, OUTPUT);
    _step.low();

    _dirPin.init(dirPin, OUTPUT);
    _dirPin.low();

    if (enablePin >= 0) {
        _en.init(uint8_t(enablePin), OUTPUT);
        _en.high();   // disabled
        _hasEn = true;
    }

    _enabled = false;
    return _step.valid() && _dirPin.valid();
}

bool TMC2209::step(bool forward) {
    if (!_enabled) return false;

    _dir = forward;
    _dirPin.write(forward);

    _step.high();
    if (_pulseUs) delayUs(_pulseUs);
    _step.low();
    if (_pulseUs) delayUs(_pulseUs);

    _pos += forward ? 1 : -1;
    return true;
}

void TMC2209::enable(bool on) {
    if (_hasEn) {
        _en.write(!on);   // active-low
        if (on) delayUs(_enableDelayUs);
    }
    _enabled = on;
}

uint8_t TMC2209::crc8(const uint8_t* data, uint8_t len) {
    // CRC-8 TMC2209: полином 0x07, init 0x00, данные LSB-to-MSB (см. tmcCrc8()).
    // Раньше здесь была сумма байт — драйвер игнорировал все датаграммы.
    return tmcCrc8(data, len);
}

BusError TMC2209::writeReg(uint8_t reg, uint32_t value) {
    // Пауза между датаграммами (>= 1 байтового времени) + сброс остатков
    // предыдущего эха/мусора: они не должны попасть в новую датаграмму.
    drainRx(interDatagramGapUs());

    uint8_t buf[8];
    buf[0] = Tmc2209Protocol::Sync;
    buf[1] = _addr;
    buf[2] = uint8_t(reg | Tmc2209Protocol::WriteBit);
    buf[3] = uint8_t(value >> 24);
    buf[4] = uint8_t(value >> 16);
    buf[5] = uint8_t(value >> 8);
    buf[6] = uint8_t(value);
    buf[7] = crc8(buf, 7);

    Uart::write(buf, 8);
    Uart::flush();

    // Запись TMC2209 не подтверждается ответом, но на single-wire линии мастер
    // видит собственное эхо — 8 байт. Даём последнему байту дойти до RX
    // (2 байтовых времени) и вычитываем его, иначе эхо останется в буфере
    // USART и будет принято за начало следующего ответа.
    drainRx(clampUs(uint32_t(byteTimeUs()) * 2ul));

    return BusError::Ok;
}

BusError TMC2209::readReg(uint8_t reg, uint32_t& value) {
    const uint8_t cmd = uint8_t(reg & 0x7F);

    // Пауза между датаграммами + сброс остатков предыдущего обмена.
    drainRx(interDatagramGapUs());

    uint8_t req[4];
    req[0] = Tmc2209Protocol::Sync;
    req[1] = _addr;
    req[2] = cmd;
    req[3] = crc8(req, 3);

    Uart::write(req, 4);
    Uart::flush();

    // Буфер с запасом: эхо запроса (4 байта) + ответ (8 байт).
    uint8_t buf[kRxWindow];
    uint8_t n = 0;
    uint32_t waited = 0;
    const uint32_t timeoutUs = uartTimeoutUs();

    while (waited < timeoutUs) {
        if (n >= kRxWindow) return BusError::Other;   // окно переполнено без ответа

        int b = Uart::read();
        if (b >= 0) {
            buf[n++] = uint8_t(b);
            if (findResponse(buf, n, cmd, value)) {
                // Хвост эха/ответа не должен попасть в следующую датаграмму.
                drainRx(interDatagramGapUs());
                return BusError::Ok;
            }
            continue;   // байты идут потоком — читаем без задержки
        }

        delayUs(kPollUs);
        waited += kPollUs;
    }

    return BusError::Timeout;
}

BusError TMC2209::modifyReg(uint8_t reg, uint32_t clearMask, uint32_t setMask) {
    uint32_t value = 0;
    BusError e = readReg(reg, value);
    if (e != BusError::Ok) return e;

    value = (value & ~clearMask) | setMask;
    return writeReg(reg, value);
}

BusError TMC2209::initUartRegisters() {
    // Разрешаем UART-режим и выбор микрошага из регистра,
    // I_SCALE_ANALOG — сбрасываем: иначе ток масштабируется напряжением VREF
    // и IHOLD_IRUN работает не так, как ожидается.
    const uint32_t setBits =
        Tmc2209Bit::GconfPdnDisable | Tmc2209Bit::GconfMstepRegSelect;

    uint32_t gconf = 0;
    BusError e = readReg(Tmc2209Register::Gconf, gconf);

    if (e == BusError::Ok) {
        gconf |= setBits;                          // остальные биты GCONF сохраняем
        gconf &= ~Tmc2209Bit::GconfIScaleAnalog;   // цифровое задание тока
        e = writeReg(Tmc2209Register::Gconf, gconf);
    } else {
        // Прочитать не удалось — пишем минимальный безопасный набор битов.
        e = writeReg(Tmc2209Register::Gconf,
                     setBits & ~Tmc2209Bit::GconfIScaleAnalog);
    }

    if (e != BusError::Ok) return e;

    // Задержка перехода в standstill по умолчанию.
    return writeReg(Tmc2209Register::TPowerDown, 20);
}

uint8_t TMC2209::mresFromMicrostep(Microstep m) {
    switch (m) {
        case Microstep::Step256: return 0;
        case Microstep::Step128: return 1;
        case Microstep::Step64: return 2;
        case Microstep::Step32: return 3;
        case Microstep::Step16: return 4;
        case Microstep::Step8: return 5;
        case Microstep::Step4: return 6;
        case Microstep::Half: return 7;
        case Microstep::Full:
        default: return 8;
    }
}

BusError TMC2209::setMicrostep(Microstep m) {
    uint8_t mres = mresFromMicrostep(m);

    BusError e = modifyReg(Tmc2209Register::ChopConf,
                           Tmc2209Bit::ChopConfMresMask,
                           uint32_t(mres) << Tmc2209Bit::ChopConfMresShift);

    if (e == BusError::Ok) _microstep = m;
    return e;
}

BusError TMC2209::setSpreadCycle(bool on) {
    return modifyReg(Tmc2209Register::Gconf,
                     Tmc2209Bit::GconfEnSpreadCycle,
                     on ? Tmc2209Bit::GconfEnSpreadCycle : 0);
}

BusError TMC2209::setStealthChop(bool on) {
    // StealthChop активен, когда en_spreadCycle = 0.
    return setSpreadCycle(!on);
}

BusError TMC2209::setInterpolation(bool on) {
    return modifyReg(Tmc2209Register::ChopConf,
                     Tmc2209Bit::ChopConfIntpol,
                     on ? Tmc2209Bit::ChopConfIntpol : 0);
}

BusError TMC2209::setDirectionInverted(bool inverted) {
    return modifyReg(Tmc2209Register::Gconf,
                     Tmc2209Bit::GconfShaft,
                     inverted ? Tmc2209Bit::GconfShaft : 0);
}

BusError TMC2209::setRunHoldCurrent(uint8_t run, uint8_t hold, uint8_t delay) {
    if (run > 31) run = 31;
    if (hold > 31) hold = 31;
    if (delay > 15) delay = 15;

    uint32_t value =
        (uint32_t(hold) & 0x1Ful) |
        ((uint32_t(run) & 0x1Ful) << Tmc2209Bit::IholdIrunIrunShift) |
        ((uint32_t(delay) & 0x0Ful) << Tmc2209Bit::IholdIrunIholdDelayShift);

    return writeReg(Tmc2209Register::IholdIrun, value);
}

BusError TMC2209::setVsense(bool high) {
    return modifyReg(Tmc2209Register::ChopConf,
                     Tmc2209Bit::ChopConfVsense,
                     high ? Tmc2209Bit::ChopConfVsense : 0);
}

uint16_t TMC2209::csRaw(uint16_t mA, uint16_t senseMilliOhm, bool vsenseHigh) {
    if (!mA || !senseMilliOhm) return 0;

    // Практический предел, чтобы не переполнять 32-битную математику.
    if (mA > 3000) mA = 3000;
    if (senseMilliOhm > 220) senseMilliOhm = 220;

    // V_fs: VSENSE = 0 ~0.325 В, VSENSE = 1 ~0.180 В (внешний R_sense).
    const uint32_t vfs_uV = vsenseHigh ? 180000ul : 325000ul;

    // По даташиту I_rms = (CS + 1)/32 * V_fs / (R_sense * sqrt(2)).
    // Отсюда (CS + 1) = I * R * sqrt(2) * 32 / V_fs; sqrt(2) ~ 1.414.
    // Возвращаем округлённое (CS + 1), 0..32; 0 — ток не задан.
    // ВАЖНО: это НЕ значение регистра CS — см. currentToCS().
    uint32_t num = uint32_t(mA) * senseMilliOhm * 1414ul / 1000ul * 32ul;
    uint32_t raw = (num + vfs_uV / 2ul) / vfs_uV;   // округление к ближайшему

    if (raw > 32) raw = 32;                          // CS = 31 максимум
    return uint16_t(raw);
}

uint8_t TMC2209::currentToCS(uint16_t mA, uint16_t senseMilliOhm, bool vsenseHigh) {
    uint16_t raw = csRaw(mA, senseMilliOhm, vsenseHigh);   // = CS + 1
    if (!raw) return 0;

    // Раньше сюда попадало CS+1 и ток получался завышенным.
    raw = uint16_t(raw - 1);                               // CS = (CS + 1) - 1
    return raw > 31 ? 31 : uint8_t(raw);
}

BusError TMC2209::setCurrentmA(uint16_t run_mA,
                               uint16_t hold_mA,
                               uint16_t senseMilliOhm) {
    // Для малых токов лучше подходит VSENSE = 1 (меньше шаг квантования),
    // для больших — VSENSE = 0. raw = 32 означает насыщение диапазона.
    const uint16_t rawHigh = csRaw(run_mA, senseMilliOhm, true);
    const bool useHighSense = (rawHigh > 0 && rawHigh <= 31 && run_mA <= 700);

    const uint8_t run = currentToCS(run_mA, senseMilliOhm, useHighSense);
    const uint8_t hold =
        hold_mA ? currentToCS(hold_mA, senseMilliOhm, useHighSense) : 0;

    BusError e = setVsense(useHighSense);
    if (e != BusError::Ok) return e;

    // VSENSE и ток — две транзакции: между ними ток кратковременно неверный.
    // Критично только при вращении; для смены тока «на ходу» используйте
    // setRunHoldCurrent() напрямую.
    return setRunHoldCurrent(run, hold, 6);
}

BusError TMC2209::setTPowerDown(uint8_t value) {
    return writeReg(Tmc2209Register::TPowerDown, value);
}

BusError TMC2209::setCoolStepThreshold(uint32_t threshold) {
    return writeReg(Tmc2209Register::TCoolThrs, threshold & 0x000FFFFFul);
}

BusError TMC2209::setStallThreshold(uint8_t threshold) {
    return writeReg(Tmc2209Register::SgThrs, threshold);
}

BusError TMC2209::getStallResult(uint32_t& result) {
    return readReg(Tmc2209Register::SgResult, result);
}

BusError TMC2209::getDrvStatus(uint32_t& status) {
    return readReg(Tmc2209Register::DrvStatus, status);
}

BusError TMC2209::setDriverEnabledUart(bool on) {
    uint32_t value = 0;
    BusError e = readReg(Tmc2209Register::ChopConf, value);
    if (e != BusError::Ok) return e;

    value &= ~Tmc2209Bit::ChopConfToffMask;
    if (on) value |= (_toff & 0x0F);

    e = writeReg(Tmc2209Register::ChopConf, value);
    if (e == BusError::Ok) _enabled = on;

    return e;
}

}  // namespace fn

#endif  // F_USE_STEPPER && F_USE_UART && F_USE_TMC2209

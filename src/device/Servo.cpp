#include "../core/Config.h"

#if F_USE_SERVO

#include <avr/io.h>
#include <avr/interrupt.h>
#include "Servo.h"
#include "../pin/PinRef.h"

namespace fn {

namespace servo_detail {
    struct Channel {
        PinRef   pin;
        uint16_t us;
        uint16_t minUs;
        uint16_t maxUs;
        bool     active;
    };
    Channel ch[Servo::MAX_CHANNELS];
    volatile uint8_t activeMask = 0;
    volatile uint8_t currentCh = 0xFF;   // текущий канал, чей импульс активен
}

Servo::Err Servo::begin() {
    if (TCCR1B & 0x07) return Err::TimerBusy;
    TCCR1A = _BV(WGM11);
    TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS11);
    ICR1 = 39999;            // 20 мс @ 16 МГц / 8
    OCR1A = 1000;            // первое прерывание через 0.5 мс
    TIMSK1 |= _BV(OCIE1A);   // COMPA — старт периода
    // COMPB не включаем, будем ставить OCR1B и включать прерывание по мере надобности.
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        servo_detail::ch[i].active = false;
        servo_detail::ch[i].us = 1500;
        servo_detail::ch[i].minUs = 500;
        servo_detail::ch[i].maxUs = 2400;
    }
    servo_detail::activeMask = 0;
    servo_detail::currentCh = 0xFF;
    return Err::Ok;
}

void Servo::end() {
    TIMSK1 &= ~(_BV(OCIE1A) | _BV(OCIE1B));
    TCCR1A = 0;
    TCCR1B = 0;
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        if (servo_detail::ch[i].active) servo_detail::ch[i].pin.low();
        servo_detail::ch[i].active = false;
    }
    servo_detail::activeMask = 0;
}

uint8_t Servo::attach(uint8_t pin, uint16_t minUs, uint16_t maxUs) {
    for (uint8_t i = 0; i < MAX_CHANNELS; i++) {
        if (!servo_detail::ch[i].active) {
            servo_detail::ch[i].pin.init(pin, OUTPUT);
            servo_detail::ch[i].pin.low();
            servo_detail::ch[i].us = (minUs + maxUs) / 2;
            servo_detail::ch[i].minUs = minUs;
            servo_detail::ch[i].maxUs = maxUs;
            servo_detail::ch[i].active = true;
            servo_detail::activeMask |= _BV(i);
            return i;
        }
    }
    return 0xFF;
}

void Servo::detach(uint8_t channel) {
    if (channel >= MAX_CHANNELS) return;
    uint8_t s = SREG; cli();
    if (servo_detail::ch[channel].active) {
        servo_detail::ch[channel].pin.low();
        servo_detail::ch[channel].active = false;
        servo_detail::activeMask &= ~_BV(channel);
    }
    SREG = s;
}

void Servo::write(uint8_t channel, uint16_t us) {
    if (channel >= MAX_CHANNELS) return;
    if (!servo_detail::ch[channel].active) return;
    if (us < 500)  us = 500;
    if (us > 2500) us = 2500;
    uint8_t s = SREG; cli();
    servo_detail::ch[channel].us = us;
    SREG = s;
}

void Servo::writeDeg(uint8_t channel, uint8_t deg) {
    if (channel >= MAX_CHANNELS || deg > 180) return;
    const auto& c = servo_detail::ch[channel];
    uint16_t us = c.minUs + (uint32_t(c.maxUs - c.minUs) * deg) / 180;
    write(channel, us);
}

}  // namespace fn

// ============================================================
// ISR
// ============================================================

// COMPA: начало нового 20-мс периода. Выключаем предыдущий импульс,
// находим следующий активный канал и запускаем его.
ISR(TIMER1_COMPA_vect) {
    using namespace fn;
    // Выключаем предыдущий канал.
    if (servo_detail::currentCh != 0xFF) {
        servo_detail::ch[servo_detail::currentCh].pin.low();
        servo_detail::currentCh = 0xFF;
    }
    // Ищем следующий активный канал.
    for (uint8_t i = 0; i < Servo::MAX_CHANNELS; i++) {
        if (servo_detail::activeMask & _BV(i)) {
            servo_detail::currentCh = i;
            servo_detail::ch[i].pin.high();
            // Устанавливаем конец импульса: 1 тик = 0.5 мкс (16 МГц / 8).
            // us = 1500 → 3000 тиков. TCNT1 = 0 в этот момент (пик).
            OCR1B = uint16_t(servo_detail::ch[i].us * 2);
            TIFR1 = _BV(OCF1B);       // очистить флаг, если был
            TIMSK1 |= _BV(OCIE1B);    // включить COMPB
            return;
        }
    }
    // Нет активных — просто ждём следующего периода.
    TIMSK1 &= ~_BV(OCIE1B);
}

// COMPB: конец текущего импульса. Выключаем канал и включаем следующий.
ISR(TIMER1_COMPB_vect) {
    using namespace fn;
    if (servo_detail::currentCh != 0xFF) {
        servo_detail::ch[servo_detail::currentCh].pin.low();
        servo_detail::currentCh = 0xFF;
    }
    // Ищем следующий активный канал, начиная с последнего +1.
    // Для простоты — с нуля (гарантированно укладываемся: 8 * 2.5 мс = 20 мс).
    for (uint8_t i = 0; i < Servo::MAX_CHANNELS; i++) {
        if (servo_detail::activeMask & _BV(i)) {
            servo_detail::currentCh = i;
            servo_detail::ch[i].pin.high();
            OCR1B = TCNT1 + uint16_t(servo_detail::ch[i].us * 2);
            return;
        }
    }
    TIMSK1 &= ~_BV(OCIE1B);
}

#endif  // F_USE_SERVO

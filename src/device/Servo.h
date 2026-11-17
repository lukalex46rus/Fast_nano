#pragma once
#include <stdint.h>

#include "../core/Features.h"

#if F_USE_SERVO

namespace fn {

// Servo на Timer1 (D9/D10 заняты таймером, но не как PWM — как база времени).
// ВНИМАНИЕ: конфликтует с Pwm1 и любым использованием Timer1.
class Servo {
   public:
    static constexpr uint8_t MAX_CHANNELS = 8;

    enum class Err : uint8_t {
        Ok = 0,
        Full,
        TimerBusy,
        BadPin,
    };

    // Инициализация Timer1 как базы времени. Возвращает Err::Ok или TimerBusy.
    static Err begin();

    // Завершить работу (отцепить все каналы, остановить Timer1).
    static void end();

    // Прикрепить пин. minUs/maxUs — границы для writeDeg.
    // Возвращает индекс канала (0..7) или 0xFF при ошибке.
    static uint8_t attach(uint8_t pin, uint16_t minUs = 500, uint16_t maxUs = 2400);

    // Отцепить канал.
    static void detach(uint8_t channel);

    // Установить ширину импульса в микросекундах (500..2500).
    static void write(uint8_t channel, uint16_t us);

    // Установить угол (0..180°), маппинг по minUs/maxUs.
    static void writeDeg(uint8_t channel, uint8_t deg);

    // Внутренняя функция — вызывается из ISR.
    static void _isr();
};

}  // namespace fn

#endif

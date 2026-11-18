#pragma once
#include <stdint.h>

#include "../core/Compiler.h"
#include "../pin/Pin.h"
#include "../timer/Tick.h"

namespace fn {
namespace shift {

enum Order : uint8_t { MSB_FIRST = 1, LSB_FIRST = 0 };

// Отправить байт MSB-first (или LSB-first) с задержкой delayUs (0 = без задержки).
template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline void sendByte(uint8_t data, uint8_t delayUs = 0) {
    for (uint8_t i = 0; i < 8; i++) {
        if (ORDER == MSB_FIRST) { BoardPin<DAT>::write(data & 0x80); data <<= 1; }
        else                    { BoardPin<DAT>::write(data & 0x01); data >>= 1; }
        if (delayUs) fn::delayUs(delayUs);
        BoardPin<CLK>::high();
        if (delayUs) fn::delayUs(delayUs);
        BoardPin<CLK>::low();
    }
}

template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline void send(const uint8_t* data, uint16_t len, uint8_t delayUs = 0) {
    while (len--) sendByte<DAT, CLK, ORDER>(*data++, delayUs);
}

// Прочитать байт MSB-first.
template <uint8_t DAT, uint8_t CLK, uint8_t ORDER = MSB_FIRST>
inline uint8_t readByte(uint8_t delayUs = 0) {
    uint8_t v = 0;
    for (uint8_t i = 0; i < 8; i++) {
        if (ORDER == MSB_FIRST) v <<= 1;
        if (BoardPin<DAT>::read()) {
            if (ORDER == MSB_FIRST) v |= 1;
            else                    v |= uint8_t(0x80 >> i);
        }
        if (delayUs) fn::delayUs(delayUs);
        BoardPin<CLK>::high();
        if (delayUs) fn::delayUs(delayUs);
        BoardPin<CLK>::low();
    }
    return v;
}

}  // namespace shift
}  // namespace fn

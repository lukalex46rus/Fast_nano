#pragma once
#include <Print.h>

#include "../../core/Features.h"

#if F_USE_LCD

namespace fn {
namespace disp {

// Arduino Print адаптер поверх любого LCD-слоя с write(uint8_t)
template <typename LCD>
class LcdPrinter : public LCD, public Print {
   public:
    using LCD::LCD;
    using LCD::write;
    using Print::print;
    using Print::println;

    size_t write(uint8_t data) override {
        return LCD::write(data);
    }
};

}  // namespace disp
}  // namespace fn

#endif  // F_USE_LCD

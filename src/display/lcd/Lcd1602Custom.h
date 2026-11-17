#pragma once
#include "../../core/Features.h"

#if F_USE_LCD

#include "Lcd1602Core.h"
#include "LcdPrinter.h"

namespace fn {
namespace disp {

// внешний Wire-like интерфейс + Arduino Print
template <typename WireT>
using Lcd1602Custom = LcdPrinter<LcdCore<WireT>>;

}  // namespace disp
}  // namespace fn

#endif  // F_USE_LCD

#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

namespace fn {
namespace looper {

// Фиксированный стек. Используется для очереди отложенных событий.
template <typename T, uint8_t capacity>
class Stack {
   public:
    bool push(T value) {
        if (_size >= capacity) return false;
        _items[_size++] = value;
        return true;
    }

    // Забрать последний элемент.
    T& pop() {
        return _items[--_size];
    }

    uint8_t size() const {
        return _size;
    }

   private:
    // Без инициализаторов: стек в статической памяти должен обнуляться
    // статически, чтобы Looper был constant-initialized.
    T _items[capacity];
    uint8_t _size;
};

}  // namespace looper
}  // namespace fn

#endif

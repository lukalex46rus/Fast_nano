#pragma once
#include <inttypes.h>
#include <stddef.h>

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

namespace fn {
namespace looper {

// Односвязный список задач. Узлы встраиваются в объекты задач.
template <typename T>
class List {
   public:
    class Node {
        friend class List;

       public:
        T* next() { return _next; }

       private:
        T* _next = nullptr;
    };

    bool add(T& node) {
        return add(&node);
    }

    bool add(T* node) {
        if (!node) return false;
        node->_next = _first;
        _first = node;
        return true;
    }

    // Удалить узел. Возвращает предыдущий элемент списка.
    T* remove(T& node) {
        return remove(&node);
    }

    T* remove(T* node) {
        if (!node) return nullptr;

        if (_first == node) {
            _first = node->_next;
            return nullptr;
        }

        T* previous = _first;
        while (previous) {
            if (previous->_next == node) {
                previous->_next = node->_next;
                return previous;
            }
            previous = previous->_next;
        }
        return nullptr;
    }

    bool contains(T& node) const {
        return contains(&node);
    }

    bool contains(T* node) const {
        T* current = _first;
        while (current) {
            if (current == node) return true;
            current = current->_next;
        }
        return false;
    }

    size_t size() const {
        size_t count = 0;
        T* current = _first;
        while (current) {
            count++;
            current = current->_next;
        }
        return count;
    }

    void clear() {
        _first = nullptr;
    }

    T* first() const {
        return _first;
    }

   private:
    // Без инициализатора: список в статической памяти должен обнуляться
    // статически, чтобы глобальные задачи не терялись из-за порядка линковки.
    T* _first;
};

}  // namespace looper
}  // namespace fn

#endif

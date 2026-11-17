#pragma once

#include "../../../core/Features.h"

#if F_USE_LOGIC && F_USE_LOOPER

#include "../utils/macro.h"

namespace fn {

// Данные задачи и обработчик, принимающий эти данные.
template <typename T>
class TaskData {
    LOOPER_MAKE_CALLBACK(DataCallback, void, T*);

   public:
    TaskData(T* data, DataCallback callback) : _data(data), _callback(callback) {}

    void setData(T* data) {
        _data = data;
    }

    T* data() {
        return _data;
    }

    void invoke() {
        if (_callback) _callback(_data);
    }

   private:
    T* _data;
    DataCallback _callback;
};

}  // namespace fn

#endif

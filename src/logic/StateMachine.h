#pragma once
// Табличный конечный автомат (FSM): состояния, события, переходы, действия.
// Без динамической памяти; таблица переходов задаётся в begin()/add().

#include <stdint.h>

#include "../core/Features.h"

#if F_USE_LOGIC && F_USE_STATE

namespace fn {
namespace logic {

using StateAction = void (*)(void* ctx);

// Специальное состояние «любое» для add().
constexpr uint8_t StateAny = 0xFF;

template <uint8_t MaxStates = 16, uint8_t MaxTransitions = 32>
class StateMachine {
    static_assert(MaxStates > 0 && MaxStates < StateAny, "bad MaxStates");
    static_assert(MaxTransitions > 0, "bad MaxTransitions");

   public:
    // stateCount — число реально используемых состояний (0..MaxStates).
    // Контекст передаётся во все действия.
    void begin(uint8_t initial = 0, uint8_t stateCount = MaxStates,
               void* ctx = nullptr) {
        _states = stateCount ? stateCount : uint8_t(1);
        if (_states > MaxStates) _states = MaxStates;
        _initial = (initial < _states) ? initial : uint8_t(0);
        _state = _initial;
        _ctx = ctx;
    }

    // Переход from -> to по событию event. from == StateAny — из любого.
    // action выполняется после смены состояния и до enter-действия.
    bool add(uint8_t from, uint8_t event, uint8_t to,
             StateAction action = nullptr) {
        if (_count >= MaxTransitions) return false;
        if (to >= _states) return false;
        if (from != StateAny && from >= _states) return false;
        _trans[_count].from = from;
        _trans[_count].event = event;
        _trans[_count].to = to;
        _trans[_count].action = action;
        _count++;
        return true;
    }

    bool addAny(uint8_t event, uint8_t to, StateAction action = nullptr) {
        return add(StateAny, event, to, action);
    }

    bool setEnter(uint8_t state, StateAction action) {
        if (state >= _states) return false;
        _enter[state] = action;
        return true;
    }
    bool setExit(uint8_t state, StateAction action) {
        if (state >= _states) return false;
        _exit[state] = action;
        return true;
    }

    // true — переход найден и выполнен.
    bool dispatch(uint8_t event) {
        for (uint8_t i = 0; i < _count; i++) {
            const Transition& t = _trans[i];
            if (t.event != event) continue;
            if (t.from != StateAny && t.from != _state) continue;

            uint8_t old = _state;
            if (t.to != old) {
                if (_exit[old]) _exit[old](_ctx);
                _state = t.to;
                if (t.action) t.action(_ctx);
                if (_enter[t.to]) _enter[t.to](_ctx);
            } else if (t.action) {
                t.action(_ctx);
            }
            return true;
        }
        return false;
    }

    // Прямая смена состояния (без transition action), с exit/enter.
    bool setState(uint8_t state) {
        if (state >= _states) return false;
        if (state == _state) return true;
        if (_exit[_state]) _exit[_state](_ctx);
        _state = state;
        if (_enter[_state]) _enter[_state](_ctx);
        return true;
    }

    void reset() { setState(_initial); }

    uint8_t state() const { return _state; }
    uint8_t initial() const { return _initial; }
    uint8_t stateCount() const { return _states; }
    uint8_t transitionCount() const { return _count; }
    void* context() const { return _ctx; }
    void setContext(void* ctx) { _ctx = ctx; }

    void clearTransitions() { _count = 0; }
    void clearActions() {
        for (uint8_t i = 0; i < MaxStates; i++) {
            _enter[i] = nullptr;
            _exit[i] = nullptr;
        }
    }

   private:
    struct Transition {
        uint8_t     from = 0;
        uint8_t     event = 0;
        uint8_t     to = 0;
        StateAction action = nullptr;
    };

    Transition  _trans[MaxTransitions] = {};
    StateAction _enter[MaxStates] = {};
    StateAction _exit[MaxStates] = {};
    uint8_t     _count = 0;
    uint8_t     _state = 0;
    uint8_t     _initial = 0;
    uint8_t     _states = MaxStates;
    void*       _ctx = nullptr;
};

template <uint8_t MaxStates = 16, uint8_t MaxTransitions = 32>
using Fsm = StateMachine<MaxStates, MaxTransitions>;

}  // namespace logic
}  // namespace fn

#endif  // F_USE_LOGIC && F_USE_STATE

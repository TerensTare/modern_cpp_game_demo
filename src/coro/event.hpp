
#pragma once

#include <optional>
#include "coro/stage.hpp"

// TODO: fire_once event for permanent events

template <typename T>
struct event_awaiter;

template <typename T = void>
struct event;

template <>
struct event<void>
{
    inline explicit event(stage_info &s) : s{&s} {}

    inline void trigger()
    {
        // TODO: better impl
        for (auto l : listeners)
            s->schedule(l.hnd, l.suspend_point);
        listeners.clear();
    }

    inline event_awaiter<void> operator co_await();

private:
    stage_info *s;
    std::vector<coro_state> listeners;
    bool is_ready = false;

    friend event_awaiter;
};

template <typename T>
struct event final : private event<void>
{
    inline explicit event(stage_info &s) : event<void>{s} {}

    inline void trigger(T const &value)
    {
        // CORRECTNESS:
        // this value is only ever accessed when the coroutines are resumed from awaiting the event, that is, right after it's set
        // so there is no way coroutines can ever get an "old" value
        this->value = value; // set the value to be passed to the coroutines waiting for this event
        event<void>::trigger();
    }

    inline event_awaiter<T> operator co_await();

private:
    std::optional<T> value;

    friend event_awaiter<T>;
};

template <>
struct event_awaiter<void> final
{
    event<> *e;

    static constexpr bool await_ready() noexcept { return false; }
    inline void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept { e->listeners.push_back({hnd, sl}); }
    static constexpr void await_resume() noexcept {}
};

template <typename T>
struct event_awaiter final
{
    event<T> *e;

    static constexpr bool await_ready() noexcept { return false; }
    inline void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept { e->listeners.push_back({hnd, sl}); }
    inline T await_resume() noexcept { return e->value.value(); }
};

inline auto event<void>::operator co_await() -> event_awaiter<void> { return event_awaiter{this}; }

template <typename T>
inline auto event<T>::operator co_await() -> event_awaiter<T> { return event_awaiter{this}; }
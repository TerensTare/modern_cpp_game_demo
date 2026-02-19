
#pragma once

#include <optional>
#include "coro/exp/scheduler.hpp"
#include "utils/compressed_pair.hpp"

// TODO: constrain by stage
// TODO: add to this frame's events

template <typename T>
struct event_awaiter;

template <typename T = void>
struct event final
{
    inline explicit event(scheduler &s) : s{&s} {}

    inline void trigger()
        requires(std::is_void_v<T>)
    {
        return trigger_impl();
    }

    template <typename U = T>
        requires(!std::is_void_v<T> && std::constructible_from<T, U &&>)
    inline void trigger(U &&val)
    {
        first_and_value.right = static_cast<U &&>(val);
        return trigger_impl();
    }

    inline event_awaiter<T> operator co_await() noexcept;

private:
    inline void trigger_impl()
    {
        suspended *awt = nullptr;
        while (awt = waiting_and_value.left.pop())
        {
            s->schedule(awt->hnd, awt->suspend_point);
        }
        waiting_and_value.left = {}; // just to be sure
    }

    using value_type = std::conditional_t<std::is_void_v<T>, void, std::optional<T>>;

    scheduler *s;
    compressed_pair<wait_list, value_type> waiting_and_value{
        .left = nullptr,
    };

    friend event_awaiter;
};

template <typename T>
struct event_awaiter final
{
    event<T> *e;
    suspended susp;

    static constexpr bool await_ready() noexcept { return false; }

    constexpr void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept
    {
        susp.hnd = hnd;
        // susp.sl = sl;
        e->waiting_and_value.push_one(susp);
    }

    constexpr decltype(auto) await_resume() const noexcept
    {
        if constexpr (!std::is_void_v<T>)
            return e->waiting_and_value.right.value();
    }
};

template <typename T>
inline auto event<T>::operator co_await() noexcept -> event_awaiter<T> { return event_awaiter{this}; }
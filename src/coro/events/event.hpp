
#pragma once

#include <optional>
#include "coro/stage.hpp"
#include "utils/compressed_pair.hpp"

template <typename T>
struct event_awaiter;

template <typename T = void>
struct event final
{
    inline explicit event(stage_info &s) : s{&s} {}

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
        auto awt = first_and_value.left;
        while (awt)
        {
            s->schedule(awt->hnd, awt->suspend_point);
            awt = awt->next;
        }
        first_and_value.left = awt;
    }

    using value_type = std::conditional_t<std::is_void_v<T>, void, std::optional<T>>;

    stage_info *s;
    compressed_pair<event_awaiter<T> *, value_type> first_and_value{
        .left = nullptr,
    };

    friend event_awaiter;
};

template <typename T>
struct event_awaiter final
{
    event<T> *e;
    std::coroutine_handle<> hnd;
    std::source_location suspend_point;
    event_awaiter *next = nullptr;

    static constexpr bool await_ready() noexcept { return false; }

    constexpr void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept
    {
        this->hnd = hnd;
        suspend_point = sl;
        next = e->first_and_value.left;
        e->first_and_value.left = this;
    }

    constexpr decltype(auto) await_resume() const noexcept
    {
        if constexpr (!std::is_void_v<T>)
            return e->first_and_value.right.value();
    }
};

template <typename T>
inline auto event<T>::operator co_await() noexcept -> event_awaiter<T> { return event_awaiter{this}; }
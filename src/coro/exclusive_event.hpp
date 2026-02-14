

#pragma once

#include <optional>
#include "coro/stage.hpp"
#include "utils/compressed_pair.hpp"

template <typename T>
struct exclusive_event_awaiter;

template <typename T = void>
struct exclusive_event final
{
    inline explicit exclusive_event(stage_info &s) : s{&s} {}

    inline void trigger()
        requires(std::is_void_v<T>)
    {
        return trigger_impl();
    }

    template <typename U = T>
        requires(!std::is_void_v<T> && std::constructible_from<T, U &&>)
    inline void trigger(U &&val)
    {
        cont_and_value.right = static_cast<U &&>(val);
        return trigger_impl();
    }

    inline exclusive_event_awaiter<T> operator co_await() noexcept;

private:
    inline void trigger_impl()
    {
        auto state = std::exchange(cont_and_value.left, coro_state{nullptr});
        s->schedule(state.hnd, state.suspend_point);
    }

    using value_type = std::conditional_t<std::is_void_v<T>, void, std::optional<T>>;

    stage_info *s;
    compressed_pair<coro_state, value_type> cont_and_value{
        .left = nullptr,
    };

    friend exclusive_event_awaiter;
};

template <typename T>
struct exclusive_event_awaiter final
{
    exclusive_event<T> *e;

    static constexpr bool await_ready() noexcept { return false; }

    constexpr void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept
    {
        e->cont_and_value.left = {hnd, sl};
    }

    constexpr decltype(auto) await_resume() noexcept
    {
        if constexpr (!std::is_void_v<T>)
            return e->value.value();
    }
};

template <typename T>
inline auto exclusive_event<T>::operator co_await() noexcept -> exclusive_event_awaiter<T> { return exclusive_event_awaiter{this}; }
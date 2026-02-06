
#pragma once

#include <coroutine>
#include <exception>

struct fire_and_forget final
{
    struct promise_type;
};

struct fire_and_forget::promise_type final
{
    static constexpr fire_and_forget get_return_object() noexcept { return {}; }
    static constexpr std::suspend_never initial_suspend() noexcept { return {}; }
    static constexpr std::suspend_never final_suspend() noexcept { return {}; }

    static constexpr void return_void() noexcept {}
    inline static void unhandled_exception() noexcept { std::terminate(); }
};
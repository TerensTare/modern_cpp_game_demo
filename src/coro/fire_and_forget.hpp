
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

    static constexpr auto final_suspend() noexcept
    {
        struct awaiter final
        {
            static constexpr bool await_ready() noexcept { return false; }
            inline static void await_suspend(std::coroutine_handle<promise_type> hnd) noexcept { hnd.destroy(); }
            static constexpr void await_resume() noexcept {}
        };

        return awaiter{};
    }

    static constexpr void return_void() noexcept {}
    inline static void unhandled_exception() noexcept { std::terminate(); }
};
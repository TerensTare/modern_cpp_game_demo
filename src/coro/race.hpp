
#pragma once

#include "coro/scheduler.hpp"
#include "coro/task.hpp"
#include "utils/function_ref.hpp"

// A coroutine type that can be scheduled on a `race_scheduler` to run some coroutines and wait for the first one to finish.
// When the race finishes, every non-winner coroutine will be destroyed no matter where they are.
// See `race_scope` for an easy way to set up a race.
struct racing_coro final
{
    struct promise_type;
};

struct race_scheduler final
{
    // sync version; run a single step of the race
    // if return == -1, there are no winners yet
    inline uint32_t step()
    {
        // TODO: update to match the regular scheduler API
        // ^ one option is to pass `ctx` in scheduler's constructor
        // TODO: log the runtimes into the profiler
        auto const n = susp.size();
        for (size_t i{}; winner == -1 && i < n; ++i)
        {
            auto hnd = susp.front();
            susp.pop();
            hnd.resume();
        }

        if (winner != -1)
        {
            while (!susp.empty())
            {
                auto hnd = susp.front();
                susp.pop();
                hnd.destroy();
            }
        }

        return winner;
    }

    // async version, must pass the parent scheduler
    task<uint32_t> run_on(stage_info &s)
    {
        while (true)
        {
            if (step() != -1)
                break;

            co_await s.sched();
        }

        co_return winner;
    }

    [[nodiscard("Must `co_await`")]]
    constexpr auto sched() noexcept
    {
        struct awaiter final
        {
            race_scheduler *race;

            static constexpr bool await_ready() noexcept { return false; }

            inline auto await_suspend(std::coroutine_handle<racing_coro::promise_type> hnd) noexcept
            {
                race->susp.push(hnd);
            }

            static constexpr void await_resume() noexcept {}
        };

        return awaiter{this};
    }

private:
    uint32_t
        winner = ~uint32_t{},
        count = {};
    std::queue<std::coroutine_handle<racing_coro::promise_type>> susp;

    friend racing_coro::promise_type; // HACK: do something better
};

// TODO: `blocking(scope)`/`sync_wait(scope)` that does the equivalent
// Utility for scheduling some coroutines and then waiting for the first one to finish. It allows you to write:
// ```cpp
// race_scheduler race;
// coro1(race, args1...);
// coro2(race, args2...);
// auto winner = co_await race.run_on(sched);
// ```
// as
// ```cpp
// auto winner = co_await race_scope(sched, [](auto &race) {
//     coro1(race, args1...);
//     coro2(race, args2...);
// });
// ```
inline task<uint32_t> race_scope(stage_info &s, function_ref<void(race_scheduler &)> scope)
{
    race_scheduler race;
    scope(race);
    co_return co_await race.run_on(s);
}

struct racing_coro::promise_type final
{
    explicit constexpr promise_type(race_scheduler &ctx, ...) noexcept
        : ctx{&ctx}
    {
        id = ctx.count++;
    }

    inline auto initial_suspend() noexcept
    {
        return ctx->sched();
    }

    inline auto final_suspend() noexcept
    {
        // TODO: is this correct?
        ctx->winner = id;
        return std::suspend_never{};
    }

    static constexpr racing_coro get_return_object() noexcept { return racing_coro{}; }
    static constexpr void return_void() noexcept {}

    [[noreturn]]
    inline static void unhandled_exception()
    {
        std::terminate();
    }

    uint32_t id;
    race_scheduler *ctx;
};
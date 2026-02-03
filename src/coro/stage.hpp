

#pragma once

#include <coroutine>
#include <queue>
#include <source_location>
#include <span>
#include <stop_token>

#include <SDL3/SDL_timer.h>

#include "coro/fire_and_forget.hpp"
#include "coro/profiler.hpp"

#include "utils/func_name.hpp"

// NOTE: this is not multi-threaded yet

// TODO:
// - bring back scheduler, but drop the executor for stages
// - keep track of the enqueue time for each coroutine
// - how do you keep track of starting `co_await` time and then enqueue time and then execution duration?
// ^ probably best for the trace to have wait_start + wait_finish fields to denote when the wait started and when the wait completed
// - schedule coroutines to a specific thread
// - show sleeps + other awaitables
// ^ sleep will overlap with other tasks; how do you denote that?
// - you can distinct coroutines by hash(handle) and keep all events from one task in one vector
// - show an arrow to denote tasks that call other tasks

struct coro_state final
{
    std::coroutine_handle<> hnd;
    std::source_location suspend_point;
};

struct context;

// NOTE: do not create these directly; instead make a new stage by calling `scheduler.stage`
// TODO: events for stage begin/end
// TODO: set the context
struct stage_info final
{
    stage_info() = default;

    stage_info(stage_info const &) = delete;
    stage_info &operator=(stage_info const &) = delete;

    stage_info(stage_info &&) = default;
    stage_info &operator=(stage_info &&) = default;

    inline void run(context &ctx);

    // Schedule the coroutine for the next time this stage runs
    inline void schedule(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current())
    {
        ready_queue.push({hnd, sl});
    }

    // Schedule the coroutine to run after `ms` time
    inline void schedule_after(std::coroutine_handle<> hnd, Uint64 ms, std::source_location const &sl = std::source_location::current())
    {
        waiting.push({hnd, sl, time + ms});
    }

    // Schedule the coroutine for the next time this stage runs
    struct sched_awaiter;
    inline sched_awaiter sched() noexcept;

    // Schedule the coroutine to run after `ms` time
    struct sleep_awaiter;
    inline sleep_awaiter sleep(Uint64 ms) noexcept;

private:
    struct waiting_coro final
    {
        std::coroutine_handle<> hnd;
        std::source_location suspend_point;
        Uint64 when_ready;

        struct compare_time final
        {
            constexpr bool operator()(waiting_coro lhs, waiting_coro rhs) noexcept
            {
                return lhs.when_ready > rhs.when_ready;
            }
        };
    };

    Uint64 time = SDL_GetTicks();
    Uint64 last_time = time;
    std::queue<coro_state> ready_queue;
    std::priority_queue<waiting_coro, std::vector<waiting_coro>, waiting_coro::compare_time> waiting;
};

struct context final
{
    // invariant: the loop on `stage_info::run` guarantees the ordering of the traces, so no need for a priority queue
    struct
    {
        Uint64 delta = 0, now = SDL_GetTicks();
    } time;

    std::vector<trace> traces;
};

inline void stage_info::run(context &ctx)
{
    time = SDL_GetTicks();

    ctx.time = {
        .delta = time - last_time,
        .now = time,
    };

    while (!waiting.empty())
    {
        auto top = waiting.top();
        if (top.when_ready > time)
            break;

        waiting.pop();
        ready_queue.push({top.hnd, top.suspend_point});
    }

    auto const n = ready_queue.size();
    ctx.traces.reserve(ctx.traces.size() + n);

    auto start = SDL_GetTicks();

    for (size_t i{}; i < n; ++i)
    {
        auto t = ready_queue.front();
        ready_queue.pop();
        t.hnd.resume();

        auto const finish = SDL_GetTicks();
        auto func_name = t.suspend_point.function_name();

        ctx.traces.push_back({
            .name = trim_func_name(func_name),
            .line = t.suspend_point.line(),
            // .stage = id,
            // TODO: set stage id
            .tid = std::this_thread::get_id(), // TODO: address when you have multiple threads; reuse per task
            .start = start,
            .finish = finish,
        });

        start = finish;
        // TODO: do you update context time here?
    }

    last_time = time;
}

struct [[nodiscard]] stage_info::sched_awaiter final
{
    stage_info *s;

    static constexpr bool await_ready() noexcept { return false; }

    inline void await_suspend(std::coroutine_handle<> hnd,
                              std::source_location const &sl = std::source_location::current()) noexcept
    {
        s->schedule(hnd, sl);
    }

    static constexpr void await_resume() noexcept {}
};
inline auto stage_info::sched() noexcept -> stage_info::sched_awaiter { return sched_awaiter{this}; }

struct [[nodiscard]] stage_info::sleep_awaiter final
{
    stage_info *s;
    Uint64 ms;

    static constexpr bool await_ready() noexcept { return false; }

    inline void await_suspend(std::coroutine_handle<> hnd,
                              std::source_location const &sl = std::source_location::current()) noexcept
    {
        s->schedule_after(hnd, ms, sl);
    }

    static constexpr void await_resume() noexcept {}
};
inline auto stage_info::sleep(Uint64 ms) noexcept -> stage_info::sleep_awaiter { return sleep_awaiter{this, ms}; }

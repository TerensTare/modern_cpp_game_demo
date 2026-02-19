
#pragma once

#include <coroutine>
#include <cstdint>
#include <source_location>
#include <string_view>
#include <thread>

#include <entt/container/dense_map.hpp>

#include <SDL3/SDL_timer.h>

enum class stage_id : uint32_t
{
    startup,
    update,
    render,
    cleanup,
    _custom, // values smaller than this are reserved
    none = ~uint32_t{},
};

struct stage_id_hash final
{
    constexpr size_t operator()(stage_id sid) const noexcept
    {
        return static_cast<std::underlying_type_t<stage_id>>(sid);
    }
};

struct suspended final
{
    std::coroutine_handle<> hnd;
    suspended *next = nullptr;
};

struct sleeping final
{
    suspended susp;
    uint64_t when_ready;

    friend constexpr bool operator>(sleeping const &lhs, sleeping const &rhs) noexcept
    {
        return lhs.when_ready > rhs.when_ready;
    }
};

struct wait_list final
{
    suspended *first = nullptr, *last = first;

    constexpr void push(wait_list &other) noexcept
    {
        // TODO: better impl
        if (!is_empty())
        {
            if (!other.is_empty())
            {
                last->next = other.first;
                last = other.last;
            }
            // else nothing
        }
        else
        {
            if (!other.is_empty())
            {
                *this = other;
            }
            // else nothing
        }
    }

    // NOTE: `sp` is passed by reference to make sure we don't push `nullptr`
    constexpr void push_one(suspended &sp) noexcept
    {
        if (!is_empty())
        {
            last->next = &sp;
            last = &sp;
        }
        else
        {
            last = first = &sp;
        }
    }

    [[nodiscard]]
    constexpr suspended *pop() noexcept
    {
        if (first)
            return std::exchange(first, first->next);
        return nullptr;
    }

    [[nodiscard]] constexpr bool is_empty() const noexcept { return !first; }
};

struct trace final
{
    std::string_view name;
    uint_least32_t line;
    stage_id stage;
    std::thread::id tid;
    uint64_t start, finish;
};

struct context final
{
    struct
    {
        Uint64 delta = 0, now = SDL_GetTicks();
    } time;
    
    // invariant: the loop on `scheduler::transit` guarantees the ordering of the traces, so no need for a priority queue
    std::vector<trace> traces;
};

struct scheduler final
{
    inline explicit scheduler(context &ctx) : ctx{&ctx} {}

    // TODO: timed waiters (at stage?)
    context *ctx;
    stage_id current_stage = stage_id::none; // just for sanity, init to `none`
    entt::dense_map<stage_id, wait_list, stage_id_hash> waiting_for_stage;
    wait_list current_stage_list;
    wait_list next_stage_list;

    // move to the given stage and run all the coroutines waiting on it
    inline void transit(stage_id sid) noexcept
    {
        // TODO: warn if current_stage == next_stage
        current_stage = sid;

        // TODO: do you push these at front or back?
        {
            auto waiting = std::exchange(waiting_for_stage[sid], {});
            next_stage_list.push(waiting);
        }
        // TODO: also add time-sleeping coroutines

        // invariant: current_stage_list should be empty here because of the loop below
        current_stage_list = {}; // just to update the `last` pointer
        std::swap(current_stage_list, next_stage_list);

        // poll all coroutines waiting in this stage
        // the chain of symmetric transfer doesn't account for cases when a coroutine finishes, so we have to loop here to make sure we poll everything
        while (current_stage_list.first)
        {
            current_stage_list.pop()->hnd.resume();
        }
    }

    struct next_stage_awaiter;
    [[nodiscard("Must be co_await")]]
    constexpr next_stage_awaiter next_stage() noexcept;

    struct on_stage_awaiter;
    [[nodiscard("Must be co_await")]]
    constexpr on_stage_awaiter on_stage(stage_id sid) noexcept;

    // v-- for internal use by awaiters
    [[nodiscard]]
    constexpr std::coroutine_handle<> pick_next() noexcept
    {
        auto link = current_stage_list.pop();
        return link ? link->hnd
                    : std::noop_coroutine();
    }
};

struct scheduler::next_stage_awaiter final
{
    scheduler *ctx;
    suspended susp;

    static constexpr bool await_ready() noexcept { return false; }

    constexpr auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        susp.hnd = hnd;
        ctx->next_stage_list.push_one(susp);
        return ctx->pick_next();
    }

    constexpr stage_id await_resume() const noexcept { return ctx->current_stage; }
};
constexpr scheduler::next_stage_awaiter scheduler::next_stage() noexcept { return next_stage_awaiter{this}; }

struct scheduler::on_stage_awaiter final
{
    scheduler *ctx;
    stage_id sid;
    suspended susp;

    static constexpr bool await_ready() noexcept { return false; }

    inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        susp.hnd = hnd;
        ctx->waiting_for_stage[sid].push_one(susp);
        return ctx->pick_next();
    }

    static constexpr void await_resume() noexcept {}
};
constexpr scheduler::on_stage_awaiter scheduler::on_stage(stage_id sid) noexcept { return on_stage_awaiter{this, sid}; }

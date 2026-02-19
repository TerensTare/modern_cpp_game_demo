
#pragma once

#include <optional>
#include "coro/stage.hpp"

// an event type that remains permanently "triggered" if set

struct permanent_event_awaiter;

struct permanent_event final
{
    inline explicit permanent_event(stage_info &s) : s{&s} {}

    inline void trigger()
    {
        set = true;

        auto awt = first;
        while (awt)
        {
            s->schedule(awt->hnd, awt->suspend_point);
            awt = awt->next;
        }
        first = awt;
    }

    // sync API
    constexpr bool has_happened() const noexcept { return set; }

    // async API
    [[nodiscard("Must be `co_await`")]]
    inline permanent_event_awaiter operator co_await() noexcept;

private:
    stage_info *s;
    permanent_event_awaiter *first = nullptr;
    bool set = false;

    friend permanent_event_awaiter;
};

struct permanent_event_awaiter final
{
    permanent_event *e;
    std::coroutine_handle<> hnd;
    std::source_location suspend_point;
    permanent_event_awaiter *next = nullptr;

    constexpr bool await_ready() const noexcept { return e->set; }

    constexpr void await_suspend(std::coroutine_handle<> hnd, std::source_location const &sl = std::source_location::current()) noexcept
    {
        this->hnd = hnd;
        suspend_point = sl;
        next = e->first;
        e->first = this;
    }

    static constexpr void await_resume() noexcept {}
};

inline permanent_event_awaiter permanent_event::operator co_await() noexcept { return permanent_event_awaiter{this}; }
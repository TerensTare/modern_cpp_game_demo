
#pragma once

#include <stop_token>

#include "coro/stage.hpp"
#include "coro/fire_and_forget.hpp"

namespace detail
{
    // TODO: use an `event<>` instead but make it thread-safe
    inline auto timeout_coro(stage_info &stage, std::stop_token &out, Uint64 ms) -> fire_and_forget
    {
        std::stop_source stop;
        out = stop.get_token();

        co_await stage.sleep(ms);

        stop.request_stop();
    }
}

// Register a new task to timeout after the given time in ms and return a stop token to listen for timeout.
// Example:
/*
```cpp
auto stop_token = timeout(sched, 1000); // timeout after 1 second
my_task(sched, stop_token); // spawn your task; check for stops inside the task
```
*/
[[nodiscard("You should use the token to check for timeout")]]
inline auto timeout(stage_info &stage, Uint64 ms) -> std::stop_token
{
    std::stop_token out;
    detail::timeout_coro(stage, out, ms);
    return out;
}

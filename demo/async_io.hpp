
#pragma once

#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_messagebox.h>

#include "coro/fire_and_forget.hpp"
#include "coro/stage.hpp"

struct async_io final
{
    async_io(async_io const &) = delete;
    async_io &operator=(async_io const &) = delete;

    async_io(async_io &&) = delete;
    async_io &operator=(async_io &&) = delete;

    inline async_io() noexcept
        : queue{SDL_CreateAsyncIOQueue()} {}

    inline ~async_io()
    {
        SDL_DestroyAsyncIOQueue(queue);
    }

    // TODO: run_on(scheduler)
    inline fire_and_forget run_on(stage_info &s);

    struct read_awaiter;
    // Return a `SDL_IOStream` to the desired path when `co_await`. Must close the given iostream. SDL loader functions usually provide a `closeio` parameter to help with that.
    [[nodiscard("Must be `co_await`")]]
    inline read_awaiter read(char const *path) noexcept;

private:
    SDL_AsyncIOQueue *queue;
};

struct async_io::read_awaiter final
{
    SDL_AsyncIOQueue *queue;
    char const *path;
    std::coroutine_handle<> then;
    size_t buff_size;
    void *buff;

    static constexpr bool await_ready() noexcept { return false; }

    inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        then = hnd;
        if (!SDL_LoadFileAsync(path, queue, this))
        {
            // TODO: better error message
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to submit async load job", SDL_GetError(), nullptr);
        }

        // TODO: signal the "run_on" task to start polling again if in sleep
    }

    inline auto await_resume() noexcept -> SDL_IOStream *
    {
        // TODO: how does this interact with pngs/etc. since fonts were a "special case"?
        auto stream = SDL_IOFromConstMem(buff, buff_size);
        SDL_SetPointerProperty(
            SDL_GetIOProperties(stream),
            SDL_PROP_IOSTREAM_MEMORY_FREE_FUNC_POINTER, SDL_free //
        );
        return stream;
    }
};
inline async_io::read_awaiter async_io::read(char const *path) noexcept
{
    return read_awaiter{queue, path};
}

inline fire_and_forget async_io::run_on(stage_info &s)
{
    while (true)
    {
        // TODO: keep a counter and if it reaches 0 here go to sleep, every time it goes up wake this task
        SDL_AsyncIOOutcome out;
        while (SDL_GetAsyncIOResult(queue, &out))
        {
            // TODO: process this outcome to resume the given task
            if (out.result != SDL_ASYNCIO_COMPLETE)
            {
                SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Failed to complete async load job", SDL_GetError(), nullptr);
            }

            auto awt = (read_awaiter *)out.userdata;
            awt->buff_size = out.bytes_transferred;
            awt->buff = out.buffer;
            // HACK: better scheduling
            s.schedule(awt->then);
        }

        co_await s.sched();
    }
}
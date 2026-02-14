
#pragma once

#include <coroutine>
#include <exception>

template <typename T>
struct generator_promise;

struct sentinel_t final
{
};

template <typename T>
struct generator_iterator final
{
    std::coroutine_handle<generator_promise<T>> hnd;

    inline void operator++() const { hnd.resume(); }
    inline T const &operator*() const { return hnd.promise().value; }

    friend bool operator==(generator_iterator<T> const &self, sentinel_t) noexcept { return self.hnd.done(); }
};

template <typename T>
struct generator final
{
    generator(generator const &) = delete;
    generator &operator=(generator const &) = delete;

    inline generator(generator &&rhs) noexcept
        : hnd{rhs.hnd}
    {
        rhs.hnd = nullptr;
    }

    inline generator &operator==(generator &&rhs) noexcept
    {
        std::swap(hnd, rhs.hnd);
        return *this;
    }

    inline ~generator()
    {
        if (hnd)
            hnd.destroy();
    }

    using promise_type = generator_promise<T>;

    inline generator_iterator<T> begin() const
    {
        hnd.resume();
        return {hnd};
    }

    static constexpr sentinel_t end() noexcept { return {}; }

private:
    inline explicit generator(promise_type &promise) noexcept
        : hnd{std::coroutine_handle<promise_type>::from_promise(promise)} {}

    std::coroutine_handle<promise_type> hnd;

    template <typename>
    friend struct generator_promise;
};

template <typename T>
struct generator_promise final
{
    T *yld;

    void await_transform(auto &&) = delete;

    inline auto get_return_object() noexcept { return generator{*this}; }

    static constexpr std::suspend_always initial_suspend() noexcept { return {}; }
    static constexpr std::suspend_always final_suspend() noexcept { return {}; }

    constexpr std::suspend_always yield_value(T &t) noexcept
    {
        yld = &t;
        return {};
    }

    constexpr std::suspend_always yield_value(T &&t) noexcept { return yield_value(t); }

    [[noreturn]]
    inline static void unhandled_exception()
    {
        std::terminate();
    }
};

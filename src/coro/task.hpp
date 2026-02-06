
#pragma once

#include <coroutine>
#include <optional>

template <typename T>
struct task_promise;

template <typename T = void>
struct [[nodiscard("task is a RAII type and should be `co_await`.\n"
                   "If this task is the topmost and you want to schedule it, note that tasks do not run unless you resume them.\n"
                   "For this specific case, consider using fire_and_forget instead as return type.")]] task final
{
    using promise_type = task_promise<T>;
    struct awaiter_type;

    task(task const &) = delete;
    task &operator=(task const &) = delete;

    inline task(task &&t) noexcept : hnd{t.hnd} { t.hnd = nullptr; }

    inline task &operator=(task &&t) noexcept
    {
        if (hnd && !hnd.done())
            hnd.destroy();

        hnd = t.hnd;
        t.hnd = nullptr;

        return *this;
    }

    inline ~task()
    {
        if (hnd && !hnd.done())
            hnd.destroy();
    }

    inline awaiter_type operator co_await() const;

    std::coroutine_handle<promise_type> hnd; // TODO: make this private eventually

private:
    inline explicit task(promise_type &promise) noexcept
        : hnd{std::coroutine_handle<promise_type>::from_promise(promise)} {}

    friend task_promise<T>;
};

template <typename T>
struct task_promise_base
{
    struct final_awaiter;

    std::coroutine_handle<> cont = std::noop_coroutine();

    static constexpr auto initial_suspend() noexcept { return std::suspend_always{}; }
    static constexpr final_awaiter final_suspend() noexcept;

    static constexpr void unhandled_exception() noexcept {}
};

template <typename T>
struct task_promise final : task_promise_base<T>
{
    std::optional<T> value;

    inline task<T> get_return_object() noexcept { return task{*this}; }
    constexpr void return_value(T const &value) noexcept { this->value = value; }
};

template <>
struct task_promise<void> : task_promise_base<void>
{
    inline task<> get_return_object() noexcept { return task{*this}; }
    constexpr void return_void() const noexcept {}
};

template <typename T>
struct task_promise_base<T>::final_awaiter final
{
    static constexpr bool await_ready() noexcept { return false; }

    inline static auto await_suspend(std::coroutine_handle<task_promise<T>> hnd) noexcept
    {
        return hnd.promise().cont;
    }

    static constexpr void await_resume() noexcept {}
};

template <typename T>
constexpr task_promise_base<T>::final_awaiter task_promise_base<T>::final_suspend() noexcept { return final_awaiter{}; }

template <typename T>
struct task<T>::awaiter_type final
{
    std::coroutine_handle<promise_type> hnd;

    inline bool await_ready() const noexcept { return !hnd || hnd.done(); }

    inline auto await_suspend(std::coroutine_handle<> hnd) const noexcept
    {
        this->hnd.promise().cont = hnd;
        return this->hnd;
    }

    inline T await_resume() const noexcept
    {
        if constexpr (!std::is_void_v<T>)
            return *hnd.promise().value;
    }
};

template <typename T>
inline task<T>::awaiter_type task<T>::operator co_await() const
{
    return awaiter_type{hnd};
}
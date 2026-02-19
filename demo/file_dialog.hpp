
#pragma once

#include <memory>
#include <string>

#include <SDL3/SDL_dialog.h>
#include "coro/scheduler.hpp"

class file_dialog final
{
    // TODO: should this be `path` instead?
    inline static std::unique_ptr<std::string[]> copy_list(char const *const *files, uint32_t &count)
    {
        count = 0;
        std::string *out = nullptr;
        if (files)
        {
            for (auto iter = files; *iter; *iter++, count++)
                ;

            out = new std::string[count];
            for (size_t i{}; i < count; ++i)
                out[i] = files[i];
        }

        return std::unique_ptr<std::string[]>(out);
    }

public:
    struct open_file_cfg final
    {
        SDL_Window *win = nullptr;
        std::span<SDL_DialogFileFilter const> filters{(SDL_DialogFileFilter const *)nullptr, 0}; // NOTE: this must live until the dialog "completes" (is closed or user picks something)
        char const *default_location = nullptr;
        bool allow_many = false;
    };

    struct save_file_cfg final
    {
        SDL_Window *win = nullptr;
        std::span<SDL_DialogFileFilter const> filters; //< NOTE: this must live until the dialog "completes" (is closed or user picks something)
        char const *default_location = nullptr;
    };

    struct open_folder_cfg final
    {
        SDL_Window *win = nullptr;
        char const *default_location = nullptr;
        bool allow_many = false;
    };

    struct result final
    {
        inline bool is_ok() const noexcept { return files.get() != nullptr; }

        int32_t which_filter = -1; // NOTE: for `open_folder`, this will be -1 so not a reliable way to check for errors
        uint32_t files_count = 0;
        std::unique_ptr<std::string[]> files{nullptr};
    };

    struct open_file_awaiter;
    [[nodiscard("Must be `co_await`")]]
    inline static open_file_awaiter open_file(stage_info &s, open_file_cfg const &cfg) noexcept;

    struct save_file_awaiter;
    [[nodiscard("Must be `co_await`")]]
    inline static save_file_awaiter save_file(stage_info &s, save_file_cfg const &cfg) noexcept;

    struct open_folder_awaiter;
    [[nodiscard("Must be `co_await`")]]
    inline static open_folder_awaiter open_folder(stage_info &s, open_folder_cfg const &cfg) noexcept;
};

struct file_dialog::open_file_awaiter final
{
    stage_info *s;
    open_file_cfg const *cfg;
    std::coroutine_handle<> then;
    int which_filter;
    uint32_t files_count;
    std::unique_ptr<std::string[]> files;

    static constexpr bool await_ready() noexcept { return false; }

    // TODO: resume the next task here as optimization
    inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        then = hnd;
        SDL_ShowOpenFileDialog(
            (SDL_DialogFileCallback)callback, this,
            cfg->win,
            cfg->filters.data(), cfg->filters.size(),
            cfg->default_location, cfg->allow_many //
        );
    }

    inline result await_resume() noexcept
    {
        return {
            .which_filter = which_filter,
            .files_count = files_count,
            .files = std::move(files),
        };
    }

private:
    static void callback(open_file_awaiter *awt, const char *const *filelist, int filter)
    {
        awt->files = copy_list(filelist, awt->files_count);
        awt->which_filter = filter;
        awt->s->schedule(awt->then);
    }
};
inline file_dialog::open_file_awaiter file_dialog::open_file(stage_info &s, open_file_cfg const &cfg) noexcept { return open_file_awaiter{&s, &cfg}; }

struct file_dialog::save_file_awaiter final
{
    stage_info *s;
    save_file_cfg const *cfg;
    std::coroutine_handle<> then;
    int which_filter;
    uint32_t files_count;
    std::unique_ptr<std::string[]> files;

    static constexpr bool await_ready() noexcept { return false; }

    // TODO: resume the next task here as optimization
    inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        then = hnd;
        SDL_ShowSaveFileDialog(
            (SDL_DialogFileCallback)callback, this,
            cfg->win,
            cfg->filters.data(), cfg->filters.size(),
            cfg->default_location //
        );
    }

    inline result await_resume() noexcept
    {
        return {
            .which_filter = which_filter,
            .files_count = files_count,
            .files = std::move(files),
        };
    }

private:
    static void callback(save_file_awaiter *awt, const char *const *filelist, int filter)
    {
        awt->files = copy_list(filelist, awt->files_count);
        awt->which_filter = filter;
        awt->s->schedule(awt->then);
    }
};
inline file_dialog::save_file_awaiter file_dialog::save_file(stage_info &s, save_file_cfg const &cfg) noexcept { return save_file_awaiter{&s, &cfg}; }

struct file_dialog::open_folder_awaiter final
{
    stage_info *s;
    open_folder_cfg const *cfg;
    std::coroutine_handle<> then;
    int which_filter;
    uint32_t files_count;
    std::unique_ptr<std::string[]> files;

    static constexpr bool await_ready() noexcept { return false; }

    // TODO: resume the next task here as optimization
    inline auto await_suspend(std::coroutine_handle<> hnd) noexcept
    {
        then = hnd;
        SDL_ShowOpenFolderDialog(
            (SDL_DialogFileCallback)callback, this,
            cfg->win,
            cfg->default_location, cfg->allow_many //
        );
    }

    inline result await_resume() noexcept
    {
        return {
            .which_filter = which_filter,
            .files_count = files_count,
            .files = std::move(files),
        };
    }

private:
    static void callback(open_folder_awaiter *awt, const char *const *filelist, int filter)
    {
        awt->files = copy_list(filelist, awt->files_count);
        awt->which_filter = filter;
        awt->s->schedule(awt->then);
    }
};
inline file_dialog::open_folder_awaiter file_dialog::open_folder(stage_info &s, open_folder_cfg const &cfg) noexcept { return open_folder_awaiter{&s, &cfg}; }

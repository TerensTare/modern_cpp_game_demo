
#pragma once

#include <string_view>
#include <thread>

enum class stage_id : uint32_t
{
    startup,
    update,
    render,
    cleanup,
};

struct trace final
{
    std::string_view name;
    uint_least32_t line;
    stage_id stage;
    std::thread::id tid;
    uint64_t start, finish;
};

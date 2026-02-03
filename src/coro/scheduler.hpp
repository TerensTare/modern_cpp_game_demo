
#pragma once

#include <entt/core/utility.hpp>
#include <entt/container/dense_map.hpp>
#include "coro/stage.hpp"

struct scheduler final
{
    scheduler() = default;

    scheduler(scheduler const &) = delete;
    scheduler &operator=(scheduler const &) = delete;

    scheduler(scheduler &&) = delete;
    scheduler &operator=(scheduler &&) = delete;

    struct stage_id_hash final
    {
        constexpr std::size_t operator()(stage_id s) const noexcept
        {
            return (std::size_t)static_cast<std::underlying_type_t<stage_id>>(s);
        }
    };

    std::stop_source stop;
    entt::dense_map<stage_id, stage_info, stage_id_hash> stages;
};
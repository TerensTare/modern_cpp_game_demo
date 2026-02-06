
#pragma once

#include <imgui.h>
#include "coro/profiler.hpp"

constexpr ImU32 color_by_tag(uint32_t tag) noexcept
{
    ImU32 colors[] = {
        IM_COL32(180, 120, 255, 255),
        IM_COL32(100, 200, 255, 255),
        IM_COL32(120, 255, 180, 255),
        IM_COL32(255, 180, 120, 255),
        IM_COL32(255, 120, 120, 255),
        IM_COL32(200, 255, 100, 255),
        IM_COL32(255, 255, 100, 255),
        IM_COL32(120, 255, 255, 255),
    };

    return colors[tag % std::size(colors)];
}

inline void coroutine_profiler(std::span<trace const> traces)
{
    if (ImGui::Begin("Profiler"))
    {
        static bool record = true;
        // if (ImGui::Button(record ? "Pause" : "Record"))
        // {
        //     record = !record;

        //     using namespace entt::literals;
        //     if (!record)
        //     {
        //         profiler::remove_sink("<imgui>"_hs);
        //     }
        //     else
        //     {
        //         profiler::add_sink("<imgui>"_hs, *this);
        //     }
        // }

        // ImGui::SameLine();

        static bool auto_scroll = true;
        ImGui::Checkbox("Auto scroll", &auto_scroll);

        if (ImGui::BeginChild("ProfilerChild", ImVec2(0, 300), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar))
        {
            static auto time_scale = 0.1f; // pixels per microsecond

            auto const min_time = traces.size() == 0 ? 0 : traces.front().start;
            auto const max_time = traces.size() == 0 ? 0 : traces.back().finish;

            auto const content_width = (max_time - min_time) * time_scale;
            ImGui::Dummy(ImVec2(content_width, 0)); // extend scrollable width

            // Check if user has manually scrolled
            auto const scroll_x = ImGui::GetScrollX();
            auto const max_scroll_x = ImGui::GetScrollMaxX();
            if (scroll_x < max_scroll_x - 5.0f)
            {
                auto_scroll = false; // user moved away
            }

            if (auto_scroll)
            {
                ImGui::SetScrollX(content_width); // scroll to the right edge
            }

            auto draw_list = ImGui::GetWindowDrawList();
            auto const origin = ImGui::GetCursorScreenPos();

            auto const y = origin.y;
            auto const row_height = 20.0f;
            auto const max_rows = 100;

            std::vector<int> stack;

            // TODO: rather stack by task id
            for (int i{}; const auto &e : traces)
            {
                while (!stack.empty())
                {
                    auto parent = stack.back();
                    if (e.start >= traces[parent].finish)
                    {
                        stack.pop_back();
                    }
                    else
                    {
                        break;
                    }
                }

                // auto const depth = stack.size();
                auto const depth = 0; // TODO: enable the hierarchy back eventually
                stack.push_back(i);

                auto const x_start = origin.x + (e.start - min_time) * time_scale;
                auto const x_end = x_start + (e.finish - e.start) * time_scale;
                auto const y_top = y + depth * row_height;
                auto const y_bottom = y_top + row_height - 2;

                ImVec2 const p0(x_start, y_top);
                ImVec2 const p1(x_end, y_bottom);
                auto const color = color_by_tag((uint32_t)e.stage);

                draw_list->AddRectFilled(p0, p1, color);
                draw_list->AddText(ImVec2(x_start + 2, y_top + 2), IM_COL32_WHITE, e.name.data(), e.name.data() + e.name.size());

                // Tooltip
                if (ImGui::IsMouseHoveringRect(p0, p1))
                {
                    if (ImGui::BeginTooltip())
                    {
                        ImGui::Text("Name: %.*s [line=%d]", e.name.size(), e.name.data(), e.line);
                        ImGui::Text("Time: %llu ms", e.start);
                        ImGui::Text("Duration: %llu ms", e.finish - e.start);
                        ImGui::Text("Depth: %d", depth);
                        ImGui::EndTooltip();
                    }
                }

                ++i;
            }

            // Handle zooming
            if (ImGui::IsWindowHovered() && ImGui::GetIO().MouseWheel != 0)
            {
                auto const zoom_factor = 1.1f;
                time_scale *= (ImGui::GetIO().MouseWheel > 0) ? zoom_factor : (1.0f / zoom_factor);
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}


#include <cstdio>
#include <span>

#include <entt/entity/registry.hpp>
#include <SDL3/SDL.h>

#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include "coro/event.hpp"
#include "coro/scheduler.hpp"
#include "coro/fire_and_forget.hpp"
#include "coro/profiler_gui.hpp"
#include "coro/task.hpp"
#include "coro/timeout.hpp"

// TODO:
// - async file loading
// - task allocator support

#define ENSURE(val, msg)                                     \
    if (!(val))                                              \
    {                                                        \
        std::printf("ERROR: " msg ". %s\n", SDL_GetError()); \
        return -1;                                           \
    }

struct pos final
{
    float x, y;
};

struct speed final
{
    float s; // pixels per second
};

// demo: adding Dear ImGui to your game
auto imgui_system(scheduler &sched, context &ctx, SDL_Window *win, SDL_Renderer *ren) -> fire_and_forget
{
    // initialize ImGui during startup
    co_await sched.stages[stage_id::startup].sched();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(win, ren);
    ImGui_ImplSDLRenderer3_Init(ren);

    while (!sched.stop.stop_requested())
    {
        // build the widgets during the update stage
        co_await sched.stages[stage_id::update].sched();

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        if (ImGui::Begin("Help"))
        {
            ImGui::Text("This app is a quick demo that shows how to use coroutines for gamedev.");
            ImGui::Text("Try and click anywhere in the window and see what happens :).");
            ImGui::Text("In the code you will see two kinds of coroutines: task<T> and fire_and_forget.");
            ImGui::Text("task<T> is just a computation that can be co_awaited and might have a value or void.");
            ImGui::Text("fire_and_forget is a computation without a value that you don't care to wait for.");
            ImGui::Text("Coroutines are scheduled into stages (startup, update, render, cleanup) by default but you can bring your own coroutine types and add your own stages as well.");
            ImGui::Text("You can either schedule coroutines next time a stage runs with `co_await stage.sched()`");
            ImGui::Text("or wait for some time with `co_await stage.sleep(timeInMs)`");
            ImGui::Text("Additionally, you can trigger and wait for events with event<T>");
            ImGui::Text("Finally, you can timeout coroutines if they run for too long with `timeout`; see `timeout_showcase` in the code to learn how.");
            ImGui::Text("You can see an additional window here with a small WIP profiler, no need to annotate your code to use it. Enjoy :).");
        }
        ImGui::End();

        // your widgets go here
        coroutine_profiler(ctx.traces);

        ImGui::Render();

        // submit ImGui "draw calls" after everything else so it shows up on top
        co_await sched.stages[stage_id::render].sched();

        // TODO: this must be ran after everything else
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), ren);
    }

    // cleanup ImGui when done
    co_await sched.stages[stage_id::cleanup].sched();

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

// system that makes the entity positions into a rectangle for rendering
inline void pos_to_rect(entt::registry &reg)
{
    auto const size = 50.0f;

    auto &&rects = reg.storage<SDL_FRect>();
    auto const &positions = reg.storage<pos>();
    for (auto &&[id, p] : positions.each())
    {
        if (!rects.contains(id))
            rects.emplace(id, 0.0f, 0.0f, size, size);
    }

    for (auto &&[id, p, r] : entt::basic_view{positions, rects}.each())
    {
        r.x = p.x - size / 2;
        r.y = p.y + size / 2;
    }
}

auto render_task(scheduler &sched, entt::registry &reg, SDL_Renderer *ren) -> fire_and_forget
{
    while (true)
    {
        co_await sched.stages[stage_id::render].sched();

        pos_to_rect(reg);

        for (auto &&[id, rect, col] : reg.view<SDL_FRect const, SDL_Color const>().each())
        {
            SDL_SetRenderDrawColor(ren, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(ren, &rect);
        }
    }
}

auto timeout_showcase(scheduler &sched, std::stop_token stop) -> fire_and_forget
{
    // sleep-and-print but also check for cancellations (timeouts)
    co_await sched.stages[stage_id::update].sleep(1000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After first sleep\n");

    co_await sched.stages[stage_id::update].sleep(2000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After second sleep\n");

    co_await sched.stages[stage_id::update].sleep(3000);

    if (stop.stop_requested())
    {
        std::printf("Cancelled\n");
        co_return;
    }

    std::printf("After third sleep\n");
}

constexpr float lerp(float a, float b, float pct)
{
    return a + (b - a) * pct;
}

// patrol the object around the given center clockwise
auto move_around(scheduler &sched, context &ctx, entt::registry &reg, entt::entity object, pos center, float dist) -> fire_and_forget
{
    // TODO: when mouse clicked, patrol to a new point
    auto &&[p, s] = reg.get<pos, speed const>(object);
    pos const quad[4]{
        {center.x - dist, center.y - dist}, // top-left
        {center.x + dist, center.y - dist}, // top-right
        {center.x + dist, center.y + dist}, // bot-right
        {center.x - dist, center.y + dist}, // bot-left
    };

    // pick the closest point to the current position
    auto which_point = [&]
    {
        auto const dx1 = quad[0].x - p.x;
        auto const dx2 = quad[2].x - p.x;
        auto const dy1 = quad[0].y - p.y;
        auto const dy2 = quad[2].y - p.y;

        auto const x1_less = abs(dx1) < abs(dx2);
        auto const y1_less = abs(dy1) < abs(dy2);

        if (x1_less)
            return y1_less ? 0 : 3;
        else
            return y1_less ? 1 : 2;
    }();

    while (true)
    {
        auto const target = quad[which_point];

        // calculate how long it would take to reach the next patrol point
        auto const len = hypot(target.x - p.x, target.y - p.y);
        auto const elapsed_time = len / (s.s / 1000.0f);

        auto const start_pos = p;
        auto const start_time = ctx.time.now;
        auto const end_time = start_time + elapsed_time;

        auto const inv_elapsed = 1.0f / elapsed_time;

        while (ctx.time.now < end_time)
        {
            co_await sched.stages[stage_id::update].sched();

            // if mouse clicked, patrol around the mouse's position when it was clicked
            if (float mx, my; SDL_GetMouseState(&mx, &my) & SDL_BUTTON_LMASK)
            {
                move_around(sched, ctx, reg, object, pos{mx, my}, dist);
                co_return;
            }

            // calculate progress from start and update position
            auto const t_pct = (ctx.time.now - start_time) * inv_elapsed;

            p = {
                .x = lerp(start_pos.x, target.x, t_pct),
                .y = lerp(start_pos.y, target.y, t_pct),
            };
        }

        // move on to the next point
        which_point = (which_point + 1) % 4;
    }
}

inline entt::entity spawn_player(scheduler &sched, context &ctx, entt::registry &reg)
{
    auto const id = reg.create();
    reg.emplace<SDL_Color>(id) = {0xff, 0x00, 0x00, 0xff};
    reg.emplace<pos>(id) = {500.0f, 500.0f};
    reg.emplace<speed>(id, 100.0f);

    move_around(sched, ctx, reg, id, pos{300.0f, 300.0f}, 100.0f);

    return id;
}

int main(int, char **)
{
    auto init_c = SDL_Init(SDL_INIT_VIDEO);
    ENSURE(init_c, "Couldn't init SDL3");

    auto win_flags = SDL_WINDOW_HIGH_PIXEL_DENSITY;
    auto win = SDL_CreateWindow("Modern C++ game example", 1280, 720, win_flags);
    ENSURE(win, "Couldn't create window");

    auto ren = SDL_CreateRenderer(win, nullptr);
    ENSURE(ren, "Couldn't create renderer");

    entt::registry reg;

    // declare the scheduler
    scheduler sched;
    context ctx;

    // create all the coroutines you plan to submit initially
    imgui_system(sched, ctx, win, ren);
    render_task(sched, reg, ren);

    spawn_player(sched, ctx, reg);

    {
        auto stop2 = timeout(sched.stages[stage_id::update], 3500);
        timeout_showcase(sched, stop2);
    }

    // run the startup stage
    sched.stages[stage_id::startup].run(ctx);

    while (!sched.stop.stop_requested())
    {
        SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                sched.stop.request_stop();
                break;

            case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
                if (event.window.windowID == SDL_GetWindowID(win))
                    sched.stop.request_stop();
                break;
            }
        }

        // game loop
        sched.stages[stage_id::update].run(ctx); // first, run the "tick" coroutines (game logic)

        // rendering
        {
            SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
            SDL_RenderClear(ren);

            sched.stages[stage_id::render].run(ctx); // then run rendering coroutines

            SDL_RenderPresent(ren);
        }

        SDL_Delay(1);
    }

    sched.stages[stage_id::cleanup].run(ctx); // finally run cleanup-related coros

    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();

    return 0;
}